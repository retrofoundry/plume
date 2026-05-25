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

    // Renders a full-screen blue (alpha=0.5) triangle over an opaque red background
    // using alpha blending (SRC_ALPHA / INV_SRC_ALPHA). Exercises the blend path and
    // verifies the composited output: blue*0.5 + red*0.5 = (0x80, 0x00, 0x80).
    inline Workload createBlendWorkload(RenderDevice *device, RenderCommandQueue *queue, RenderShaderFormat fmt) {
        struct Fixture {
            RenderDevice *device;
            std::unique_ptr<RenderShader> vertexShader;
            std::unique_ptr<RenderShader> pixelShader;
            std::unique_ptr<RenderPipelineLayout> layout;
            std::unique_ptr<RenderPipeline> pipeline;
            std::unique_ptr<RenderTexture> rt;
            std::unique_ptr<RenderFramebuffer> fb;
            std::unique_ptr<RenderBuffer> vertexBuffer;
            std::unique_ptr<RenderBuffer> readback;
            RenderInputSlot inputSlot;
        };
        auto fx = std::make_shared<Fixture>();
        fx->device = device;

        static constexpr uint32_t W = 16, H = 16;
        // Expected blended result: blue*0.5 + red*0.5 = (0x80, 0x00, 0x80)
        // EA = 0xFF: alpha output = src_alpha*1 + dst_alpha*(1-src_alpha) = 0.5 + 1.0*0.5 = 1.0
        static constexpr uint8_t ER = 0x80, EG = 0x00, EB = 0x80, EA = 0xFF;

        Workload w;
        w.name = "blend_alpha";

        w.setup = [fx, fmt]() {
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

            RenderPipelineLayoutDesc layoutDesc;
            layoutDesc.allowInputLayout = true;
            fx->layout = fx->device->createPipelineLayout(layoutDesc);

            fx->vertexShader = PLUME_SELECT_SHADER(fx->device, fmt, "VSMain", SceneColorVert);
            fx->pixelShader  = PLUME_SELECT_SHADER(fx->device, fmt, "PSMain", SceneColorFrag);
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
            desc.vertexShader = fx->vertexShader.get();
            desc.pixelShader = fx->pixelShader.get();
            desc.renderTargetFormat[0] = RenderFormat::R8G8B8A8_UNORM;
            desc.renderTargetBlend[0] = RenderBlendDesc::AlphaBlend();
            desc.renderTargetCount = 1;
            desc.primitiveTopology = RenderPrimitiveTopology::TRIANGLE_LIST;
            fx->pipeline = fx->device->createGraphicsPipeline(desc);

            // Blue vertices at alpha 0.5: will blend over the red clear color
            const float r = 0.0f, g = 0.0f, b = 1.0f, a = 0.5f;
            const float verts[3 * 7] = {
                -1.0f, -1.0f, 0.0f,  r, g, b, a,
                 3.0f, -1.0f, 0.0f,  r, g, b, a,
                -1.0f,  3.0f, 0.0f,  r, g, b, a,
            };
            fx->vertexBuffer = fx->device->createBuffer(
                RenderBufferDesc::VertexBuffer(sizeof(verts), RenderHeapType::UPLOAD));
            void *mapped = fx->vertexBuffer->map();
            std::memcpy(mapped, verts, sizeof(verts));
            fx->vertexBuffer->unmap();
        };

        w.encode = [fx](RenderCommandList *cmd) {
            cmd->barriers(RenderBarrierStage::GRAPHICS,
                RenderTextureBarrier(fx->rt.get(), RenderTextureLayout::COLOR_WRITE));
            cmd->setFramebuffer(fx->fb.get());
            cmd->clearColor(0, RenderColor(1.0f, 0.0f, 0.0f, 1.0f));
            cmd->setGraphicsPipelineLayout(fx->layout.get());
            cmd->setPipeline(fx->pipeline.get());

            RenderViewport vp(0.0f, 0.0f, float(16), float(16));
            RenderRect sc(0, 0, 16, 16);
            cmd->setViewports(vp);
            cmd->setScissors(sc);

            RenderVertexBufferView vbView(fx->vertexBuffer->at(0), sizeof(float) * 7 * 3);
            cmd->setVertexBuffers(0, &vbView, 1, &fx->inputSlot);
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
            auto close = [](uint8_t x, uint8_t y) { return (x > y ? x - y : y - x) <= 2; };
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
