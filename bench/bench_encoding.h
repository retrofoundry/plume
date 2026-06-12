//
// plume bench
//

#pragma once

#include "bench.h"
#include "plume_render_interface.h"

#ifdef PLUME_BENCH_HAS_SHADERS
#include "shader_select.h"
#ifdef __APPLE__
#include "shaders/SceneColorVert.hlsl.metal.h"
#include "shaders/SceneColorFrag.hlsl.metal.h"
#endif
#include "shaders/SceneColorVert.hlsl.spirv.h"
#include "shaders/SceneColorFrag.hlsl.spirv.h"
#endif

#include <memory>

namespace plume::bench {

    struct EncodingFixture {
        RenderDevice *device = nullptr;
        RenderCommandQueue *queue = nullptr;
        std::unique_ptr<RenderTexture> renderTarget;
        std::unique_ptr<RenderTexture> depthTarget;
        std::unique_ptr<RenderFramebuffer> framebuffer;
        std::unique_ptr<RenderPipelineLayout> pipelineLayout;
        std::unique_ptr<RenderPipeline> pipeline;
        std::unique_ptr<RenderBuffer> vertexBuffer;
        RenderInputSlot inputSlot;

        void init(RenderDevice *dev, RenderCommandQueue *q, RenderShaderFormat fmt) {
            device = dev;
            queue = q;

            RenderTextureDesc rtDesc;
            rtDesc.dimension = RenderTextureDimension::TEXTURE_2D;
            rtDesc.width = 1920;
            rtDesc.height = 1080;
            rtDesc.depth = 1;
            rtDesc.mipLevels = 1;
            rtDesc.arraySize = 1;
            rtDesc.format = RenderFormat::R8G8B8A8_UNORM;
            rtDesc.flags = RenderTextureFlag::RENDER_TARGET;
            renderTarget = device->createTexture(rtDesc);

            RenderTextureDesc depthDesc;
            depthDesc.dimension = RenderTextureDimension::TEXTURE_2D;
            depthDesc.width = 1920;
            depthDesc.height = 1080;
            depthDesc.depth = 1;
            depthDesc.mipLevels = 1;
            depthDesc.arraySize = 1;
            depthDesc.format = RenderFormat::D32_FLOAT;
            depthDesc.flags = RenderTextureFlag::DEPTH_TARGET;
            depthTarget = device->createTexture(depthDesc);

            const RenderTexture *colorAttachment = renderTarget.get();
            RenderFramebufferDesc fbDesc;
            fbDesc.colorAttachments = &colorAttachment;
            fbDesc.colorAttachmentsCount = 1;
            fbDesc.depthAttachment = depthTarget.get();
            framebuffer = device->createFramebuffer(fbDesc);

            inputSlot = RenderInputSlot(0, sizeof(float) * 7);

#ifdef PLUME_BENCH_HAS_SHADERS
            RenderPipelineLayoutDesc layoutDesc;
            layoutDesc.allowInputLayout = true;
            pipelineLayout = device->createPipelineLayout(layoutDesc);

            auto vertexShader = PLUME_SELECT_SHADER(device, fmt, "VSMain", SceneColorVert);
            auto fragmentShader = PLUME_SELECT_SHADER(device, fmt, "PSMain", SceneColorFrag);

            RenderInputElement inputElements[] = {
                RenderInputElement("POSITION", 0, 0, RenderFormat::R32G32B32_FLOAT, 0, 0),
                RenderInputElement("COLOR", 0, 1, RenderFormat::R32G32B32A32_FLOAT, 0, sizeof(float) * 3)
            };

            RenderGraphicsPipelineDesc pipelineDesc;
            pipelineDesc.inputSlots = &inputSlot;
            pipelineDesc.inputSlotsCount = 1;
            pipelineDesc.inputElements = inputElements;
            pipelineDesc.inputElementsCount = 2;
            pipelineDesc.pipelineLayout = pipelineLayout.get();
            pipelineDesc.vertexShader = vertexShader.get();
            pipelineDesc.pixelShader = fragmentShader.get();
            pipelineDesc.renderTargetFormat[0] = RenderFormat::R8G8B8A8_UNORM;
            pipelineDesc.renderTargetBlend[0] = RenderBlendDesc::Copy();
            pipelineDesc.renderTargetCount = 1;
            pipelineDesc.primitiveTopology = RenderPrimitiveTopology::TRIANGLE_LIST;
            pipeline = device->createGraphicsPipeline(pipelineDesc);

            vertexBuffer = device->createBuffer(
                RenderBufferDesc::VertexBuffer(4096, RenderHeapType::UPLOAD));
#else
            (void)fmt;
#endif
        }
    };

    inline BenchSuite createEncodingSuite(RenderDevice *device, RenderCommandQueue *queue, RenderShaderFormat fmt) {
        auto fixture = std::make_shared<EncodingFixture>();
        fixture->init(device, queue, fmt);

        BenchSuite suite;
        suite.name = "encoding";

        suite.cases.push_back({
            .name = "cmdlist_create_begin_end",
            .config = { .warmupIterations = 5, .measuredIterations = 30, .innerLoopCount = 20 },
            .fn = [fixture]() {
                auto cmdList = fixture->queue->createCommandList();
                cmdList->begin();
                cmdList->end();
            }
        });

        suite.cases.push_back({
            .name = "cmdlist_set_framebuffer_clear",
            .config = { .warmupIterations = 5, .measuredIterations = 30, .innerLoopCount = 1 },
            .fn = [fixture]() {
                auto cmdList = fixture->queue->createCommandList();
                cmdList->begin();
                cmdList->barriers(RenderBarrierStage::GRAPHICS,
                    RenderTextureBarrier(fixture->renderTarget.get(), RenderTextureLayout::COLOR_WRITE));
                cmdList->setFramebuffer(fixture->framebuffer.get());
                cmdList->clearColor(0, RenderColor(0.0f, 0.0f, 0.0f, 1.0f));
                cmdList->clearDepth(true, 1.0f);
                cmdList->end();
            }
        });

        suite.cases.push_back({
            .name = "cmdlist_viewport_scissor_100x",
            .config = { .warmupIterations = 5, .measuredIterations = 30, .innerLoopCount = 1 },
            .fn = [fixture]() {
                auto cmdList = fixture->queue->createCommandList();
                cmdList->begin();
                cmdList->barriers(RenderBarrierStage::GRAPHICS,
                    RenderTextureBarrier(fixture->renderTarget.get(), RenderTextureLayout::COLOR_WRITE));
                cmdList->setFramebuffer(fixture->framebuffer.get());

                for (int i = 0; i < 100; i++) {
                    RenderViewport vp(0.0f, 0.0f, 1920.0f, 1080.0f);
                    RenderRect sc(0, 0, 1920, 1080);
                    cmdList->setViewports(vp);
                    cmdList->setScissors(sc);
                }
                cmdList->end();
            }
        });

#ifdef PLUME_BENCH_HAS_SHADERS
        suite.cases.push_back({
            .name = "cmdlist_draw_instanced_1000x",
            .config = { .warmupIterations = 3, .measuredIterations = 20, .innerLoopCount = 1 },
            .fn = [fixture]() {
                auto cmdList = fixture->queue->createCommandList();
                cmdList->begin();
                cmdList->barriers(RenderBarrierStage::GRAPHICS,
                    RenderTextureBarrier(fixture->renderTarget.get(), RenderTextureLayout::COLOR_WRITE));
                cmdList->setFramebuffer(fixture->framebuffer.get());
                cmdList->setGraphicsPipelineLayout(fixture->pipelineLayout.get());
                cmdList->setPipeline(fixture->pipeline.get());

                RenderViewport vp(0.0f, 0.0f, 1920.0f, 1080.0f);
                RenderRect sc(0, 0, 1920, 1080);
                cmdList->setViewports(vp);
                cmdList->setScissors(sc);

                RenderVertexBufferView vbView(fixture->vertexBuffer.get(), 4096);
                cmdList->setVertexBuffers(0, &vbView, 1, &fixture->inputSlot);

                for (int i = 0; i < 1000; i++) {
                    cmdList->drawInstanced(3, 1, 0, 0);
                }
                cmdList->end();
            }
        });
#endif

        suite.cases.push_back({
            .name = "cmdlist_barriers_100",
            .config = { .warmupIterations = 5, .measuredIterations = 30, .innerLoopCount = 1 },
            .fn = [fixture]() {
                auto cmdList = fixture->queue->createCommandList();
                cmdList->begin();
                for (int i = 0; i < 50; i++) {
                    cmdList->barriers(RenderBarrierStage::GRAPHICS,
                        RenderTextureBarrier(fixture->renderTarget.get(), RenderTextureLayout::COLOR_WRITE));
                    cmdList->barriers(RenderBarrierStage::COMPUTE,
                        RenderTextureBarrier(fixture->renderTarget.get(), RenderTextureLayout::SHADER_READ));
                }
                cmdList->end();
            }
        });

        return suite;
    }

}
