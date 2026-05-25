//
// plume bench
//

#pragma once

#include "workload.h"
#include "plume_render_interface.h"
#include "plume_render_interface_builders.h"

#include "shader_select.h"
#ifdef __APPLE__
#include "shaders/DescriptorDrawVert.hlsl.metal.h"
#include "shaders/DescriptorDrawFrag.hlsl.metal.h"
#endif
#include "shaders/DescriptorDrawVert.hlsl.spirv.h"
#include "shaders/DescriptorDrawFrag.hlsl.spirv.h"
#ifdef _WIN32
#include "shaders/DescriptorDrawVert.hlsl.dxil.h"
#include "shaders/DescriptorDrawFrag.hlsl.dxil.h"
#endif

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>

namespace plume::bench {

    // Draws a fullscreen triangle where out = baseColor * tint, both from a cbuffer
    // descriptor; reads the framebuffer back and verifies. Validates a graphics
    // constant-buffer descriptor binding. (Push constants are covered by the
    // solid_color-based workloads, which avoid the D3D12 root-constant/CBV register clash.)
    inline Workload createDescriptorDrawWorkload(RenderDevice *device, RenderCommandQueue *queue, RenderShaderFormat fmt) {
        struct Fixture {
            RenderDevice *device;
            std::unique_ptr<RenderShader> vs;
            std::unique_ptr<RenderShader> ps;
            std::unique_ptr<RenderPipelineLayout> layout;
            std::unique_ptr<RenderPipeline> pipeline;
            std::unique_ptr<RenderDescriptorSet> set;
            std::unique_ptr<RenderBuffer> constants;
            std::unique_ptr<RenderTexture> rt;
            std::unique_ptr<RenderFramebuffer> fb;
            std::unique_ptr<RenderBuffer> readback;
        };
        auto fx = std::make_shared<Fixture>();
        fx->device = device;

        static constexpr uint32_t W = 16, H = 16;
        static constexpr uint8_t ER = 0x80, EG = 0x40, EB = 0x80, EA = 0xFF;

        Workload w;
        w.name = "descriptor_draw_readback";

        w.setup = [fx, fmt]() {
            fx->vs = PLUME_SELECT_SHADER(fx->device, fmt, "VSMain", DescriptorDrawVert);
            fx->ps = PLUME_SELECT_SHADER(fx->device, fmt, "PSMain", DescriptorDrawFrag);

            RenderTextureDesc rtDesc;
            rtDesc.dimension = RenderTextureDimension::TEXTURE_2D;
            rtDesc.width = W;
            rtDesc.height = H;
            rtDesc.depth = 1;
            rtDesc.mipLevels = 1;
            rtDesc.arraySize = 1;
            rtDesc.format = RenderFormat::R8G8B8A8_UNORM;
            rtDesc.flags = RenderTextureFlag::RENDER_TARGET;
            fx->rt = fx->device->createTexture(rtDesc);

            const RenderTexture *color = fx->rt.get();
            RenderFramebufferDesc fbDesc;
            fbDesc.colorAttachments = &color;
            fbDesc.colorAttachmentsCount = 1;
            fx->fb = fx->device->createFramebuffer(fbDesc);

            fx->readback = fx->device->createBuffer(
                RenderBufferDesc::ReadbackBuffer(readbackRowPitchBytes(W, 4) * H));

            // cbuffer Params { float4 baseColor; float4 tint; } — both members in one CBV.
            fx->constants = fx->device->createBuffer(RenderBufferDesc::UploadBuffer(sizeof(float) * 8));
            float params[8] = { 0.5f, 0.5f, 1.0f, 1.0f,   1.0f, 0.5f, 0.5f, 1.0f }; // baseColor, tint
            std::memcpy(fx->constants->map(), params, sizeof(params));
            fx->constants->unmap();

            RenderDescriptorSetBuilder b;
            b.begin();
            b.addConstantBuffer(0); // register(b0)
            b.end();
            fx->set = fx->device->createDescriptorSet(b.descriptorSetDesc);
            fx->set->setBuffer(0, fx->constants.get(), sizeof(float) * 8);

            RenderPipelineLayoutDesc layoutDesc;
            layoutDesc.descriptorSetDescs = &b.descriptorSetDesc;
            layoutDesc.descriptorSetDescsCount = 1;
            fx->layout = fx->device->createPipelineLayout(layoutDesc);

            RenderGraphicsPipelineDesc desc;
            desc.pipelineLayout = fx->layout.get();
            desc.vertexShader = fx->vs.get();
            desc.pixelShader = fx->ps.get();
            desc.renderTargetFormat[0] = RenderFormat::R8G8B8A8_UNORM;
            desc.renderTargetBlend[0] = RenderBlendDesc::Copy();
            desc.renderTargetCount = 1;
            desc.primitiveTopology = RenderPrimitiveTopology::TRIANGLE_LIST;
            fx->pipeline = fx->device->createGraphicsPipeline(desc);
        };

        w.encode = [fx](RenderCommandList *cmd) {
            cmd->barriers(RenderBarrierStage::GRAPHICS,
                RenderTextureBarrier(fx->rt.get(), RenderTextureLayout::COLOR_WRITE));
            cmd->setFramebuffer(fx->fb.get());
            cmd->clearColor(0, RenderColor(0.0f, 0.0f, 0.0f, 1.0f));
            cmd->setGraphicsPipelineLayout(fx->layout.get());
            cmd->setPipeline(fx->pipeline.get());
            cmd->setGraphicsDescriptorSet(fx->set.get(), 0);

            RenderViewport vp(0.0f, 0.0f, float(W), float(H));
            RenderRect sc(0, 0, int32_t(W), int32_t(H));
            cmd->setViewports(vp);
            cmd->setScissors(sc);
            cmd->drawInstanced(3, 1, 0, 0);

            cmd->barriers(RenderBarrierStage::COPY,
                RenderTextureBarrier(fx->rt.get(), RenderTextureLayout::COPY_SOURCE));
            RenderTextureCopyLocation dst = RenderTextureCopyLocation::PlacedFootprint(
                fx->readback.get(), RenderFormat::R8G8B8A8_UNORM, W, H, 1, readbackRowTexels(W, 4), 0);
            RenderTextureCopyLocation src = RenderTextureCopyLocation::Subresource(fx->rt.get(), 0, 0);
            cmd->copyTextureRegion(dst, src, 0, 0, 0, nullptr);
        };

        w.verify = [fx]() -> VerifyResult {
            auto *px = static_cast<const uint8_t *>(fx->readback->map());
            if (px == nullptr) {
                return VerifyResult::fail("readback map() returned null");
            }
            const uint32_t pitch = readbackRowPitchBytes(W, 4);
            const uint8_t *p = px + (H / 2) * pitch + (W / 2) * 4;
            auto close = [](uint8_t a, uint8_t b) { return (a > b ? a - b : b - a) <= 1; };
            VerifyResult result = VerifyResult::ok();
            if (!close(p[0], ER) || !close(p[1], EG) || !close(p[2], EB) || !close(p[3], EA)) {
                char buf[128];
                snprintf(buf, sizeof(buf), "center = (%02x,%02x,%02x,%02x), expected (%02x,%02x,%02x,%02x)",
                    p[0], p[1], p[2], p[3], ER, EG, EB, EA);
                result = VerifyResult::fail(buf);
            }
            fx->readback->unmap();
            return result;
        };

        return w;
    }

}
