//
// plume bench
//

#pragma once

#include "bench.h"
#include "workload.h"
#include "workloads/buffer_copy.h"
#include "workloads/buffer_upload.h"
#ifdef PLUME_BENCH_HAS_SHADERS
#include "workloads/frame_macro.h"
#include "workloads/depth_test.h"
#include "workloads/blend.h"
#include "workloads/indexed_draw.h"
#include "workloads/compute_dispatch.h"
#include "workloads/descriptor_draw.h"
#include "workloads/texture_sample.h"
#include "workloads/compute_barrier.h"
#include "workloads/format_r8_unorm.h"
#include "workloads/format_rgba16f.h"
#include "workloads/sampler_linear.h"
#include "workloads/texture_mip.h"
#include "workloads/mrt.h"
#include "workloads/instanced.h"
#include "workloads/structured_buffer_read.h"
#include "workloads/multi_descriptor_set.h"
#include "workloads/msaa_resolve.h"
#include "workloads/msaa_coverage.h"
#include "workloads/stencil_mask.h"
#include "workloads/stencil_increment.h"
#include "workloads/image_store.h"
#include "workloads/cull_mode.h"
#endif

#include <vector>

namespace plume::bench {

    // GPU-submitting workloads timed with the dual cpu_encode/gpu_complete metric.
    inline std::vector<BenchResult> runWorkloadPerfSuite(RenderDevice *device, RenderCommandQueue *queue, RenderShaderFormat fmt, const WorkloadPerfConfig &config) {
        std::vector<Workload> workloads;
        workloads.push_back(createBufferCopyWorkload(device, queue));        // minimal submission round-trip
        workloads.push_back(createBufferUploadWorkload(device, queue));      // map/unmap upload
#ifdef PLUME_BENCH_HAS_SHADERS
        workloads.push_back(createFrameMacroWorkload(device, queue, fmt));   // full-frame render
        workloads.push_back(createDepthTestWorkload(device, queue, fmt));
        workloads.push_back(createBlendWorkload(device, queue, fmt));
        workloads.push_back(createIndexedDrawWorkload(device, queue, fmt));
        workloads.push_back(createComputeDispatchWorkload(device, queue, fmt));
        workloads.push_back(createDescriptorDrawWorkload(device, queue, fmt));
        workloads.push_back(createTextureSampleWorkload(device, queue, fmt));
        workloads.push_back(createComputeBarrierWorkload(device, queue, fmt));
        workloads.push_back(createFormatR8UnormWorkload(device, queue, fmt));
        workloads.push_back(createFormatRgba16fWorkload(device, queue, fmt));
        workloads.push_back(createSamplerLinearWorkload(device, queue, fmt));
        workloads.push_back(createTextureMipWorkload(device, queue, fmt));
        workloads.push_back(createMrtWorkload(device, queue, fmt));
        workloads.push_back(createInstancedWorkload(device, queue, fmt));
        workloads.push_back(createStructuredBufferReadWorkload(device, queue, fmt));
        workloads.push_back(createMultiDescriptorSetWorkload(device, queue, fmt));
        workloads.push_back(createMsaaResolveWorkload(device, queue, fmt));
        workloads.push_back(createMsaaCoverageWorkload(device, queue, fmt));
        workloads.push_back(createStencilMaskWorkload(device, queue, fmt));
        workloads.push_back(createStencilIncrementWorkload(device, queue, fmt));
        workloads.push_back(createImageStoreWorkload(device, queue, fmt));
        workloads.push_back(createCullModeWorkload(device, queue, fmt));
#endif

        // Batch submits per sample so the gated cpu_encode metric is not swamped by
        // per-submit jitter; honor an explicit caller override if one was set.
        WorkloadPerfConfig cfg = config;
        if (cfg.innerLoopCount <= 1) {
            cfg.innerLoopCount = 20;
        }

        std::vector<BenchResult> results;
        results.reserve(workloads.size());
        for (const auto &w : workloads) {
            results.push_back(runWorkloadPerf(device, queue, w, cfg));
        }
        return results;
    }

}
