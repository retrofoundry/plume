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

    // Maps an upload buffer, writes a pattern, unmaps, GPU-copies to a readback buffer,
    // and verifies the round trip. Exercises the per-frame map/unmap upload path.
    inline Workload createBufferUploadWorkload(RenderDevice *device, RenderCommandQueue *queue) {
        struct Fixture {
            RenderDevice *device;
            std::unique_ptr<RenderBuffer> upload;
            std::unique_ptr<RenderBuffer> readback;
        };
        auto fx = std::make_shared<Fixture>();
        fx->device = device;

        static constexpr uint32_t N = 4096;

        Workload w;
        w.name = "buffer_map_upload_4KB";

        w.setup = [fx]() {
            fx->upload = fx->device->createBuffer(RenderBufferDesc::UploadBuffer(N));
            fx->readback = fx->device->createBuffer(RenderBufferDesc::ReadbackBuffer(N));
        };

        // The map/memcpy/unmap is the work under test; the copy carries it to a readable buffer.
        w.encode = [fx](RenderCommandList *cmd) {
            auto *src = static_cast<uint8_t *>(fx->upload->map());
            for (uint32_t i = 0; i < N; i++) {
                src[i] = static_cast<uint8_t>((i * 13 + 5) & 0xFF);
            }
            fx->upload->unmap();
            cmd->copyBufferRegion(fx->readback->at(0), fx->upload->at(0), N);
        };

        w.verify = [fx]() -> VerifyResult {
            auto *dst = static_cast<const uint8_t *>(fx->readback->map());
            if (dst == nullptr) {
                return VerifyResult::fail("readback map() returned null");
            }
            VerifyResult result = VerifyResult::ok();
            for (uint32_t i = 0; i < N; i++) {
                uint8_t expected = static_cast<uint8_t>((i * 13 + 5) & 0xFF);
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
