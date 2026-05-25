//
// plume bench
//

#pragma once

#include "workload.h"
#include "plume_render_interface.h"
#include "plume_render_interface_builders.h"

#include "shader_select.h"
#ifdef __APPLE__
#include "shaders/SampleLevelVert.hlsl.metal.h"
#include "shaders/SampleLevelFrag.hlsl.metal.h"
#endif
#include "shaders/SampleLevelVert.hlsl.spirv.h"
#include "shaders/SampleLevelFrag.hlsl.spirv.h"
#ifdef _WIN32
#include "shaders/SampleLevelVert.hlsl.dxil.h"
#include "shaders/SampleLevelFrag.hlsl.dxil.h"
#endif

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>

namespace plume::bench {

    // Uploads distinct data to a texture's two mip levels (mip 0 = black, mip 1 = white)
    // and samples mip 1 explicitly via SampleLevel. Verifies the result is mip 1's value,
    // proving mip-level selection + per-subresource upload. A broken selection samples
    // mip 0 (black) instead.
    inline Workload createTextureMipWorkload(RenderDevice *device, RenderCommandQueue *queue, RenderShaderFormat fmt) {
        struct Fixture {
            RenderDevice *device;
            std::unique_ptr<RenderShader> vs;
            std::unique_ptr<RenderShader> ps;
            std::unique_ptr<RenderPipelineLayout> layout;
            std::unique_ptr<RenderPipeline> pipeline;
            std::unique_ptr<RenderDescriptorSet> set;
            std::unique_ptr<RenderTexture> tex;
            std::unique_ptr<RenderSampler> sampler;
            std::unique_ptr<RenderBuffer> stagingMip0;
            std::unique_ptr<RenderBuffer> stagingMip1;
            std::unique_ptr<RenderTexture> rt;
            std::unique_ptr<RenderFramebuffer> fb;
            std::unique_ptr<RenderBuffer> readback;
            bool uploaded = false;
        };
        auto fx = std::make_shared<Fixture>();
        fx->device = device;

        static constexpr uint32_t W = 16, H = 16;
        static constexpr uint8_t EXPECTED = 0xFF; // mip 1 is white

        Workload w;
        w.name = "texture_mip_readback";

        w.setup = [fx, fmt]() {
            fx->vs = PLUME_SELECT_SHADER(fx->device, fmt, "VSMain", SampleLevelVert);
            fx->ps = PLUME_SELECT_SHADER(fx->device, fmt, "PSMain", SampleLevelFrag);

            // 2x1 base texture with 2 mips: mip0 = 2x1, mip1 = 1x1. Single-row uploads.
            RenderTextureDesc texDesc;
            texDesc.dimension = RenderTextureDimension::TEXTURE_2D;
            texDesc.width = 2;
            texDesc.height = 1;
            texDesc.depth = 1;
            texDesc.mipLevels = 2;
            texDesc.arraySize = 1;
            texDesc.format = RenderFormat::R8G8B8A8_UNORM;
            fx->tex = fx->device->createTexture(texDesc);

            fx->stagingMip0 = fx->device->createBuffer(RenderBufferDesc::UploadBuffer(8));
            uint8_t mip0[8] = { 0x00, 0x00, 0x00, 0xFF,   0x00, 0x00, 0x00, 0xFF };
            std::memcpy(fx->stagingMip0->map(), mip0, 8);
            fx->stagingMip0->unmap();

            fx->stagingMip1 = fx->device->createBuffer(RenderBufferDesc::UploadBuffer(4));
            uint8_t mip1[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
            std::memcpy(fx->stagingMip1->map(), mip1, 4);
            fx->stagingMip1->unmap();

            RenderSamplerDesc samplerDesc;
            samplerDesc.minFilter = RenderFilter::NEAREST;
            samplerDesc.magFilter = RenderFilter::NEAREST;
            samplerDesc.addressU = RenderTextureAddressMode::CLAMP;
            samplerDesc.addressV = RenderTextureAddressMode::CLAMP;
            samplerDesc.addressW = RenderTextureAddressMode::CLAMP;
            fx->sampler = fx->device->createSampler(samplerDesc);

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

            RenderDescriptorSetBuilder b;
            b.begin();
            b.addTexture(0);
            b.addSampler(1);
            b.end();
            fx->set = fx->device->createDescriptorSet(b.descriptorSetDesc);
            fx->set->setTexture(0, fx->tex.get(), RenderTextureLayout::SHADER_READ);
            fx->set->setSampler(1, fx->sampler.get());

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
            if (!fx->uploaded) {
                cmd->barriers(RenderBarrierStage::COPY,
                    RenderTextureBarrier(fx->tex.get(), RenderTextureLayout::COPY_DEST));
                RenderTextureCopyLocation dstMip0 = RenderTextureCopyLocation::Subresource(fx->tex.get(), 0, 0);
                RenderTextureCopyLocation srcMip0 = RenderTextureCopyLocation::PlacedFootprint(
                    fx->stagingMip0.get(), RenderFormat::R8G8B8A8_UNORM, 2, 1, 1, 2, 0);
                cmd->copyTextureRegion(dstMip0, srcMip0, 0, 0, 0, nullptr);
                RenderTextureCopyLocation dstMip1 = RenderTextureCopyLocation::Subresource(fx->tex.get(), 1, 0);
                RenderTextureCopyLocation srcMip1 = RenderTextureCopyLocation::PlacedFootprint(
                    fx->stagingMip1.get(), RenderFormat::R8G8B8A8_UNORM, 1, 1, 1, 1, 0);
                cmd->copyTextureRegion(dstMip1, srcMip1, 0, 0, 0, nullptr);
                cmd->barriers(RenderBarrierStage::GRAPHICS,
                    RenderTextureBarrier(fx->tex.get(), RenderTextureLayout::SHADER_READ));
                fx->uploaded = true;
            }

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
            VerifyResult result = VerifyResult::ok();
            uint8_t d = p[0] > EXPECTED ? p[0] - EXPECTED : EXPECTED - p[0];
            if (d > 1) {
                char buf[96];
                snprintf(buf, sizeof(buf), "center R = %02x, expected %02x (mip 1)", p[0], EXPECTED);
                result = VerifyResult::fail(buf);
            }
            fx->readback->unmap();
            return result;
        };

        return w;
    }

}
