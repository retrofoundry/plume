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

    // Pass 1 draws a half-covering triangle that writes stencil=1 (REPLACE/ALWAYS) without
    // changing color. Pass 2 draws a fullscreen white triangle that only passes the stencil
    // test where stencil==1 (EQUAL). The result is partitioned: white where pass 1 marked,
    // black elsewhere. Verifying both white AND black pixels exist proves the stencil write
    // and the stencil test both work (if either failed, the image would be uniformly one color).
    inline Workload createStencilMaskWorkload(RenderDevice *device, RenderCommandQueue *queue, RenderShaderFormat fmt) {
        struct Fixture {
            RenderDevice *device;
            std::unique_ptr<RenderShader> vs;
            std::unique_ptr<RenderShader> ps;
            std::unique_ptr<RenderPipelineLayout> layout;
            std::unique_ptr<RenderPipeline> writePipeline;
            std::unique_ptr<RenderPipeline> testPipeline;
            std::unique_ptr<RenderTexture> rt;
            std::unique_ptr<RenderTexture> depthStencil;
            std::unique_ptr<RenderFramebuffer> fb;
            std::unique_ptr<RenderBuffer> vbHalf;
            std::unique_ptr<RenderBuffer> vbFull;
            std::unique_ptr<RenderBuffer> readback;
            RenderInputSlot inputSlot;
        };
        auto fx = std::make_shared<Fixture>();
        fx->device = device;

        static constexpr uint32_t W = 16, H = 16;

        Workload w;
        w.name = "stencil_mask_readback";

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
            layoutDesc.allowInputLayout = true;
            fx->layout = fx->device->createPipelineLayout(layoutDesc);

            fx->inputSlot = RenderInputSlot(0, sizeof(float) * 7);
            RenderInputElement inputElements[] = {
                RenderInputElement("POSITION", 0, 0, RenderFormat::R32G32B32_FLOAT, 0, 0),
                RenderInputElement("COLOR", 0, 1, RenderFormat::R32G32B32A32_FLOAT, 0, sizeof(float) * 3)
            };

            RenderGraphicsPipelineDesc base;
            base.inputSlots = &fx->inputSlot;
            base.inputSlotsCount = 1;
            base.inputElements = inputElements;
            base.inputElementsCount = 2;
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

            // Pass 1: write stencil = 1 everywhere the triangle covers.
            RenderGraphicsPipelineDesc writeDesc = base;
            writeDesc.stencilWriteMask = 0xFF;
            writeDesc.stencilReference = 1;
            writeDesc.stencilFrontFace.compareFunction = RenderComparisonFunction::ALWAYS;
            writeDesc.stencilFrontFace.passOp = RenderStencilOp::REPLACE;
            writeDesc.stencilFrontFace.failOp = RenderStencilOp::KEEP;
            writeDesc.stencilFrontFace.depthFailOp = RenderStencilOp::KEEP;
            writeDesc.stencilBackFace = writeDesc.stencilFrontFace;
            fx->writePipeline = fx->device->createGraphicsPipeline(writeDesc);

            // Pass 2: keep stencil, draw only where stencil == 1.
            RenderGraphicsPipelineDesc testDesc = base;
            testDesc.stencilWriteMask = 0x00;
            testDesc.stencilReference = 1;
            testDesc.stencilFrontFace.compareFunction = RenderComparisonFunction::EQUAL;
            testDesc.stencilFrontFace.passOp = RenderStencilOp::KEEP;
            testDesc.stencilFrontFace.failOp = RenderStencilOp::KEEP;
            testDesc.stencilFrontFace.depthFailOp = RenderStencilOp::KEEP;
            testDesc.stencilBackFace = testDesc.stencilFrontFace;
            fx->testPipeline = fx->device->createGraphicsPipeline(testDesc);

            fx->vbHalf = fx->device->createBuffer(RenderBufferDesc::VertexBuffer(sizeof(float) * 7 * 3, RenderHeapType::UPLOAD));
            fx->vbFull = fx->device->createBuffer(RenderBufferDesc::VertexBuffer(sizeof(float) * 7 * 3, RenderHeapType::UPLOAD));
            const float halfTri[6] = { -1.0f, -1.0f,  1.0f, -1.0f,  -1.0f, 1.0f }; // covers ~half
            const float fullTri[6] = { -1.0f, -1.0f,  3.0f, -1.0f,  -1.0f, 3.0f }; // covers all
            fillTri(fx->vbHalf.get(), halfTri, 0.0f, 0.0f, 0.0f); // pass 1 writes black (== clear)
            fillTri(fx->vbFull.get(), fullTri, 1.0f, 1.0f, 1.0f); // pass 2 white
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
            cmd->setPipeline(fx->writePipeline.get());
            RenderVertexBufferView halfView(fx->vbHalf->at(0), sizeof(float) * 7 * 3);
            cmd->setVertexBuffers(0, &halfView, 1, &fx->inputSlot);
            cmd->drawInstanced(3, 1, 0, 0);

            cmd->setPipeline(fx->testPipeline.get());
            RenderVertexBufferView fullView(fx->vbFull->at(0), sizeof(float) * 7 * 3);
            cmd->setVertexBuffers(0, &fullView, 1, &fx->inputSlot);
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
            uint32_t whiteCount = 0, blackCount = 0;
            const uint32_t pitch = readbackRowPitchBytes(W, 4);
            for (uint32_t y = 0; y < H; y++) {
                const uint8_t *row = px + y * pitch;
                for (uint32_t x = 0; x < W; x++) {
                    uint8_t r = row[x * 4];
                    if (r >= 0xF0) whiteCount++;
                    else if (r <= 0x0F) blackCount++;
                }
            }
            fx->readback->unmap();
            if (whiteCount == 0 || blackCount == 0) {
                char buf[128];
                snprintf(buf, sizeof(buf), "white=%u black=%u (need both; uniform image means stencil write or test failed)",
                    whiteCount, blackCount);
                return VerifyResult::fail(buf);
            }
            return VerifyResult::ok();
        };

        return w;
    }

}
