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

    // Two fullscreen passes increment stencil (INCREMENT_AND_CLAMP) from 0 to 2 everywhere,
    // then a third fullscreen pass paints white only where stencil == 2 (EQUAL). Verifying the
    // center is white proves the read-modify-write increment stencil op ran twice — a feature
    // distinct from the REPLACE op the mask workload covers. If increment is broken (stencil
    // stays 0 or 1), the EQUAL-2 test fails everywhere and the center stays black.
    inline Workload createStencilIncrementWorkload(RenderDevice *device, RenderCommandQueue *queue, RenderShaderFormat fmt) {
        struct Fixture {
            RenderDevice *device;
            std::unique_ptr<RenderShader> vs;
            std::unique_ptr<RenderShader> ps;
            std::unique_ptr<RenderPipelineLayout> layout;
            std::unique_ptr<RenderPipeline> incrPipeline;
            std::unique_ptr<RenderPipeline> testPipeline;
            std::unique_ptr<RenderTexture> rt;
            std::unique_ptr<RenderTexture> depthStencil;
            std::unique_ptr<RenderFramebuffer> fb;
            std::unique_ptr<RenderBuffer> readback;
        };
        auto fx = std::make_shared<Fixture>();
        fx->device = device;

        static constexpr uint32_t W = 16, H = 16;
        static constexpr uint8_t EXPECTED = 0xFF; // white where stencil reached 2

        Workload w;
        w.name = "stencil_increment_readback";

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
            rtDesc.format = RenderFormat::R8G8B8A8_UNORM;
            rtDesc.flags = RenderTextureFlag::RENDER_TARGET;
            fx->rt = fx->device->createTexture(rtDesc);

            RenderTextureDesc dsDesc;
            dsDesc.dimension = RenderTextureDimension::TEXTURE_2D;
            dsDesc.width = W;
            dsDesc.height = H;
            dsDesc.depth = 1;
            dsDesc.mipLevels = 1;
            dsDesc.arraySize = 1;
            dsDesc.format = RenderFormat::D32_FLOAT_S8_UINT;
            dsDesc.flags = RenderTextureFlag::DEPTH_TARGET;
            fx->depthStencil = fx->device->createTexture(dsDesc);

            const RenderTexture *color = fx->rt.get();
            RenderFramebufferDesc fbDesc;
            fbDesc.colorAttachments = &color;
            fbDesc.colorAttachmentsCount = 1;
            fbDesc.depthAttachment = fx->depthStencil.get();
            fx->fb = fx->device->createFramebuffer(fbDesc);

            fx->readback = fx->device->createBuffer(
                RenderBufferDesc::ReadbackBuffer(readbackRowPitchBytes(W, 4) * H));

            RenderPipelineLayoutDesc layoutDesc;
            RenderPushConstantRange pcRange;
            pcRange.size = sizeof(float) * 4;
            pcRange.stageFlags = RenderShaderStageFlag::PIXEL;
            layoutDesc.pushConstantRanges = &pcRange;
            layoutDesc.pushConstantRangesCount = 1;
            fx->layout = fx->device->createPipelineLayout(layoutDesc);

            RenderGraphicsPipelineDesc base;
            base.pipelineLayout = fx->layout.get();
            base.vertexShader = fx->vs.get();
            base.pixelShader = fx->ps.get();
            base.renderTargetFormat[0] = RenderFormat::R8G8B8A8_UNORM;
            base.renderTargetBlend[0] = RenderBlendDesc::Copy();
            base.renderTargetCount = 1;
            base.primitiveTopology = RenderPrimitiveTopology::TRIANGLE_LIST;
            base.depthTargetFormat = RenderFormat::D32_FLOAT_S8_UINT;
            base.depthEnabled = false;
            base.depthWriteEnabled = false;
            base.stencilEnabled = true;
            base.stencilReadMask = 0xFF;

            // Increment pass: always pass, increment stencil.
            RenderGraphicsPipelineDesc incrDesc = base;
            incrDesc.stencilWriteMask = 0xFF;
            incrDesc.stencilReference = 0;
            incrDesc.stencilFrontFace.compareFunction = RenderComparisonFunction::ALWAYS;
            incrDesc.stencilFrontFace.passOp = RenderStencilOp::INCREMENT_AND_CLAMP;
            incrDesc.stencilFrontFace.failOp = RenderStencilOp::KEEP;
            incrDesc.stencilFrontFace.depthFailOp = RenderStencilOp::KEEP;
            incrDesc.stencilBackFace = incrDesc.stencilFrontFace;
            fx->incrPipeline = fx->device->createGraphicsPipeline(incrDesc);

            // Test pass: pass only where stencil == 2.
            RenderGraphicsPipelineDesc testDesc = base;
            testDesc.stencilWriteMask = 0x00;
            testDesc.stencilReference = 2;
            testDesc.stencilFrontFace.compareFunction = RenderComparisonFunction::EQUAL;
            testDesc.stencilFrontFace.passOp = RenderStencilOp::KEEP;
            testDesc.stencilFrontFace.failOp = RenderStencilOp::KEEP;
            testDesc.stencilFrontFace.depthFailOp = RenderStencilOp::KEEP;
            testDesc.stencilBackFace = testDesc.stencilFrontFace;
            fx->testPipeline = fx->device->createGraphicsPipeline(testDesc);
        };

        w.encode = [fx](RenderCommandList *cmd) {
            cmd->barriers(RenderBarrierStage::GRAPHICS,
                RenderTextureBarrier(fx->rt.get(), RenderTextureLayout::COLOR_WRITE));
            cmd->setFramebuffer(fx->fb.get());
            cmd->clearColor(0, RenderColor(0.0f, 0.0f, 0.0f, 1.0f));
            cmd->clearDepthStencil(true, true, 1.0f, 0);

            RenderViewport vp(0.0f, 0.0f, float(W), float(H));
            RenderRect sc(0, 0, int32_t(W), int32_t(H));
            cmd->setViewports(vp);
            cmd->setScissors(sc);

            cmd->setGraphicsPipelineLayout(fx->layout.get());

            const float black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
            const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

            cmd->setPipeline(fx->incrPipeline.get());
            cmd->setGraphicsPushConstants(0, black);
            cmd->drawInstanced(3, 1, 0, 0); // stencil -> 1
            cmd->drawInstanced(3, 1, 0, 0); // stencil -> 2

            cmd->setPipeline(fx->testPipeline.get());
            cmd->setGraphicsPushConstants(0, white);
            cmd->drawInstanced(3, 1, 0, 0); // white where stencil == 2

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
            VerifyResult result = VerifyResult::ok();
            uint8_t d = p[0] > EXPECTED ? p[0] - EXPECTED : EXPECTED - p[0];
            if (d > 1) {
                char buf[96];
                snprintf(buf, sizeof(buf), "center R = %02x, expected %02x (stencil should equal 2)", p[0], EXPECTED);
                result = VerifyResult::fail(buf);
            }
            fx->readback->unmap();
            return result;
        };

        return w;
    }

}
