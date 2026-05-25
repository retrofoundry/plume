//
// plume bench
//

#pragma once

#include "workload.h"
#include "plume_render_interface.h"
#include "plume_render_interface_builders.h"

#include "shader_select.h"
#ifdef __APPLE__
#include "shaders/TextureSampleVert.hlsl.metal.h"
#include "shaders/TextureSampleFrag.hlsl.metal.h"
#endif
#include "shaders/TextureSampleVert.hlsl.spirv.h"
#include "shaders/TextureSampleFrag.hlsl.spirv.h"
#ifdef _WIN32
#include "shaders/TextureSampleVert.hlsl.dxil.h"
#include "shaders/TextureSampleFrag.hlsl.dxil.h"
#endif

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>

namespace plume::bench {

    // Uploads a 2x1 texture (left texel 0x00, right texel 0xFF in all channels) and
    // samples it with a LINEAR sampler across a fullscreen draw. Bilinear filtering
    // produces a blended mid-gray at the center; NEAREST would return exactly 0x00 or
    // 0xFF. Verifying the center is strictly inside (0x00, 0xFF) proves interpolation.
    inline Workload createSamplerLinearWorkload(RenderDevice *device, RenderCommandQueue *queue, RenderShaderFormat fmt) {
        struct Fixture {
            RenderDevice *device;
            std::unique_ptr<RenderShader> vs;
            std::unique_ptr<RenderShader> ps;
            std::unique_ptr<RenderPipelineLayout> layout;
            std::unique_ptr<RenderPipeline> pipeline;
            std::unique_ptr<RenderDescriptorSet> set;
            std::unique_ptr<RenderTexture> tex;
            std::unique_ptr<RenderSampler> sampler;
            std::unique_ptr<RenderBuffer> staging;
            std::unique_ptr<RenderTexture> rt;
            std::unique_ptr<RenderFramebuffer> fb;
            std::unique_ptr<RenderBuffer> readback;
            bool uploaded = false;
        };
        auto fx = std::make_shared<Fixture>();
        fx->device = device;

        static constexpr uint32_t W = 16, H = 16;

        Workload w;
        w.name = "sampler_linear_readback";

        w.setup = [fx, fmt]() {
            fx->vs = PLUME_SELECT_SHADER(fx->device, fmt, "VSMain", TextureSampleVert);
            fx->ps = PLUME_SELECT_SHADER(fx->device, fmt, "PSMain", TextureSampleFrag);

            // 2x1 source texture: left = black, right = white (single row, no row-pitch issue).
            RenderTextureDesc texDesc;
            texDesc.dimension = RenderTextureDimension::TEXTURE_2D;
            texDesc.width = 2;
            texDesc.height = 1;
            texDesc.depth = 1;
            texDesc.mipLevels = 1;
            texDesc.arraySize = 1;
            texDesc.format = RenderFormat::R8G8B8A8_UNORM;
            fx->tex = fx->device->createTexture(texDesc);

            fx->staging = fx->device->createBuffer(RenderBufferDesc::UploadBuffer(8));
            uint8_t texels[8] = { 0x00, 0x00, 0x00, 0xFF,   0xFF, 0xFF, 0xFF, 0xFF };
            std::memcpy(fx->staging->map(), texels, 8);
            fx->staging->unmap();

            RenderSamplerDesc samplerDesc;
            samplerDesc.minFilter = RenderFilter::LINEAR;
            samplerDesc.magFilter = RenderFilter::LINEAR;
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
                RenderTextureCopyLocation dstTex = RenderTextureCopyLocation::Subresource(fx->tex.get(), 0, 0);
                RenderTextureCopyLocation srcBuf = RenderTextureCopyLocation::PlacedFootprint(
                    fx->staging.get(), RenderFormat::R8G8B8A8_UNORM, 2, 1, 1, 2, 0);
                cmd->copyTextureRegion(dstTex, srcBuf, 0, 0, 0, nullptr);
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
            // Blended gray: strictly inside (0x00, 0xFF). NEAREST would give 0x00 or 0xFF.
            if (p[0] < 0x30 || p[0] > 0xD0) {
                char buf[96];
                snprintf(buf, sizeof(buf), "center R = %02x, expected a blended value in [0x30,0xD0]", p[0]);
                result = VerifyResult::fail(buf);
            }
            fx->readback->unmap();
            return result;
        };

        return w;
    }

}
