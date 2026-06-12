//
// plume bench
//

#pragma once

#include "bench.h"
#include "plume_render_interface.h"
#include "plume_render_interface_builders.h"

#include <memory>
#include <vector>

namespace plume::bench {

    struct DescriptorFixture {
        RenderDevice *device = nullptr;
        std::vector<std::unique_ptr<RenderBuffer>> buffers;
        std::vector<std::unique_ptr<RenderTexture>> textures;
        std::unique_ptr<RenderSampler> sampler;

        void init(RenderDevice *dev) {
            device = dev;

            for (int i = 0; i < 64; i++) {
                buffers.push_back(device->createBuffer(
                    RenderBufferDesc::DefaultBuffer(256)));
            }

            for (int i = 0; i < 32; i++) {
                RenderTextureDesc desc;
                desc.dimension = RenderTextureDimension::TEXTURE_2D;
                desc.width = 64;
                desc.height = 64;
                desc.depth = 1;
                desc.mipLevels = 1;
                desc.arraySize = 1;
                desc.format = RenderFormat::R8G8B8A8_UNORM;
                textures.push_back(device->createTexture(desc));
            }

            RenderSamplerDesc samplerDesc;
            samplerDesc.minFilter = RenderFilter::LINEAR;
            samplerDesc.magFilter = RenderFilter::LINEAR;
            samplerDesc.mipmapMode = RenderMipmapMode::LINEAR;
            samplerDesc.addressU = RenderTextureAddressMode::WRAP;
            samplerDesc.addressV = RenderTextureAddressMode::WRAP;
            samplerDesc.addressW = RenderTextureAddressMode::WRAP;
            sampler = device->createSampler(samplerDesc);
        }
    };

    inline BenchSuite createDescriptorSuite(RenderDevice *device) {
        auto fixture = std::make_shared<DescriptorFixture>();
        fixture->init(device);

        BenchSuite suite;
        suite.name = "descriptors";

        suite.cases.push_back({
            .name = "descriptor_set_create_1_buffer",
            .config = { .warmupIterations = 10, .measuredIterations = 50, .innerLoopCount = 20 },
            .fn = [fixture]() {
                RenderDescriptorSetBuilder b;
                b.begin();
                b.addConstantBuffer(0);
                b.end();
                auto set = fixture->device->createDescriptorSet(b.descriptorSetDesc);
            }
        });

        suite.cases.push_back({
            .name = "descriptor_set_create_8_textures",
            .config = { .warmupIterations = 10, .measuredIterations = 50, .innerLoopCount = 10 },
            .fn = [fixture]() {
                RenderDescriptorSetBuilder b;
                b.begin();
                b.addTexture(0, 8);
                b.end();
                auto set = fixture->device->createDescriptorSet(b.descriptorSetDesc);
            }
        });

        suite.cases.push_back({
            .name = "descriptor_set_update_16_buffers",
            .config = { .warmupIterations = 10, .measuredIterations = 50, .innerLoopCount = 1 },
            .fn = [fixture]() {
                RenderDescriptorSetBuilder b;
                b.begin();
                b.addStructuredBuffer(0, 16);
                b.end();
                auto set = fixture->device->createDescriptorSet(b.descriptorSetDesc);
                for (int i = 0; i < 16; i++) {
                    set->setBuffer(i, fixture->buffers[i].get(), 256);
                }
            }
        });

        suite.cases.push_back({
            .name = "descriptor_set_update_16_textures",
            .config = { .warmupIterations = 10, .measuredIterations = 50, .innerLoopCount = 1 },
            .fn = [fixture]() {
                RenderDescriptorSetBuilder b;
                b.begin();
                b.addTexture(0, 16);
                b.end();
                auto set = fixture->device->createDescriptorSet(b.descriptorSetDesc);
                for (int i = 0; i < 16; i++) {
                    set->setTexture(i, fixture->textures[i % 32].get(), RenderTextureLayout::SHADER_READ);
                }
            }
        });

        suite.cases.push_back({
            .name = "descriptor_set_batch_50_create_destroy",
            .config = { .warmupIterations = 5, .measuredIterations = 30, .innerLoopCount = 1 },
            .fn = [fixture]() {
                std::vector<std::unique_ptr<RenderDescriptorSet>> sets;
                sets.reserve(50);
                for (int i = 0; i < 50; i++) {
                    RenderDescriptorSetBuilder b;
                    b.begin();
                    b.addTexture(0, 4);
                    b.end();
                    sets.push_back(fixture->device->createDescriptorSet(b.descriptorSetDesc));
                }
                for (int i = 0; i < 50; i++) {
                    for (int j = 0; j < 4; j++) {
                        sets[i]->setTexture(j, fixture->textures[j].get(), RenderTextureLayout::SHADER_READ);
                    }
                }
                sets.clear();
            }
        });

        return suite;
    }

}
