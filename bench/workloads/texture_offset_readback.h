//
// plume bench
//

#pragma once

#include "workload.h"
#include "plume_render_interface.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>

namespace plume::bench {

    // Uploads a position-dependent pattern to a texture, then reads back a SUB-REGION
    // at a non-zero srcBox origin and verifies the bytes. Every other readback in the
    // suite copies the full texture from (0,0), so a backend that sources the copy
    // origin from the wrong argument (e.g. dst offsets instead of srcBox) still passes
    // them; this workload is the one that forces the origins to disagree.
    inline Workload createTextureOffsetReadbackWorkload(RenderDevice *device, RenderCommandQueue *queue) {
        struct Fixture {
            RenderDevice *device;
            std::unique_ptr<RenderTexture> tex;
            std::unique_ptr<RenderBuffer> staging;
            std::unique_ptr<RenderBuffer> readback;
        };
        auto fx = std::make_shared<Fixture>();
        fx->device = device;

        static constexpr uint32_t W = 16, H = 16;     // full texture
        static constexpr uint32_t RX = 8, RY = 4;     // region origin
        static constexpr uint32_t RW = 8, RH = 8;     // region size

        // Unique bytes per position so any origin/pitch mistake changes the data.
        auto texel = [](uint32_t x, uint32_t y, uint8_t *out) {
            out[0] = static_cast<uint8_t>(x * 16 + 1);
            out[1] = static_cast<uint8_t>(y * 16 + 2);
            out[2] = static_cast<uint8_t>((x ^ y) * 8 + 3);
            out[3] = 0xFF;
        };

        Workload w;
        w.name = "texture_offset_readback";

        w.setup = [fx, texel]() {
            RenderTextureDesc texDesc;
            texDesc.dimension = RenderTextureDimension::TEXTURE_2D;
            texDesc.width = W;
            texDesc.height = H;
            texDesc.depth = 1;
            texDesc.mipLevels = 1;
            texDesc.arraySize = 1;
            texDesc.format = RenderFormat::R8G8B8A8_UNORM;
            fx->tex = fx->device->createTexture(texDesc);

            const uint32_t uploadPitch = readbackRowPitchBytes(W, 4);
            fx->staging = fx->device->createBuffer(RenderBufferDesc::UploadBuffer(uploadPitch * H));
            auto *dst = static_cast<uint8_t *>(fx->staging->map());
            for (uint32_t y = 0; y < H; y++) {
                for (uint32_t x = 0; x < W; x++) {
                    texel(x, y, dst + y * uploadPitch + x * 4);
                }
            }
            fx->staging->unmap();

            fx->readback = fx->device->createBuffer(
                RenderBufferDesc::ReadbackBuffer(readbackRowPitchBytes(RW, 4) * RH));
        };

        w.encode = [fx](RenderCommandList *cmd) {
            cmd->barriers(RenderBarrierStage::COPY,
                RenderTextureBarrier(fx->tex.get(), RenderTextureLayout::COPY_DEST));
            RenderTextureCopyLocation uploadSrc = RenderTextureCopyLocation::PlacedFootprint(
                fx->staging.get(), RenderFormat::R8G8B8A8_UNORM, W, H, 1, readbackRowTexels(W, 4), 0);
            RenderTextureCopyLocation uploadDst = RenderTextureCopyLocation::Subresource(fx->tex.get(), 0, 0);
            cmd->copyTextureRegion(uploadDst, uploadSrc, 0, 0, 0, nullptr);

            cmd->barriers(RenderBarrierStage::COPY,
                RenderTextureBarrier(fx->tex.get(), RenderTextureLayout::COPY_SOURCE));
            RenderTextureCopyLocation dst = RenderTextureCopyLocation::PlacedFootprint(
                fx->readback.get(), RenderFormat::R8G8B8A8_UNORM, RW, RH, 1, readbackRowTexels(RW, 4), 0);
            RenderTextureCopyLocation src = RenderTextureCopyLocation::Subresource(fx->tex.get(), 0, 0);
            const RenderBox srcBox(RX, RY, RX + RW, RY + RH);
            cmd->copyTextureRegion(dst, src, 0, 0, 0, &srcBox);
        };

        w.verify = [fx, texel]() -> VerifyResult {
            auto *px = static_cast<const uint8_t *>(fx->readback->map());
            if (px == nullptr) {
                return VerifyResult::fail("readback map() returned null");
            }
            VerifyResult result = VerifyResult::ok();
            const uint32_t pitch = readbackRowPitchBytes(RW, 4);
            for (uint32_t y = 0; y < RH && result.passed; y++) {
                for (uint32_t x = 0; x < RW; x++) {
                    const uint8_t *p = px + y * pitch + x * 4;
                    uint8_t expected[4];
                    texel(RX + x, RY + y, expected);
                    if (std::memcmp(p, expected, 4) != 0) {
                        char buf[160];
                        snprintf(buf, sizeof(buf),
                            "region pixel (%u,%u) = (%02x,%02x,%02x,%02x), expected texel (%u,%u) = (%02x,%02x,%02x,%02x)",
                            x, y, p[0], p[1], p[2], p[3],
                            RX + x, RY + y, expected[0], expected[1], expected[2], expected[3]);
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
