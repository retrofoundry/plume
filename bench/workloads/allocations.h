//
// plume bench
//

#pragma once

#include "workload.h"
#include "plume_render_interface.h"

#include <memory>

namespace plume::bench {

    // Buffer creation is a real, valid operation with no GPU output; verify asserts validity.
    inline Workload createBufferAllocWorkload(RenderDevice *device) {
        struct Fixture {
            RenderDevice *device;
            std::unique_ptr<RenderBuffer> last;
        };
        auto fx = std::make_shared<Fixture>();
        fx->device = device;

        Workload w;
        w.name = "buffer_create_64KB_private";
        w.run = [fx]() {
            fx->last = fx->device->createBuffer(RenderBufferDesc::DefaultBuffer(64 * 1024));
        };
        w.verify = [fx]() -> VerifyResult {
            return fx->last != nullptr ? VerifyResult::ok()
                                       : VerifyResult::fail("createBuffer returned null");
        };
        return w;
    }

}
