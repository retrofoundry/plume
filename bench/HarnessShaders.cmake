# Opt-in HLSL shader pipeline for the harness (bench + test) workloads.
#
# plume_harness_link_shaders(<target> <define>) creates the shared
# plume_compiled_shaders OBJECT library on first use and links it to <target>,
# setting <define>=1 so the target compiles its shader-dependent workloads.
# Both plume_bench and plume_test call this; the blobs are compiled once.
#
# Apple needs spirv-cross for the extra HLSL->SPIR-V->MSL step (opt-in via
# PLUME_SPIRV_CROSS_LIB_DIR); off Apple only DXC is needed (HLSL->SPIR-V for
# Vulkan, HLSL->DXIL for D3D12), which PlumeShaders fetches on every platform.
# When the toolchain is absent the macro is a no-op and the target builds with
# only the shader-free workloads.

macro(plume_harness_link_shaders target hasShadersDefine)
    if((APPLE AND DEFINED PLUME_SPIRV_CROSS_LIB_DIR) OR (NOT APPLE))
        if(NOT TARGET plume_compiled_shaders)
            include(${CMAKE_SOURCE_DIR}/examples/cmake/PlumeShaders.cmake)
            plume_shaders_init()
            add_library(plume_compiled_shaders OBJECT)
            plume_compile_compute_shader(plume_compiled_shaders
                ${CMAKE_SOURCE_DIR}/bench/shaders/compute_fill.hlsl ComputeFill CSMain)
            plume_compile_vertex_shader(plume_compiled_shaders
                ${CMAKE_SOURCE_DIR}/bench/shaders/descriptor_draw.hlsl DescriptorDrawVert VSMain)
            plume_compile_pixel_shader(plume_compiled_shaders
                ${CMAKE_SOURCE_DIR}/bench/shaders/descriptor_draw.hlsl DescriptorDrawFrag PSMain)
            plume_compile_vertex_shader(plume_compiled_shaders
                ${CMAKE_SOURCE_DIR}/bench/shaders/texture_sample.hlsl TextureSampleVert VSMain)
            plume_compile_pixel_shader(plume_compiled_shaders
                ${CMAKE_SOURCE_DIR}/bench/shaders/texture_sample.hlsl TextureSampleFrag PSMain)
            plume_compile_compute_shader(plume_compiled_shaders
                ${CMAKE_SOURCE_DIR}/bench/shaders/compute_increment.hlsl ComputeIncrement CSMain)
            plume_compile_vertex_shader(plume_compiled_shaders
                ${CMAKE_SOURCE_DIR}/bench/shaders/scene_color.hlsl SceneColorVert VSMain)
            plume_compile_pixel_shader(plume_compiled_shaders
                ${CMAKE_SOURCE_DIR}/bench/shaders/scene_color.hlsl SceneColorFrag PSMain)
            plume_compile_vertex_shader(plume_compiled_shaders
                ${CMAKE_SOURCE_DIR}/bench/shaders/solid_color.hlsl SolidColorVert VSMain)
            plume_compile_pixel_shader(plume_compiled_shaders
                ${CMAKE_SOURCE_DIR}/bench/shaders/solid_color.hlsl SolidColorFrag PSMain)
            plume_compile_vertex_shader(plume_compiled_shaders
                ${CMAKE_SOURCE_DIR}/bench/shaders/sample_level.hlsl SampleLevelVert VSMain)
            plume_compile_pixel_shader(plume_compiled_shaders
                ${CMAKE_SOURCE_DIR}/bench/shaders/sample_level.hlsl SampleLevelFrag PSMain)
            plume_compile_vertex_shader(plume_compiled_shaders
                ${CMAKE_SOURCE_DIR}/bench/shaders/mrt.hlsl MrtVert VSMain)
            plume_compile_pixel_shader(plume_compiled_shaders
                ${CMAKE_SOURCE_DIR}/bench/shaders/mrt.hlsl MrtFrag PSMain)
            plume_compile_vertex_shader(plume_compiled_shaders
                ${CMAKE_SOURCE_DIR}/bench/shaders/instanced.hlsl InstancedVert VSMain)
            plume_compile_pixel_shader(plume_compiled_shaders
                ${CMAKE_SOURCE_DIR}/bench/shaders/instanced.hlsl InstancedFrag PSMain)
            plume_compile_compute_shader(plume_compiled_shaders
                ${CMAKE_SOURCE_DIR}/bench/shaders/structured_read.hlsl StructuredRead CSMain)
            plume_compile_compute_shader(plume_compiled_shaders
                ${CMAKE_SOURCE_DIR}/bench/shaders/two_sets.hlsl TwoSets CSMain)
            plume_compile_compute_shader(plume_compiled_shaders
                ${CMAKE_SOURCE_DIR}/bench/shaders/image_store.hlsl ImageStore CSMain)
            # Propagate the generated-header directory to all consumers.
            set_property(TARGET plume_compiled_shaders APPEND PROPERTY
                INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_BINARY_DIR}")
        endif()
        target_link_libraries(${target} PRIVATE plume_compiled_shaders)
        target_compile_definitions(${target} PRIVATE ${hasShadersDefine}=1)
    endif()
endmacro()
