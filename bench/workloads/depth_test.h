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

    // Draws a near triangle then a far triangle with depth test LESS; the far one must
    // be rejected, so the center reads the near color. Verifies depth rejection works.
    inline Workload createDepthTestWorkload(RenderDevice *device, RenderCommandQueue *queue, RenderShaderFormat fmt) {
        struct Fixture {
            RenderDevice *device;
            std::unique_ptr<RenderShader> vs;
            std::unique_ptr<RenderShader> ps;
            std::unique_ptr<RenderPipelineLayout> layout;
            std::unique_ptr<RenderPipeline> pipeline;
            std::unique_ptr<RenderTexture> rt;
            std::unique_ptr<RenderTexture> depth;
            std::unique_ptr<RenderFramebuffer> fb;
            std::unique_ptr<RenderBuffer> vbNear;
            std::unique_ptr<RenderBuffer> vbFar;
            std::unique_ptr<RenderBuffer> readback;
            RenderInputSlot inputSlot;
        };
        auto fx = std::make_shared<Fixture>();
        fx->device = device;

        static constexpr uint32_t W = 16, H = 16;
        // near = blue (0,0,1), far = red (1,0,0); expect near to win.
        static constexpr uint8_t ER = 0x00, EG = 0x00, EB = 0xFF, EA = 0xFF;

        Workload w;
        w.name = "depth_test_occlusion";

        auto fillTri = [](RenderBuffer *vb, float z, float r, float g, float b) {
            const float verts[3 * 7] = {
                -1.0f, -1.0f, z,  r, g, b, 1.0f,
                 3.0f, -1.0f, z,  r, g, b, 1.0f,
                -1.0f,  3.0f, z,  r, g, b, 1.0f,
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

            RenderTextureDesc depthDesc;
            depthDesc.dimension = RenderTextureDimension::TEXTURE_2D;
            depthDesc.width = W;
            depthDesc.height = H;
            depthDesc.depth = 1;
            depthDesc.mipLevels = 1;
            depthDesc.arraySize = 1;
            depthDesc.format = RenderFormat::D32_FLOAT;
            depthDesc.flags = RenderTextureFlag::DEPTH_TARGET;
            fx->depth = fx->device->createTexture(depthDesc);

            const RenderTexture *color = fx->rt.get();
            RenderFramebufferDesc fbDesc;
            fbDesc.colorAttachments = &color;
            fbDesc.colorAttachmentsCount = 1;
            fbDesc.depthAttachment = fx->depth.get();
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
            desc.depthEnabled = true;
            desc.depthWriteEnabled = true;
            desc.depthFunction = RenderComparisonFunction::LESS;
            desc.depthTargetFormat = RenderFormat::D32_FLOAT;
            fx->pipeline = fx->device->createGraphicsPipeline(desc);

            fx->vbNear = fx->device->createBuffer(RenderBufferDesc::VertexBuffer(sizeof(float) * 7 * 3, RenderHeapType::UPLOAD));
            fx->vbFar = fx->device->createBuffer(RenderBufferDesc::VertexBuffer(sizeof(float) * 7 * 3, RenderHeapType::UPLOAD));
            fillTri(fx->vbNear.get(), 0.25f, 0.0f, 0.0f, 1.0f); // near = blue
            fillTri(fx->vbFar.get(), 0.75f, 1.0f, 0.0f, 0.0f);  // far = red
        };

        w.encode = [fx](RenderCommandList *cmd) {
            cmd->barriers(RenderBarrierStage::GRAPHICS,
                RenderTextureBarrier(fx->rt.get(), RenderTextureLayout::COLOR_WRITE));
            cmd->setFramebuffer(fx->fb.get());
            cmd->clearColor(0, RenderColor(0.0f, 0.0f, 0.0f, 1.0f));
            cmd->clearDepth(true, 1.0f);
            cmd->setGraphicsPipelineLayout(fx->layout.get());
            cmd->setPipeline(fx->pipeline.get());
            RenderViewport vp(0.0f, 0.0f, float(W), float(H));
            RenderRect sc(0, 0, int32_t(W), int32_t(H));
            cmd->setViewports(vp);
            cmd->setScissors(sc);

            RenderVertexBufferView nearView(fx->vbNear->at(0), sizeof(float) * 7 * 3);
            cmd->setVertexBuffers(0, &nearView, 1, &fx->inputSlot);
            cmd->drawInstanced(3, 1, 0, 0);

            RenderVertexBufferView farView(fx->vbFar->at(0), sizeof(float) * 7 * 3);
            cmd->setVertexBuffers(0, &farView, 1, &fx->inputSlot);
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
                snprintf(buf, sizeof(buf), "center = (%02x,%02x,%02x,%02x), expected near color (%02x,%02x,%02x,%02x)",
                    p[0], p[1], p[2], p[3], ER, EG, EB, EA);
                result = VerifyResult::fail(buf);
            }
            fx->readback->unmap();
            return result;
        };

        return w;
    }

}
