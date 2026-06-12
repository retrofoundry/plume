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

    // Renders a solid color into a 4x MSAA render target, resolves it to a single-sample
    // texture, reads that back, and verifies the color. Exercises the full multisample
    // path: MSAA target allocation, rendering to it, resolveTexture, and resolved readback.
    inline Workload createMsaaResolveWorkload(RenderDevice *device, RenderCommandQueue *queue, RenderShaderFormat fmt) {
        struct Fixture {
            RenderDevice *device;
            std::unique_ptr<RenderShader> vs;
            std::unique_ptr<RenderShader> ps;
            std::unique_ptr<RenderPipelineLayout> layout;
            std::unique_ptr<RenderPipeline> pipeline;
            std::unique_ptr<RenderTexture> msaa;
            std::unique_ptr<RenderTexture> resolved;
            std::unique_ptr<RenderFramebuffer> fb;
            std::unique_ptr<RenderBuffer> readback;
            uint32_t sampleCount = 1;
        };
        auto fx = std::make_shared<Fixture>();
        fx->device = device;

        static constexpr uint32_t W = 16, H = 16;
        // Push color (0.25, 0.5, 0.75, 1.0) -> bytes (0x40, 0x80, 0xBF, 0xFF).
        static constexpr uint8_t ER = 0x40, EG = 0x80, EB = 0xBF, EA = 0xFF;

        Workload w;
        w.name = "msaa_resolve_readback";

        w.setup = [fx, fmt]() {
            fx->vs = PLUME_SELECT_SHADER(fx->device, fmt, "VSMain", SolidColorVert);
            fx->ps = PLUME_SELECT_SHADER(fx->device, fmt, "PSMain", SolidColorFrag);

            RenderSampleCounts supported = fx->device->getSampleCountsSupported(RenderFormat::R8G8B8A8_UNORM);
            fx->sampleCount = (supported & RenderSampleCount::COUNT_4) ? RenderSampleCount::COUNT_4
                            : (supported & RenderSampleCount::COUNT_2) ? RenderSampleCount::COUNT_2
                            : RenderSampleCount::COUNT_1;

            RenderTextureDesc msaaDesc;
            msaaDesc.dimension = RenderTextureDimension::TEXTURE_2D;
            msaaDesc.width = W;
            msaaDesc.height = H;
            msaaDesc.depth = 1;
            msaaDesc.mipLevels = 1;
            msaaDesc.arraySize = 1;
            msaaDesc.format = RenderFormat::R8G8B8A8_UNORM;
            msaaDesc.flags = RenderTextureFlag::RENDER_TARGET;
            msaaDesc.multisampling = RenderMultisampling(fx->sampleCount);
            fx->msaa = fx->device->createTexture(msaaDesc);

            RenderTextureDesc resolvedDesc;
            resolvedDesc.dimension = RenderTextureDimension::TEXTURE_2D;
            resolvedDesc.width = W;
            resolvedDesc.height = H;
            resolvedDesc.depth = 1;
            resolvedDesc.mipLevels = 1;
            resolvedDesc.arraySize = 1;
            resolvedDesc.format = RenderFormat::R8G8B8A8_UNORM;
            resolvedDesc.flags = RenderTextureFlag::RENDER_TARGET;
            fx->resolved = fx->device->createTexture(resolvedDesc);

            const RenderTexture *color = fx->msaa.get();
            RenderFramebufferDesc fbDesc;
            fbDesc.colorAttachments = &color;
            fbDesc.colorAttachmentsCount = 1;
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

            RenderGraphicsPipelineDesc desc;
            desc.pipelineLayout = fx->layout.get();
            desc.vertexShader = fx->vs.get();
            desc.pixelShader = fx->ps.get();
            desc.renderTargetFormat[0] = RenderFormat::R8G8B8A8_UNORM;
            desc.renderTargetBlend[0] = RenderBlendDesc::Copy();
            desc.renderTargetCount = 1;
            desc.multisampling = RenderMultisampling(fx->sampleCount);
            desc.primitiveTopology = RenderPrimitiveTopology::TRIANGLE_LIST;
            fx->pipeline = fx->device->createGraphicsPipeline(desc);
        };

        w.encode = [fx](RenderCommandList *cmd) {
            cmd->barriers(RenderBarrierStage::GRAPHICS,
                RenderTextureBarrier(fx->msaa.get(), RenderTextureLayout::COLOR_WRITE));
            cmd->setFramebuffer(fx->fb.get());
            cmd->clearColor(0, RenderColor(0.0f, 0.0f, 0.0f, 1.0f));
            cmd->setGraphicsPipelineLayout(fx->layout.get());
            cmd->setPipeline(fx->pipeline.get());

            const float color[4] = { 0.25f, 0.5f, 0.75f, 1.0f };
            cmd->setGraphicsPushConstants(0, color);

            RenderViewport vp(0.0f, 0.0f, float(W), float(H));
            RenderRect sc(0, 0, int32_t(W), int32_t(H));
            cmd->setViewports(vp);
            cmd->setScissors(sc);
            cmd->drawInstanced(3, 1, 0, 0);

            RenderTextureBarrier toResolve[2] = {
                RenderTextureBarrier(fx->msaa.get(), RenderTextureLayout::RESOLVE_SOURCE),
                RenderTextureBarrier(fx->resolved.get(), RenderTextureLayout::RESOLVE_DEST),
            };
            cmd->barriers(RenderBarrierStage::COPY, toResolve, 2);
            cmd->resolveTexture(fx->resolved.get(), fx->msaa.get());

            cmd->barriers(RenderBarrierStage::COPY,
                RenderTextureBarrier(fx->resolved.get(), RenderTextureLayout::COPY_SOURCE));
            RenderTextureCopyLocation dst = RenderTextureCopyLocation::PlacedFootprint(
                fx->readback.get(), RenderFormat::R8G8B8A8_UNORM, W, H, 1, readbackRowTexels(W, 4), 0);
            RenderTextureCopyLocation src = RenderTextureCopyLocation::Subresource(fx->resolved.get(), 0, 0);
            cmd->copyTextureRegion(dst, src, 0, 0, 0, nullptr);
        };

        w.verify = [fx]() -> VerifyResult {
            auto *px = static_cast<const uint8_t *>(fx->readback->map());
            if (px == nullptr) {
                return VerifyResult::fail("readback map() returned null");
            }
            const uint32_t pitch = readbackRowPitchBytes(W, 4);
            const uint8_t *p = px + (H / 2) * pitch + (W / 2) * 4;
            auto close = [](uint8_t a, uint8_t b) { return (a > b ? a - b : b - a) <= 2; };
            VerifyResult result = VerifyResult::ok();
            if (fx->sampleCount < RenderSampleCount::COUNT_2) {
                result = VerifyResult::fail("backend reports no MSAA support for R8G8B8A8_UNORM");
            } else if (!close(p[0], ER) || !close(p[1], EG) || !close(p[2], EB) || !close(p[3], EA)) {
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
