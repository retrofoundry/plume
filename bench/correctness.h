//
// plume bench
//

#pragma once

#include "workload.h"
#include "plume_render_interface.h"

#include <cstdio>
#include <cstdlib>
#include <memory>

namespace plume::bench {

    struct CorrectnessOutcome {
        std::string name;
        VerifyResult result;
    };

    inline CorrectnessOutcome runWorkloadCorrectness(RenderDevice *device, RenderCommandQueue *queue, const Workload &w) {
        // PLUME_TRACE logs each workload's stage before it runs, so a backend that hard-crashes
        // (e.g. during a new backend's bring-up) reveals exactly where it died in the output.
        const bool trace = std::getenv("PLUME_TRACE") != nullptr;
        auto step = [&](const char *stage) {
            if (trace) {
                printf("  [trace] %s: %s\n", w.name.c_str(), stage);
            }
        };

        step("setup");
        if (w.setup) {
            w.setup();
        }

        if (w.encode) {
            step("encode");
            auto fence = device->createCommandFence();
            auto cmdList = queue->createCommandList();
            cmdList->begin();
            w.encode(cmdList.get());
            cmdList->end();

            step("submit");
            const RenderCommandList *raw = cmdList.get();
            queue->executeCommandLists(&raw, 1, nullptr, 0, nullptr, 0, fence.get());
            step("wait");
            queue->waitForCommandFence(fence.get());
        } else if (w.run) {
            step("run");
            w.run();
        }

        step("verify");
        VerifyResult vr = w.verify ? w.verify() : VerifyResult::ok();
        return {w.name, vr};
    }

    // Returns the number of failures. Zero means all passed.
    inline int runCorrectnessSuite(RenderDevice *device, RenderCommandQueue *queue, const WorkloadSuite &suite) {
        printf("\n=== %s ===\n", suite.name.c_str());
        int failures = 0;
        for (const auto &w : suite.workloads) {
            CorrectnessOutcome o = runWorkloadCorrectness(device, queue, w);
            if (o.result.passed) {
                printf("  PASS  %s\n", o.name.c_str());
            } else {
                printf("  FAIL  %s — %s\n", o.name.c_str(), o.result.detail.c_str());
                failures++;
            }
        }
        return failures;
    }

}
