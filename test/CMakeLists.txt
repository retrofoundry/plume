add_executable(plume_test main.cpp)
target_link_libraries(plume_test PRIVATE plume)

target_include_directories(plume_test PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/bench
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(APPLE)
    target_link_libraries(plume_test PRIVATE
        "-framework Metal -framework QuartzCore -framework CoreGraphics -framework Foundation -framework IOKit")
elseif(WIN32)
    target_link_libraries(plume_test PRIVATE d3d12 dxgi)
elseif(CMAKE_SYSTEM_NAME MATCHES "Linux")
    find_package(X11 REQUIRED)
    target_include_directories(plume_test PUBLIC ${X11_INCLUDE_DIR})
    target_link_libraries(plume_test PRIVATE ${X11_LIBRARIES})
endif()

set_target_properties(plume_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

include(${CMAKE_SOURCE_DIR}/bench/HarnessShaders.cmake)
plume_harness_link_shaders(plume_test PLUME_TEST_HAS_SHADERS)
