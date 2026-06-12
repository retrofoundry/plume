//
// plume bench
//

#pragma once

#include "workload.h"
#include "plume_render_interface.h"
#include "plume_render_interface_builders.h"

#include "shader_select.h"
#ifdef __APPLE__
#include "shaders/ImageStore.hlsl.metal.h"
#endif
#include "shaders/ImageStore.hlsl.spirv.h"
#ifdef _WIN32
#include "shaders/ImageStore.hlsl.dxil.h"
#endif

#include <cstdint>
#include <cstdio>
#include <memory>

namespace plume::bench {

    // Dispatches a compute kernel that writes a per-pixel gradient into a RWTexture2D (UAV),
    // copies the texture to a readback buffer, and verifies three pixels. Exercises the
    // storage-image write path (descriptor type, GENERAL layout, image barrier) and per-pixel
    // addressing — distinct from the RWStructuredBuffer write the compute_dispatch workload covers.
    inline Workload createImageStoreWorkload(RenderDevice *device, RenderCommandQueue *queue, RenderShaderFormat fmt) {
        struct Fixture {
            RenderDevice *device;
            std::unique_ptr<RenderShader> shader;
            std::unique_ptr<RenderPipelineLayout> layout;
            std::unique_ptr<RenderPipeline> pipeline;
            std::unique_ptr<RenderDescriptorSet> set;
            std::unique_ptr<RenderTexture> tex;
            std::unique_ptr<RenderBuffer> readback;
        };
        auto fx = std::make_shared<Fixture>();
        fx->device = device;

        static constexpr uint32_t W = 16, H = 16;

        Workload w;
        w.name = "image_store_readback";

        w.setup = [fx, fmt]() {
            fx->shader = PLUME_SELECT_SHADER(fx->device, fmt, "CSMain", ImageStore);

            RenderTextureDesc texDesc;
            texDesc.dimension = RenderTextureDimension::TEXTURE_2D;
            texDesc.width = W;
            texDesc.height = H;
            texDesc.depth = 1;
            texDesc.mipLevels = 1;
            texDesc.arraySize = 1;
            texDesc.format = RenderFormat::R8G8B8A8_UNORM;
            // Both flags required: Metal keys shader-write on UNORDERED_ACCESS, Vulkan on STORAGE.
            texDesc.flags = RenderTextureFlag::UNORDERED_ACCESS | RenderTextureFlag::STORAGE;
            fx->tex = fx->device->createTexture(texDesc);

            fx->readback = fx->device->createBuffer(
                RenderBufferDesc::ReadbackBuffer(readbackRowPitchBytes(W, 4) * H));

            RenderDescriptorSetBuilder b;
            b.begin();
            b.addReadWriteTexture(0); // register(u0)
            b.end();
            fx->set = fx->device->createDescriptorSet(b.descriptorSetDesc);
            fx->set->setTexture(0, fx->tex.get(), RenderTextureLayout::GENERAL);

            RenderPipelineLayoutDesc layoutDesc;
            layoutDesc.descriptorSetDescs = &b.descriptorSetDesc;
            layoutDesc.descriptorSetDescsCount = 1;
            fx->layout = fx->device->createPipelineLayout(layoutDesc);

            RenderComputePipelineDesc pipeDesc(fx->layout.get(), fx->shader.get(), 8, 8, 1);
            fx->pipeline = fx->device->createComputePipeline(pipeDesc);
        };

        w.encode = [fx](RenderCommandList *cmd) {
            cmd->barriers(RenderBarrierStage::COMPUTE,
                RenderTextureBarrier(fx->tex.get(), RenderTextureLayout::GENERAL));
            cmd->setComputePipelineLayout(fx->layout.get());
            cmd->setPipeline(fx->pipeline.get());
            cmd->setComputeDescriptorSet(fx->set.get(), 0);
            cmd->dispatch(W / 8, H / 8, 1); // 16x16 threads via numthreads(8,8,1)

            cmd->barriers(RenderBarrierStage::COPY,
                RenderTextureBarrier(fx->tex.get(), RenderTextureLayout::COPY_SOURCE));
            RenderTextureCopyLocation dst = RenderTextureCopyLocation::PlacedFootprint(
                fx->readback.get(), RenderFormat::R8G8B8A8_UNORM, W, H, 1, readbackRowTexels(W, 4), 0);
            RenderTextureCopyLocation src = RenderTextureCopyLocation::Subresource(fx->tex.get(), 0, 0);
            cmd->copyTextureRegion(dst, src, 0, 0, 0, nullptr);
        };

        w.verify = [fx]() -> VerifyResult {
            auto *px = static_cast<const uint8_t *>(fx->readback->map());
            if (px == nullptr) {
                return VerifyResult::fail("readback map() returned null");
            }
            auto close = [](uint8_t a, uint8_t b) { return (a > b ? a - b : b - a) <= 2; };
            // Three samples prove per-pixel addressing, not a constant blast:
            // (0,0)=(0,0), center (8,8)=(0x88,0x88), (15,15)=(0xFF,0xFF). B=0x80, A=0xFF throughout.
            struct Probe { uint32_t x, y; uint8_t r, g; };
            const Probe probes[3] = { {0, 0, 0x00, 0x00}, {8, 8, 0x88, 0x88}, {15, 15, 0xFF, 0xFF} };
            VerifyResult result = VerifyResult::ok();
            for (const auto &pr : probes) {
                const uint8_t *p = px + pr.y * readbackRowPitchBytes(W, 4) + pr.x * 4;
                if (!close(p[0], pr.r) || !close(p[1], pr.g) || !close(p[2], 0x80) || !close(p[3], 0xFF)) {
                    char buf[128];
                    snprintf(buf, sizeof(buf), "(%u,%u) = (%02x,%02x,%02x,%02x), expected (%02x,%02x,80,ff)",
                        pr.x, pr.y, p[0], p[1], p[2], p[3], pr.r, pr.g);
                    result = VerifyResult::fail(buf);
                    break;
                }
            }
            fx->readback->unmap();
            return result;
        };

        return w;
    }

}
