add_executable(plume_bench main.cpp)
target_link_libraries(plume_bench PRIVATE plume)

target_include_directories(plume_bench PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(APPLE)
    target_link_libraries(plume_bench PRIVATE
        "-framework Metal -framework QuartzCore -framework CoreGraphics -framework Foundation -framework IOKit")
elseif(WIN32)
    target_link_libraries(plume_bench PRIVATE d3d12 dxgi)
elseif(CMAKE_SYSTEM_NAME MATCHES "Linux")
    find_package(X11 REQUIRED)
    target_include_directories(plume_bench PUBLIC ${X11_INCLUDE_DIR})
    target_link_libraries(plume_bench PRIVATE ${X11_LIBRARIES})
endif()

set_target_properties(plume_bench PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

include(${CMAKE_SOURCE_DIR}/bench/HarnessShaders.cmake)
plume_harness_link_shaders(plume_bench PLUME_BENCH_HAS_SHADERS)
