//
// plume bench
//

#pragma once

#include "bench.h"
#include "plume_render_interface.h"

namespace plume::bench {

    inline BenchSuite createAllocationSuite(RenderDevice *device) {
        BenchSuite suite;
        suite.name = "allocations";

        suite.cases.push_back({
            .name = "buffer_create_small_upload_64B",
            .config = { .warmupIterations = 10, .measuredIterations = 50, .innerLoopCount = 20 },
            .fn = [device]() {
                RenderBufferDesc desc;
                desc.size = 64;
                desc.heapType = RenderHeapType::UPLOAD;
                auto buf = device->createBuffer(desc);
            }
        });

        suite.cases.push_back({
            .name = "buffer_create_medium_private_64KB",
            .config = { .warmupIterations = 10, .measuredIterations = 50, .innerLoopCount = 10 },
            .fn = [device]() {
                auto buf = device->createBuffer(RenderBufferDesc::DefaultBuffer(64 * 1024));
            }
        });

        suite.cases.push_back({
            .name = "buffer_create_large_private_4MB",
            .config = { .warmupIterations = 5, .measuredIterations = 30, .innerLoopCount = 3 },
            .fn = [device]() {
                auto buf = device->createBuffer(RenderBufferDesc::DefaultBuffer(4 * 1024 * 1024));
            }
        });

        suite.cases.push_back({
            .name = "buffer_create_device_addressable_4KB",
            .config = { .warmupIterations = 10, .measuredIterations = 50, .innerLoopCount = 10 },
            .fn = [device]() {
                auto buf = device->createBuffer(
                    RenderBufferDesc::DefaultBuffer(4096, RenderBufferFlag::DEVICE_ADDRESSABLE));
            }
        });

        suite.cases.push_back({
            .name = "texture_create_256x256_RGBA8",
            .config = { .warmupIterations = 5, .measuredIterations = 30, .innerLoopCount = 5 },
            .fn = [device]() {
                RenderTextureDesc desc;
                desc.dimension = RenderTextureDimension::TEXTURE_2D;
                desc.width = 256;
                desc.height = 256;
                desc.depth = 1;
                desc.mipLevels = 1;
                desc.arraySize = 1;
                desc.format = RenderFormat::R8G8B8A8_UNORM;
                desc.flags = RenderTextureFlag::RENDER_TARGET;
                auto tex = device->createTexture(desc);
            }
        });

        suite.cases.push_back({
            .name = "texture_create_2048x2048_RGBA8",
            .config = { .warmupIterations = 3, .measuredIterations = 20, .innerLoopCount = 2 },
            .fn = [device]() {
                RenderTextureDesc desc;
                desc.dimension = RenderTextureDimension::TEXTURE_2D;
                desc.width = 2048;
                desc.height = 2048;
                desc.depth = 1;
                desc.mipLevels = 1;
                desc.arraySize = 1;
                desc.format = RenderFormat::R8G8B8A8_UNORM;
                desc.flags = RenderTextureFlag::RENDER_TARGET;
                auto tex = device->createTexture(desc);
            }
        });

        suite.cases.push_back({
            .name = "buffer_batch_create_destroy_100x4KB",
            .config = { .warmupIterations = 5, .measuredIterations = 50, .innerLoopCount = 1 },
            .fn = [device]() {
                std::vector<std::unique_ptr<RenderBuffer>> buffers;
                buffers.reserve(100);
                for (int i = 0; i < 100; i++) {
                    buffers.push_back(device->createBuffer(
                        RenderBufferDesc::DefaultBuffer(4096)));
                }
                buffers.clear();
            }
        });

        return suite;
    }

}
