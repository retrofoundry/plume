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

    // Writes a known pattern into an upload buffer, GPU-copies it to a readback buffer,
    // and verifies the bytes survive the round trip. Exercises copyBufferRegion + readback.
    inline Workload createBufferCopyWorkload(RenderDevice *device, RenderCommandQueue *queue) {
        struct Fixture {
            RenderDevice *device;
            std::unique_ptr<RenderBuffer> upload;
            std::unique_ptr<RenderBuffer> readback;
        };
        auto fx = std::make_shared<Fixture>();
        fx->device = device;

        static constexpr uint32_t N = 256; // bytes

        Workload w;
        w.name = "buffer_copy_readback";

        w.setup = [fx]() {
            fx->upload = fx->device->createBuffer(RenderBufferDesc::UploadBuffer(N));
            fx->readback = fx->device->createBuffer(RenderBufferDesc::ReadbackBuffer(N));

            auto *src = static_cast<uint8_t *>(fx->upload->map());
            for (uint32_t i = 0; i < N; i++) {
                src[i] = static_cast<uint8_t>((i * 31 + 7) & 0xFF);
            }
            fx->upload->unmap();
        };

        w.encode = [fx](RenderCommandList *cmd) {
            cmd->copyBufferRegion(fx->readback->at(0), fx->upload->at(0), N);
        };

        w.verify = [fx]() -> VerifyResult {
            auto *dst = static_cast<const uint8_t *>(fx->readback->map());
            if (dst == nullptr) {
                return VerifyResult::fail("readback map() returned null");
            }
            VerifyResult result = VerifyResult::ok();
            for (uint32_t i = 0; i < N; i++) {
                uint8_t expected = static_cast<uint8_t>((i * 31 + 7) & 0xFF);
                if (dst[i] != expected) {
                    char buf[96];
                    snprintf(buf, sizeof(buf), "byte %u = %02x, expected %02x", i, dst[i], expected);
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
