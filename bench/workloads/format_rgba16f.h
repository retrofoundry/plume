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
#include <cstring>
#include <memory>

namespace plume::bench {

    // Decodes an IEEE-754 half (binary16) to float. Our test values are all normal
    // (no subnormal/inf path needed) but the full decode is here for correctness.
    inline float halfToFloat(uint16_t h) {
        uint32_t sign = uint32_t(h & 0x8000) << 16;
        uint32_t exp = (h >> 10) & 0x1F;
        uint32_t mant = h & 0x3FF;
        uint32_t f;
        if (exp == 0) {
            if (mant == 0) {
                f = sign;
            } else {
                exp = 127 - 15 + 1;
                while ((mant & 0x400) == 0) { mant <<= 1; exp--; }
                mant &= 0x3FF;
                f = sign | (exp << 23) | (mant << 13);
            }
        } else if (exp == 0x1F) {
            f = sign | 0x7F800000u | (mant << 13);
        } else {
            f = sign | ((exp - 15 + 127) << 23) | (mant << 13);
        }
        float out;
        std::memcpy(&out, &f, 4);
        return out;
    }

    // Renders out-of-[0,1] values into an R16G16B16A16_FLOAT render target and reads
    // them back. A UNORM target would clamp 2.5 -> 1.0 and -1.0 -> 0.0; a true float
    // target preserves them. Exercises half-float render-target + 8-byte/pixel readback.
    inline Workload createFormatRgba16fWorkload(RenderDevice *device, RenderCommandQueue *queue, RenderShaderFormat fmt) {
        struct Fixture {
            RenderDevice *device;
            std::unique_ptr<RenderShader> vs;
            std::unique_ptr<RenderShader> ps;
            std::unique_ptr<RenderPipelineLayout> layout;
            std::unique_ptr<RenderPipeline> pipeline;
            std::unique_ptr<RenderTexture> rt;
            std::unique_ptr<RenderFramebuffer> fb;
            std::unique_ptr<RenderBuffer> readback;
        };
        auto fx = std::make_shared<Fixture>();
        fx->device = device;

        static constexpr uint32_t W = 16, H = 16;
        const float EXPECTED[4] = { 2.5f, -1.0f, 0.5f, 1.0f };

        Workload w;
        w.name = "format_rgba16f_readback";

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
            rtDesc.format = RenderFormat::R16G16B16A16_FLOAT;
            rtDesc.flags = RenderTextureFlag::RENDER_TARGET;
            fx->rt = fx->device->createTexture(rtDesc);

            const RenderTexture *color = fx->rt.get();
            RenderFramebufferDesc fbDesc;
            fbDesc.colorAttachments = &color;
            fbDesc.colorAttachmentsCount = 1;
            fx->fb = fx->device->createFramebuffer(fbDesc);

            fx->readback = fx->device->createBuffer(
                RenderBufferDesc::ReadbackBuffer(readbackRowPitchBytes(W, 8) * H));

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
            desc.renderTargetFormat[0] = RenderFormat::R16G16B16A16_FLOAT;
            desc.renderTargetBlend[0] = RenderBlendDesc::Copy();
            desc.renderTargetCount = 1;
            desc.primitiveTopology = RenderPrimitiveTopology::TRIANGLE_LIST;
            fx->pipeline = fx->device->createGraphicsPipeline(desc);
        };

        w.encode = [fx, EXPECTED](RenderCommandList *cmd) {
            cmd->barriers(RenderBarrierStage::GRAPHICS,
                RenderTextureBarrier(fx->rt.get(), RenderTextureLayout::COLOR_WRITE));
            cmd->setFramebuffer(fx->fb.get());
            cmd->clearColor(0, RenderColor(0.0f, 0.0f, 0.0f, 1.0f));
            cmd->setGraphicsPipelineLayout(fx->layout.get());
            cmd->setPipeline(fx->pipeline.get());

            cmd->setGraphicsPushConstants(0, EXPECTED);

            RenderViewport vp(0.0f, 0.0f, float(W), float(H));
            RenderRect sc(0, 0, int32_t(W), int32_t(H));
            cmd->setViewports(vp);
            cmd->setScissors(sc);
            cmd->drawInstanced(3, 1, 0, 0);

            cmd->barriers(RenderBarrierStage::COPY,
                RenderTextureBarrier(fx->rt.get(), RenderTextureLayout::COPY_SOURCE));
            RenderTextureCopyLocation dst = RenderTextureCopyLocation::PlacedFootprint(
                fx->readback.get(), RenderFormat::R16G16B16A16_FLOAT, W, H, 1, readbackRowTexels(W, 8), 0);
            RenderTextureCopyLocation src = RenderTextureCopyLocation::Subresource(fx->rt.get(), 0, 0);
            cmd->copyTextureRegion(dst, src, 0, 0, 0, nullptr);
        };

        w.verify = [fx, EXPECTED]() -> VerifyResult {
            auto *px8 = static_cast<const uint8_t *>(fx->readback->map());
            if (px8 == nullptr) {
                return VerifyResult::fail("readback map() returned null");
            }
            const uint16_t *p = reinterpret_cast<const uint16_t *>(
                px8 + (H / 2) * readbackRowPitchBytes(W, 8) + (W / 2) * 8);
            VerifyResult result = VerifyResult::ok();
            for (int i = 0; i < 4; i++) {
                float v = halfToFloat(p[i]);
                float d = v > EXPECTED[i] ? v - EXPECTED[i] : EXPECTED[i] - v;
                if (d > 0.01f) {
                    char buf[128];
                    snprintf(buf, sizeof(buf), "channel %d = %f, expected %f", i, v, EXPECTED[i]);
                    result = VerifyResult::fail(buf);
                    break;
                }
            }
            fx->readback->unmap();
            return result;
        };

        return w;
    }

}
