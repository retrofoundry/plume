#include "plume_render_interface.h"
#include "correctness.h"
#include "harness_tests.h"
#include "workloads/clear_color.h"
#include "workloads/buffer_copy.h"
#include "workloads/buffer_upload.h"
#include "workloads/texture_offset_readback.h"
#include "workloads/allocations.h"

#ifdef __APPLE__
#include <pthread/qos.h>
#endif

// The graphics + compute workloads need the HLSL shader pipeline (scene_color etc.),
// so they are only built when it is enabled.
#ifdef PLUME_TEST_HAS_SHADERS
#include "shader_select.h"
#ifdef __APPLE__
#include "shaders/ComputeFill.hlsl.metal.h"
#endif
#include "shaders/ComputeFill.hlsl.spirv.h"
#ifdef _WIN32
#include "shaders/ComputeFill.hlsl.dxil.h"
#endif
#include "workloads/pipeline_create.h"
#include "workloads/frame_macro.h"
#include "workloads/indexed_draw.h"
#include "workloads/depth_test.h"
#include "workloads/blend.h"
#include "workloads/cull_mode.h"
#include "workloads/compute_dispatch.h"
#include "workloads/image_store.h"
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
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

namespace plume {
#ifdef __APPLE__
    extern std::unique_ptr<RenderInterface> CreateMetalInterface();
#endif
#ifdef _WIN32
    extern std::unique_ptr<RenderInterface> CreateD3D12Interface();
#endif
    extern std::unique_ptr<RenderInterface> CreateVulkanInterface();
}

int main() {
#ifdef __APPLE__
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
    // Unbuffered so workload output survives a backend abort (e.g. a Vulkan fault).
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("plume test\n");
    printf("==========\n\n");

    const char *backend = std::getenv("PLUME_BACKEND");
    std::unique_ptr<plume::RenderInterface> renderInterface;
#if defined(__APPLE__)
    if (backend && std::strcmp(backend, "vulkan") == 0) {
        renderInterface = plume::CreateVulkanInterface();
    } else {
        renderInterface = plume::CreateMetalInterface();
    }
#elif defined(_WIN32)
    if (backend && std::strcmp(backend, "vulkan") == 0) {
        renderInterface = plume::CreateVulkanInterface();
    } else {
        renderInterface = plume::CreateD3D12Interface();
    }
#else
    renderInterface = plume::CreateVulkanInterface();
#endif
    if (!renderInterface) { fprintf(stderr, "Error: Failed to create render interface\n"); return 1; }
    auto device = renderInterface->createDevice();
    if (!device) { fprintf(stderr, "Error: Failed to create render device\n"); return 1; }
    auto commandQueue = device->createCommandQueue(plume::RenderCommandListType::DIRECT);
    if (!commandQueue) { fprintf(stderr, "Error: Failed to create command queue\n"); return 1; }

    printf("Device: %s\n", device->getDescription().name.c_str());
    auto shaderFormat = renderInterface->getCapabilities().shaderFormat;

#ifdef PLUME_TEST_HAS_SHADERS
    {
        auto cs = PLUME_SELECT_SHADER(device.get(), shaderFormat, "CSMain", ComputeFill);
        printf("HLSL pipeline: compute shader load %s\n", cs ? "OK" : "FAILED");
        if (!cs) { return 1; }
    }
#endif

    using namespace plume::bench;
    WorkloadSuite suite;
    suite.name = "graphics";
    suite.workloads.push_back(createClearColorWorkload(device.get(), commandQueue.get()));
    suite.workloads.push_back(createBufferCopyWorkload(device.get(), commandQueue.get()));
    suite.workloads.push_back(createBufferUploadWorkload(device.get(), commandQueue.get()));
    suite.workloads.push_back(createTextureOffsetReadbackWorkload(device.get(), commandQueue.get()));

    WorkloadSuite allocSuite;
    allocSuite.name = "allocations";
    allocSuite.workloads.push_back(createBufferAllocWorkload(device.get()));

#ifdef PLUME_TEST_HAS_SHADERS
    WorkloadSuite shaderSuite;
    shaderSuite.name = "shaders";
    shaderSuite.workloads.push_back(createPipelineCreateWorkload(device.get(), shaderFormat));
    shaderSuite.workloads.push_back(createFrameMacroWorkload(device.get(), commandQueue.get(), shaderFormat));
    shaderSuite.workloads.push_back(createIndexedDrawWorkload(device.get(), commandQueue.get(), shaderFormat));
    shaderSuite.workloads.push_back(createDepthTestWorkload(device.get(), commandQueue.get(), shaderFormat));
    shaderSuite.workloads.push_back(createBlendWorkload(device.get(), commandQueue.get(), shaderFormat));
    shaderSuite.workloads.push_back(createCullModeWorkload(device.get(), commandQueue.get(), shaderFormat));
    shaderSuite.workloads.push_back(createComputeDispatchWorkload(device.get(), commandQueue.get(), shaderFormat));
    shaderSuite.workloads.push_back(createImageStoreWorkload(device.get(), commandQueue.get(), shaderFormat));
    shaderSuite.workloads.push_back(createDescriptorDrawWorkload(device.get(), commandQueue.get(), shaderFormat));
    shaderSuite.workloads.push_back(createTextureSampleWorkload(device.get(), commandQueue.get(), shaderFormat));
    shaderSuite.workloads.push_back(createComputeBarrierWorkload(device.get(), commandQueue.get(), shaderFormat));
    shaderSuite.workloads.push_back(createFormatR8UnormWorkload(device.get(), commandQueue.get(), shaderFormat));
    shaderSuite.workloads.push_back(createFormatRgba16fWorkload(device.get(), commandQueue.get(), shaderFormat));
    shaderSuite.workloads.push_back(createSamplerLinearWorkload(device.get(), commandQueue.get(), shaderFormat));
    shaderSuite.workloads.push_back(createTextureMipWorkload(device.get(), commandQueue.get(), shaderFormat));
    shaderSuite.workloads.push_back(createMrtWorkload(device.get(), commandQueue.get(), shaderFormat));
    shaderSuite.workloads.push_back(createInstancedWorkload(device.get(), commandQueue.get(), shaderFormat));
    shaderSuite.workloads.push_back(createStructuredBufferReadWorkload(device.get(), commandQueue.get(), shaderFormat));
    shaderSuite.workloads.push_back(createMultiDescriptorSetWorkload(device.get(), commandQueue.get(), shaderFormat));
    shaderSuite.workloads.push_back(createMsaaResolveWorkload(device.get(), commandQueue.get(), shaderFormat));
    shaderSuite.workloads.push_back(createMsaaCoverageWorkload(device.get(), commandQueue.get(), shaderFormat));
    shaderSuite.workloads.push_back(createStencilMaskWorkload(device.get(), commandQueue.get(), shaderFormat));
    shaderSuite.workloads.push_back(createStencilIncrementWorkload(device.get(), commandQueue.get(), shaderFormat));
#endif

    int failures = runCorrectnessSuite(device.get(), commandQueue.get(), createHarnessSuite());
    failures += runCorrectnessSuite(device.get(), commandQueue.get(), suite);
    failures += runCorrectnessSuite(device.get(), commandQueue.get(), allocSuite);
#ifdef PLUME_TEST_HAS_SHADERS
    failures += runCorrectnessSuite(device.get(), commandQueue.get(), shaderSuite);
#endif

    printf("\n%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
