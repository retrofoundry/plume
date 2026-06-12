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

    // Uploads a 1x1 texture, samples it across a fullscreen draw, reads back, verifies.
    // Covers texture upload (buffer->texture copy), SHADER_READ barrier, texture+sampler
    // descriptors, and sampling.
    inline Workload createTextureSampleWorkload(RenderDevice *device, RenderCommandQueue *queue, RenderShaderFormat fmt) {
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
        static constexpr uint8_t TR = 0x40, TG = 0x80, TB = 0xC0, TA = 0xFF; // the texel

        Workload w;
        w.name = "texture_sample_readback";

        w.setup = [fx, fmt]() {
            fx->vs = PLUME_SELECT_SHADER(fx->device, fmt, "VSMain", TextureSampleVert);
            fx->ps = PLUME_SELECT_SHADER(fx->device, fmt, "PSMain", TextureSampleFrag);

            // 1x1 sampled texture (uploaded in encode the first time).
            RenderTextureDesc texDesc;
            texDesc.dimension = RenderTextureDimension::TEXTURE_2D;
            texDesc.width = 1;
            texDesc.height = 1;
            texDesc.depth = 1;
            texDesc.mipLevels = 1;
            texDesc.arraySize = 1;
            texDesc.format = RenderFormat::R8G8B8A8_UNORM;
            fx->tex = fx->device->createTexture(texDesc);

            fx->staging = fx->device->createBuffer(RenderBufferDesc::UploadBuffer(4));
            uint8_t texel[4] = { TR, TG, TB, TA };
            std::memcpy(fx->staging->map(), texel, 4);
            fx->staging->unmap();

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

            // Descriptor set: texture at 0, sampler at 1 (matches register(t0)/register(s1)).
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
            // Upload the texel once (the texture persists across iterations).
            if (!fx->uploaded) {
                cmd->barriers(RenderBarrierStage::COPY,
                    RenderTextureBarrier(fx->tex.get(), RenderTextureLayout::COPY_DEST));
                RenderTextureCopyLocation dstTex = RenderTextureCopyLocation::Subresource(fx->tex.get(), 0, 0);
                RenderTextureCopyLocation srcBuf = RenderTextureCopyLocation::PlacedFootprint(
                    fx->staging.get(), RenderFormat::R8G8B8A8_UNORM, 1, 1, 1, 1, 0);
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
            auto close = [](uint8_t a, uint8_t b) { return (a > b ? a - b : b - a) <= 1; };
            VerifyResult result = VerifyResult::ok();
            if (!close(p[0], TR) || !close(p[1], TG) || !close(p[2], TB) || !close(p[3], TA)) {
                char buf[128];
                snprintf(buf, sizeof(buf), "center = (%02x,%02x,%02x,%02x), expected (%02x,%02x,%02x,%02x)",
                    p[0], p[1], p[2], p[3], TR, TG, TB, TA);
                result = VerifyResult::fail(buf);
            }
            fx->readback->unmap();
            return result;
        };

        return w;
    }

}
