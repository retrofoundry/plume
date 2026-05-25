//
// plume bench
//

#pragma once

#include "workload.h"
#include "plume_render_interface.h"

#include <cstdint>
#include <cstdio>
#include <memory>

namespace plume::bench {

    // Clears a small render target to a known color, copies it to a readback buffer,
    // and verifies every pixel. Exercises the render-target -> readback path.
    inline Workload createClearColorWorkload(RenderDevice *device, RenderCommandQueue *queue) {
        struct Fixture {
            RenderDevice *device;
            RenderCommandQueue *queue;
            std::unique_ptr<RenderTexture> rt;
            std::unique_ptr<RenderFramebuffer> fb;
            std::unique_ptr<RenderBuffer> readback;
        };
        auto fx = std::make_shared<Fixture>();
        fx->device = device;
        fx->queue = queue;

        static constexpr uint32_t W = 4, H = 4;
        // Channel values chosen as k/255 so float->unorm round-trips exactly.
        static constexpr uint8_t R = 0x40, G = 0x80, B = 0xC0, A = 0xFF;

        Workload w;
        w.name = "clear_color_readback";

        w.setup = [fx]() {
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
        };

        w.encode = [fx](RenderCommandList *cmd) {
            cmd->barriers(RenderBarrierStage::GRAPHICS,
                RenderTextureBarrier(fx->rt.get(), RenderTextureLayout::COLOR_WRITE));
            cmd->setFramebuffer(fx->fb.get());
            cmd->clearColor(0, RenderColor(R / 255.0f, G / 255.0f, B / 255.0f, A / 255.0f));

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
            VerifyResult result = VerifyResult::ok();
            const uint32_t pitch = readbackRowPitchBytes(W, 4);
            for (uint32_t y = 0; y < H && result.passed; y++) {
                const uint8_t *row = px + y * pitch;
                for (uint32_t x = 0; x < W; x++) {
                    const uint8_t *p = row + x * 4;
                    // Allow +/-1 for any backend rounding differences.
                    auto close = [](uint8_t a, uint8_t b) { return (a > b ? a - b : b - a) <= 1; };
                    if (!close(p[0], R) || !close(p[1], G) || !close(p[2], B) || !close(p[3], A)) {
                        char buf[160];
                        snprintf(buf, sizeof(buf),
                            "pixel (%u,%u) = (%02x,%02x,%02x,%02x), expected (%02x,%02x,%02x,%02x)",
                            x, y, p[0], p[1], p[2], p[3], R, G, B, A);
                        result = VerifyResult::fail(buf);
                        break;
                    }
                }
            }
            fx->readback->unmap();
            return result;
        };

        return w;
    }

}
