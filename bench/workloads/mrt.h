//
// plume bench
//

#pragma once

#include "workload.h"
#include "plume_render_interface.h"

#include "shader_select.h"
#ifdef __APPLE__
#include "shaders/MrtVert.hlsl.metal.h"
#include "shaders/MrtFrag.hlsl.metal.h"
#endif
#include "shaders/MrtVert.hlsl.spirv.h"
#include "shaders/MrtFrag.hlsl.spirv.h"
#ifdef _WIN32
#include "shaders/MrtVert.hlsl.dxil.h"
#include "shaders/MrtFrag.hlsl.dxil.h"
#endif

#include <cstdint>
#include <cstdio>
#include <memory>

namespace plume::bench {

    // Renders to two color attachments (target0 = red, target1 = green), reads both
    // back, and verifies each got its own color. Exercises multiple render targets.
    inline Workload createMrtWorkload(RenderDevice *device, RenderCommandQueue *queue, RenderShaderFormat fmt) {
        struct Fixture {
            RenderDevice *device;
            std::unique_ptr<RenderShader> vs;
            std::unique_ptr<RenderShader> ps;
            std::unique_ptr<RenderPipelineLayout> layout;
            std::unique_ptr<RenderPipeline> pipeline;
            std::unique_ptr<RenderTexture> rt0;
            std::unique_ptr<RenderTexture> rt1;
            std::unique_ptr<RenderFramebuffer> fb;
            std::unique_ptr<RenderBuffer> readback0;
            std::unique_ptr<RenderBuffer> readback1;
        };
        auto fx = std::make_shared<Fixture>();
        fx->device = device;

        static constexpr uint32_t W = 16, H = 16;

        Workload w;
        w.name = "mrt_two_targets_readback";

        w.setup = [fx, fmt]() {
            fx->vs = PLUME_SELECT_SHADER(fx->device, fmt, "VSMain", MrtVert);
            fx->ps = PLUME_SELECT_SHADER(fx->device, fmt, "PSMain", MrtFrag);

            RenderTextureDesc rtDesc;
            rtDesc.dimension = RenderTextureDimension::TEXTURE_2D;
            rtDesc.width = W;
            rtDesc.height = H;
            rtDesc.depth = 1;
            rtDesc.mipLevels = 1;
            rtDesc.arraySize = 1;
            rtDesc.format = RenderFormat::R8G8B8A8_UNORM;
            rtDesc.flags = RenderTextureFlag::RENDER_TARGET;
            fx->rt0 = fx->device->createTexture(rtDesc);
            fx->rt1 = fx->device->createTexture(rtDesc);

            const RenderTexture *colors[2] = { fx->rt0.get(), fx->rt1.get() };
            RenderFramebufferDesc fbDesc;
            fbDesc.colorAttachments = colors;
            fbDesc.colorAttachmentsCount = 2;
            fx->fb = fx->device->createFramebuffer(fbDesc);

            fx->readback0 = fx->device->createBuffer(
                RenderBufferDesc::ReadbackBuffer(readbackRowPitchBytes(W, 4) * H));
            fx->readback1 = fx->device->createBuffer(
                RenderBufferDesc::ReadbackBuffer(readbackRowPitchBytes(W, 4) * H));

            RenderPipelineLayoutDesc layoutDesc;
            fx->layout = fx->device->createPipelineLayout(layoutDesc);

            RenderGraphicsPipelineDesc desc;
            desc.pipelineLayout = fx->layout.get();
            desc.vertexShader = fx->vs.get();
            desc.pixelShader = fx->ps.get();
            desc.renderTargetFormat[0] = RenderFormat::R8G8B8A8_UNORM;
            desc.renderTargetFormat[1] = RenderFormat::R8G8B8A8_UNORM;
            desc.renderTargetBlend[0] = RenderBlendDesc::Copy();
            desc.renderTargetBlend[1] = RenderBlendDesc::Copy();
            desc.renderTargetCount = 2;
            desc.primitiveTopology = RenderPrimitiveTopology::TRIANGLE_LIST;
            fx->pipeline = fx->device->createGraphicsPipeline(desc);
        };

        w.encode = [fx](RenderCommandList *cmd) {
            RenderTextureBarrier toWrite[2] = {
                RenderTextureBarrier(fx->rt0.get(), RenderTextureLayout::COLOR_WRITE),
                RenderTextureBarrier(fx->rt1.get(), RenderTextureLayout::COLOR_WRITE),
            };
            cmd->barriers(RenderBarrierStage::GRAPHICS, toWrite, 2);
            cmd->setFramebuffer(fx->fb.get());
            cmd->clearColor(0, RenderColor(0.0f, 0.0f, 0.0f, 1.0f));
            cmd->clearColor(1, RenderColor(0.0f, 0.0f, 0.0f, 1.0f));
            cmd->setGraphicsPipelineLayout(fx->layout.get());
            cmd->setPipeline(fx->pipeline.get());

            RenderViewport vp(0.0f, 0.0f, float(W), float(H));
            RenderRect sc(0, 0, int32_t(W), int32_t(H));
            cmd->setViewports(vp);
            cmd->setScissors(sc);
            cmd->drawInstanced(3, 1, 0, 0);

            RenderTextureBarrier toCopy[2] = {
                RenderTextureBarrier(fx->rt0.get(), RenderTextureLayout::COPY_SOURCE),
                RenderTextureBarrier(fx->rt1.get(), RenderTextureLayout::COPY_SOURCE),
            };
            cmd->barriers(RenderBarrierStage::COPY, toCopy, 2);

            RenderTextureCopyLocation dst0 = RenderTextureCopyLocation::PlacedFootprint(
                fx->readback0.get(), RenderFormat::R8G8B8A8_UNORM, W, H, 1, readbackRowTexels(W, 4), 0);
            RenderTextureCopyLocation src0 = RenderTextureCopyLocation::Subresource(fx->rt0.get(), 0, 0);
            cmd->copyTextureRegion(dst0, src0, 0, 0, 0, nullptr);

            RenderTextureCopyLocation dst1 = RenderTextureCopyLocation::PlacedFootprint(
                fx->readback1.get(), RenderFormat::R8G8B8A8_UNORM, W, H, 1, readbackRowTexels(W, 4), 0);
            RenderTextureCopyLocation src1 = RenderTextureCopyLocation::Subresource(fx->rt1.get(), 0, 0);
            cmd->copyTextureRegion(dst1, src1, 0, 0, 0, nullptr);
        };

        w.verify = [fx]() -> VerifyResult {
            auto *p0 = static_cast<const uint8_t *>(fx->readback0->map());
            auto *p1 = static_cast<const uint8_t *>(fx->readback1->map());
            if (p0 == nullptr || p1 == nullptr) {
                return VerifyResult::fail("readback map() returned null");
            }
            const uint32_t pitch = readbackRowPitchBytes(W, 4);
            const uint8_t *a = p0 + (H / 2) * pitch + (W / 2) * 4;
            const uint8_t *b = p1 + (H / 2) * pitch + (W / 2) * 4;
            auto close = [](uint8_t x, uint8_t y) { return (x > y ? x - y : y - x) <= 1; };
            VerifyResult result = VerifyResult::ok();
            // target0 red, target1 green
            if (!close(a[0], 0xFF) || !close(a[1], 0x00) || !close(a[2], 0x00) ||
                !close(b[0], 0x00) || !close(b[1], 0xFF) || !close(b[2], 0x00)) {
                char buf[128];
                snprintf(buf, sizeof(buf), "rt0=(%02x,%02x,%02x) rt1=(%02x,%02x,%02x), expected red/green",
                    a[0], a[1], a[2], b[0], b[1], b[2]);
                result = VerifyResult::fail(buf);
            }
            fx->readback0->unmap();
            fx->readback1->unmap();
            return result;
        };

        return w;
    }

}
