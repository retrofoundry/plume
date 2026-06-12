//
// plume bench
//

#pragma once

#include "plume_render_interface.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace plume::bench {

    // A texture->buffer readback ("PlacedFootprint") must have a row pitch that some
    // backends require to be aligned: D3D12 mandates 256-byte rows
    // (D3D12_TEXTURE_DATA_PITCH_ALIGNMENT). Metal and Vulkan accept the padded pitch
    // too, so aligning unconditionally keeps a single cross-backend readback path.
    // Returns the aligned row pitch in BYTES.
    inline uint32_t readbackRowPitchBytes(uint32_t width, uint32_t bytesPerPixel) {
        constexpr uint32_t ALIGN = 256;
        const uint32_t tight = width * bytesPerPixel;
        return (tight + (ALIGN - 1)) & ~(ALIGN - 1);
    }

    // The same pitch expressed in TEXELS, for the PlacedFootprint rowWidth argument.
    // (ALIGN=256 is divisible by the 1/4/8-byte pixel sizes the suite uses, so this is exact.)
    inline uint32_t readbackRowTexels(uint32_t width, uint32_t bytesPerPixel) {
        return readbackRowPitchBytes(width, bytesPerPixel) / bytesPerPixel;
    }

    struct VerifyResult {
        bool passed = true;
        std::string detail;

        static VerifyResult ok() { return {true, ""}; }
        static VerifyResult fail(std::string detail) { return {false, std::move(detail)}; }
    };

    // One valid, submittable RHI scenario, consumed by both the correctness and perf runners.
    struct Workload {
        std::string name;

        // Persistent resource creation, run once, never timed.
        std::function<void()> setup;

        // Command-recording body under test. Set this for GPU-submitting workloads.
        // The runner wraps it with createCommandList/begin/end and submits the result.
        std::function<void(RenderCommandList *)> encode;

        // CPU-only timed body for workloads that record no commands (e.g. resource creation).
        // Set this XOR encode.
        std::function<void()> run;

        // Correctness check after GPU completion. Null => perf-only (no correctness entry).
        std::function<VerifyResult()> verify;
    };

    struct WorkloadSuite {
        std::string name;
        std::vector<Workload> workloads;
    };

}
