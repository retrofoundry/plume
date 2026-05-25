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

    // Draws a front-facing green triangle then a back-facing red triangle over the same area,
    // with cullMode=BACK. The red (back-facing) triangle is culled, so the center stays green.
    // Verifies the center is green. With culling disabled the red triangle (drawn last) would
    // win, so the test fails if back-face culling is broken.
    inline Workload createCullModeWorkload(RenderDevice *device, RenderCommandQueue *queue, RenderShaderFormat fmt) {
        struct Fixture {
            RenderDevice *device;
            std::unique_ptr<RenderShader> vs;
            std::unique_ptr<RenderShader> ps;
            std::unique_ptr<RenderPipelineLayout> layout;
            std::unique_ptr<RenderPipeline> pipeline;
            std::unique_ptr<RenderTexture> rt;
            std::unique_ptr<RenderFramebuffer> fb;
            std::unique_ptr<RenderBuffer> vbGreen;
            std::unique_ptr<RenderBuffer> vbRed;
            std::unique_ptr<RenderBuffer> readback;
            RenderInputSlot inputSlot;
        };
        auto fx = std::make_shared<Fixture>();
        fx->device = device;

        static constexpr uint32_t W = 16, H = 16;
        static constexpr uint8_t ER = 0x00, EG = 0xFF, EB = 0x00, EA = 0xFF; // green survives

        Workload w;
        w.name = "cull_mode_readback";

        // Fill a fullscreen-covering triangle with the given vertex winding order and color.
        auto fillTri = [](RenderBuffer *vb, const float *ndc6, float r, float g, float b) {
            const float verts[3 * 7] = {
                ndc6[0], ndc6[1], 0.0f,  r, g, b, 1.0f,
                ndc6[2], ndc6[3], 0.0f,  r, g, b, 1.0f,
                ndc6[4], ndc6[5], 0.0f,  r, g, b, 1.0f,
            };
            std::memcpy(vb->map(), verts, sizeof(verts));
            vb->unmap();
        };

        w.setup = [fx, fillTri, fmt]() {
            fx->vs = PLUME_SELECT_SHADER(fx->device, fmt, "VSMain", SceneColorVert);
            fx->ps = PLUME_SELECT_SHADER(fx->device, fmt, "PSMain", SceneColorFrag);

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
            desc.primitiveTopology = RenderPrimitiveTopology::TRIANGLE_LIST;
            desc.cullMode = RenderCullMode::BACK;
            desc.frontFace = RenderFrontFace::CLOCKWISE;
            fx->pipeline = fx->device->createGraphicsPipeline(desc);

            fx->vbGreen = fx->device->createBuffer(RenderBufferDesc::VertexBuffer(sizeof(float) * 7 * 3, RenderHeapType::UPLOAD));
            fx->vbRed = fx->device->createBuffer(RenderBufferDesc::VertexBuffer(sizeof(float) * 7 * 3, RenderHeapType::UPLOAD));
            // Same three corners, opposite winding. Green is clockwise (front-facing under
            // frontFace=CLOCKWISE) and survives; red is counter-clockwise (back-facing) and is culled.
            const float greenCW[6] = { -1.0f, -1.0f,  -1.0f, 3.0f,  3.0f, -1.0f };
            const float redCCW[6]  = { -1.0f, -1.0f,  3.0f, -1.0f,  -1.0f, 3.0f };
            fillTri(fx->vbGreen.get(), greenCW, 0.0f, 1.0f, 0.0f);
            fillTri(fx->vbRed.get(), redCCW, 1.0f, 0.0f, 0.0f);
        };

        w.encode = [fx](RenderCommandList *cmd) {
            cmd->barriers(RenderBarrierStage::GRAPHICS,
                RenderTextureBarrier(fx->rt.get(), RenderTextureLayout::COLOR_WRITE));
            cmd->setFramebuffer(fx->fb.get());
            cmd->clearColor(0, RenderColor(0.0f, 0.0f, 0.0f, 1.0f));
            cmd->setGraphicsPipelineLayout(fx->layout.get());
            cmd->setPipeline(fx->pipeline.get());

            RenderViewport vp(0.0f, 0.0f, float(W), float(H));
            RenderRect sc(0, 0, int32_t(W), int32_t(H));
            cmd->setViewports(vp);
            cmd->setScissors(sc);

            RenderVertexBufferView greenView(fx->vbGreen->at(0), sizeof(float) * 7 * 3);
            cmd->setVertexBuffers(0, &greenView, 1, &fx->inputSlot);
            cmd->drawInstanced(3, 1, 0, 0); // front-facing, drawn

            RenderVertexBufferView redView(fx->vbRed->at(0), sizeof(float) * 7 * 3);
            cmd->setVertexBuffers(0, &redView, 1, &fx->inputSlot);
            cmd->drawInstanced(3, 1, 0, 0); // back-facing, culled

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
                snprintf(buf, sizeof(buf), "center = (%02x,%02x,%02x,%02x), expected green (back-face red should be culled)",
                    p[0], p[1], p[2], p[3]);
                result = VerifyResult::fail(buf);
            }
            fx->readback->unmap();
            return result;
        };

        return w;
    }

}
