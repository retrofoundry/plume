//
// plume bench
//

#pragma once

#include "workload.h"
#include "plume_render_interface.h"

#include "shader_select.h"
#ifdef __APPLE__
#include "shaders/SceneColorVert.hlsl.metal.h"
#include "shaders/SceneColorFrag.hlsl.metal.h"
#endif
#include "shaders/SceneColorVert.hlsl.spirv.h"
#include "shaders/SceneColorFrag.hlsl.spirv.h"
#ifdef _WIN32
#include "shaders/SceneColorVert.hlsl.dxil.h"
#include "shaders/SceneColorFrag.hlsl.dxil.h"
#endif

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>

namespace plume::bench {

    // Renders a half-covering white triangle into a 4x MSAA target over a black clear,
    // resolves, and scans the result for an antialiased edge pixel — a partial-coverage
    // value strictly between black and white that only multisampling can produce. Proves
    // MSAA is genuinely active (a single-sample target yields only fully black/white pixels).
    inline Workload createMsaaCoverageWorkload(RenderDevice *device, RenderCommandQueue *queue, RenderShaderFormat fmt) {
        struct Fixture {
            RenderDevice *device;
            std::unique_ptr<RenderShader> vs;
            std::unique_ptr<RenderShader> ps;
            std::unique_ptr<RenderPipelineLayout> layout;
            std::unique_ptr<RenderPipeline> pipeline;
            std::unique_ptr<RenderTexture> msaa;
            std::unique_ptr<RenderTexture> resolved;
            std::unique_ptr<RenderFramebuffer> fb;
            std::unique_ptr<RenderBuffer> vb;
            std::unique_ptr<RenderBuffer> readback;
            RenderInputSlot inputSlot;
            uint32_t sampleCount = 1;
        };
        auto fx = std::make_shared<Fixture>();
        fx->device = device;

        static constexpr uint32_t W = 16, H = 16;

        Workload w;
        w.name = "msaa_coverage_readback";

        w.setup = [fx, fmt]() {
            fx->vs = PLUME_SELECT_SHADER(fx->device, fmt, "VSMain", SceneColorVert);
            fx->ps = PLUME_SELECT_SHADER(fx->device, fmt, "PSMain", SceneColorFrag);

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
            layoutDesc.allowInputLayout = true;
            fx->layout = fx->device->createPipelineLayout(layoutDesc);

            fx->inputSlot = RenderInputSlot(0, sizeof(float) * 7);
            RenderInputElement inputElements[] = {
                RenderInputElement("POSITION", 0, 0, RenderFormat::R32G32B32_FLOAT, 0, 0),
                RenderInputElement("COLOR", 0, 1, RenderFormat::R32G32B32A32_FLOAT, 0, sizeof(float) * 3)
            };

            RenderGraphicsPipelineDesc desc;
            desc.inputSlots = &fx->inputSlot;
            desc.inputSlotsCount = 1;
            desc.inputElements = inputElements;
            desc.inputElementsCount = 2;
            desc.pipelineLayout = fx->layout.get();
            desc.vertexShader = fx->vs.get();
            desc.pixelShader = fx->ps.get();
            desc.renderTargetFormat[0] = RenderFormat::R8G8B8A8_UNORM;
            desc.renderTargetBlend[0] = RenderBlendDesc::Copy();
            desc.renderTargetCount = 1;
            desc.multisampling = RenderMultisampling(fx->sampleCount);
            desc.primitiveTopology = RenderPrimitiveTopology::TRIANGLE_LIST;
            fx->pipeline = fx->device->createGraphicsPipeline(desc);

            // Half-covering white triangle: a diagonal edge across the framebuffer.
            const float verts[3 * 7] = {
                -1.0f, -1.0f, 0.0f,  1.0f, 1.0f, 1.0f, 1.0f,
                 1.0f, -1.0f, 0.0f,  1.0f, 1.0f, 1.0f, 1.0f,
                -1.0f,  1.0f, 0.0f,  1.0f, 1.0f, 1.0f, 1.0f,
            };
            fx->vb = fx->device->createBuffer(
                RenderBufferDesc::VertexBuffer(sizeof(verts), RenderHeapType::UPLOAD));
            std::memcpy(fx->vb->map(), verts, sizeof(verts));
            fx->vb->unmap();
        };

        w.encode = [fx](RenderCommandList *cmd) {
            cmd->barriers(RenderBarrierStage::GRAPHICS,
                RenderTextureBarrier(fx->msaa.get(), RenderTextureLayout::COLOR_WRITE));
            cmd->setFramebuffer(fx->fb.get());
            cmd->clearColor(0, RenderColor(0.0f, 0.0f, 0.0f, 1.0f));
            cmd->setGraphicsPipelineLayout(fx->layout.get());
            cmd->setPipeline(fx->pipeline.get());

            RenderViewport vp(0.0f, 0.0f, float(W), float(H));
            RenderRect sc(0, 0, int32_t(W), int32_t(H));
            cmd->setViewports(vp);
            cmd->setScissors(sc);

            RenderVertexBufferView vbView(fx->vb->at(0), sizeof(float) * 7 * 3);
            cmd->setVertexBuffers(0, &vbView, 1, &fx->inputSlot);
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
            bool hasWhite = false, hasBlack = false, hasPartial = false;
            const uint32_t pitch = readbackRowPitchBytes(W, 4);
            for (uint32_t y = 0; y < H; y++) {
                const uint8_t *row = px + y * pitch;
                for (uint32_t x = 0; x < W; x++) {
                    uint8_t r = row[x * 4];
                    if (r >= 0xF0) hasWhite = true;
                    else if (r <= 0x0F) hasBlack = true;
                    if (r >= 0x20 && r <= 0xE0) hasPartial = true;
                }
            }
            fx->readback->unmap();
            if (fx->sampleCount < RenderSampleCount::COUNT_2) {
                return VerifyResult::fail("backend reports no MSAA support for R8G8B8A8_UNORM");
            }
            if (!hasWhite || !hasBlack || !hasPartial) {
                char buf[128];
                snprintf(buf, sizeof(buf), "white=%d black=%d partial=%d (need all; partial proves AA edge)",
                    hasWhite, hasBlack, hasPartial);
                return VerifyResult::fail(buf);
            }
            return VerifyResult::ok();
        };

        return w;
    }

}
