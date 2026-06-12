//
// plume bench
//

#pragma once

#include "workload.h"
#include "plume_render_interface.h"

#include "shader_select.h"
#ifdef __APPLE__
#include "shaders/SolidColorVert.hlsl.metal.h"
#include "shaders/SolidColorFrag.hlsl.metal.h"
#endif
#include "shaders/SolidColorVert.hlsl.spirv.h"
#include "shaders/SolidColorFrag.hlsl.spirv.h"
#ifdef _WIN32
#include "shaders/SolidColorVert.hlsl.dxil.h"
#include "shaders/SolidColorFrag.hlsl.dxil.h"
#endif

#include <cstdint>
#include <cstdio>
#include <memory>

namespace plume::bench {

    // Renders a known value into a single-channel R8_UNORM render target and reads it
    // back. Exercises non-RGBA8 format handling: 1-byte-per-pixel row pitch and channel
    // count. A backend that assumes 4 channels mishandles the readback.
    inline Workload createFormatR8UnormWorkload(RenderDevice *device, RenderCommandQueue *queue, RenderShaderFormat fmt) {
        struct Fixture {
            RenderDevice *device;
            std::unique_ptr<RenderShader> vs;
            std::unique_ptr<RenderShader> ps;
            std::unique_ptr<RenderPipelineLayout> layout;
            std::unique_ptr<RenderPipeline> pipeline;
            std::unique_ptr<RenderTexture> rt;
            std::unique_ptr<RenderFramebuffer> fb;
            std::unique_ptr<RenderBuffer> readback;
        };
        auto fx = std::make_shared<Fixture>();
        fx->device = device;

        static constexpr uint32_t W = 16, H = 16;
        static constexpr uint8_t EXPECTED = 0xBF; // 0.75 * 255 = 191.25 -> 191

        Workload w;
        w.name = "format_r8_unorm_readback";

        w.setup = [fx, fmt]() {
            fx->vs = PLUME_SELECT_SHADER(fx->device, fmt, "VSMain", SolidColorVert);
            fx->ps = PLUME_SELECT_SHADER(fx->device, fmt, "PSMain", SolidColorFrag);

            RenderTextureDesc rtDesc;
            rtDesc.dimension = RenderTextureDimension::TEXTURE_2D;
            rtDesc.width = W;
            rtDesc.height = H;
            rtDesc.depth = 1;
            rtDesc.mipLevels = 1;
            rtDesc.arraySize = 1;
            rtDesc.format = RenderFormat::R8_UNORM;
            rtDesc.flags = RenderTextureFlag::RENDER_TARGET;
            fx->rt = fx->device->createTexture(rtDesc);

            const RenderTexture *color = fx->rt.get();
            RenderFramebufferDesc fbDesc;
            fbDesc.colorAttachments = &color;
            fbDesc.colorAttachmentsCount = 1;
            fx->fb = fx->device->createFramebuffer(fbDesc);

            fx->readback = fx->device->createBuffer(
                RenderBufferDesc::ReadbackBuffer(readbackRowPitchBytes(W, 1) * H));

            RenderPipelineLayoutDesc layoutDesc;
            RenderPushConstantRange pcRange;
            pcRange.size = sizeof(float) * 4;
            pcRange.stageFlags = RenderShaderStageFlag::PIXEL;
            layoutDesc.pushConstantRanges = &pcRange;
            layoutDesc.pushConstantRangesCount = 1;
            fx->layout = fx->device->createPipelineLayout(layoutDesc);

            RenderGraphicsPipelineDesc desc;
            desc.pipelineLayout = fx->layout.get();
            desc.vertexShader = fx->vs.get();
            desc.pixelShader = fx->ps.get();
            desc.renderTargetFormat[0] = RenderFormat::R8_UNORM;
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

            const float color[4] = { 0.75f, 0.0f, 0.0f, 1.0f };
            cmd->setGraphicsPushConstants(0, color);

            RenderViewport vp(0.0f, 0.0f, float(W), float(H));
            RenderRect sc(0, 0, int32_t(W), int32_t(H));
            cmd->setViewports(vp);
            cmd->setScissors(sc);
            cmd->drawInstanced(3, 1, 0, 0);

            cmd->barriers(RenderBarrierStage::COPY,
                RenderTextureBarrier(fx->rt.get(), RenderTextureLayout::COPY_SOURCE));
            RenderTextureCopyLocation dst = RenderTextureCopyLocation::PlacedFootprint(
                fx->readback.get(), RenderFormat::R8_UNORM, W, H, 1, readbackRowTexels(W, 1), 0);
            RenderTextureCopyLocation src = RenderTextureCopyLocation::Subresource(fx->rt.get(), 0, 0);
            cmd->copyTextureRegion(dst, src, 0, 0, 0, nullptr);
        };

        w.verify = [fx]() -> VerifyResult {
            auto *px = static_cast<const uint8_t *>(fx->readback->map());
            if (px == nullptr) {
                return VerifyResult::fail("readback map() returned null");
            }
            uint8_t v = px[(H / 2) * readbackRowPitchBytes(W, 1) + (W / 2)];
            VerifyResult result = VerifyResult::ok();
            uint8_t d = v > EXPECTED ? v - EXPECTED : EXPECTED - v;
            if (d > 1) {
                char buf[96];
                snprintf(buf, sizeof(buf), "center = %02x, expected %02x", v, EXPECTED);
                result = VerifyResult::fail(buf);
            }
            fx->readback->unmap();
            return result;
        };

        return w;
    }

}
