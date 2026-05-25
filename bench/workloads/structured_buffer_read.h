//
// plume bench
//

#pragma once

#include "workload.h"
#include "plume_render_interface.h"
#include "plume_render_interface_builders.h"

#include "shader_select.h"
#ifdef __APPLE__
#include "shaders/StructuredRead.hlsl.metal.h"
#endif
#include "shaders/StructuredRead.hlsl.spirv.h"
#ifdef _WIN32
#include "shaders/StructuredRead.hlsl.dxil.h"
#endif

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>

namespace plume::bench {

    // Dispatches a kernel that reads a StructuredBuffer<float4> SRV and writes 2x its
    // value to a UAV; reads back and verifies. Exercises structured-buffer SRV reads.
    inline Workload createStructuredBufferReadWorkload(RenderDevice *device, RenderCommandQueue *queue, RenderShaderFormat fmt) {
        struct Fixture {
            RenderDevice *device;
            std::unique_ptr<RenderShader> shader;
            std::unique_ptr<RenderPipelineLayout> layout;
            std::unique_ptr<RenderPipeline> pipeline;
            std::unique_ptr<RenderDescriptorSet> set;
            std::unique_ptr<RenderBuffer> input;
            std::unique_ptr<RenderBuffer> output;
            std::unique_ptr<RenderBuffer> readback;
        };
        auto fx = std::make_shared<Fixture>();
        fx->device = device;

        static constexpr uint32_t BYTES = sizeof(float) * 4;
        const float INPUT[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
        const float EXPECTED[4] = { 2.0f, 4.0f, 6.0f, 8.0f };

        Workload w;
        w.name = "structured_buffer_read";

        w.setup = [fx, fmt, INPUT]() {
            fx->shader = PLUME_SELECT_SHADER(fx->device, fmt, "CSMain", StructuredRead);

            RenderDescriptorSetBuilder b;
            b.begin();
            b.addStructuredBuffer(0);          // register(t0)
            b.addReadWriteStructuredBuffer(1); // register(u1)
            b.end();
            fx->set = fx->device->createDescriptorSet(b.descriptorSetDesc);

            RenderPipelineLayoutDesc layoutDesc;
            layoutDesc.descriptorSetDescs = &b.descriptorSetDesc;
            layoutDesc.descriptorSetDescsCount = 1;
            fx->layout = fx->device->createPipelineLayout(layoutDesc);

            RenderComputePipelineDesc pipeDesc(fx->layout.get(), fx->shader.get(), 1, 1, 1);
            fx->pipeline = fx->device->createComputePipeline(pipeDesc);

            fx->input = fx->device->createBuffer(RenderBufferDesc::UploadBuffer(BYTES));
            std::memcpy(fx->input->map(), INPUT, BYTES);
            fx->input->unmap();

            fx->output = fx->device->createBuffer(
                RenderBufferDesc::DefaultBuffer(BYTES, RenderBufferFlag::UNORDERED_ACCESS));
            fx->readback = fx->device->createBuffer(RenderBufferDesc::ReadbackBuffer(BYTES));

            RenderBufferStructuredView inputView(sizeof(float) * 4);
            RenderBufferStructuredView outputView(sizeof(float) * 4);
            fx->set->setBuffer(0, fx->input.get(), BYTES, &inputView);
            fx->set->setBuffer(1, fx->output.get(), BYTES, &outputView);
        };

        w.encode = [fx](RenderCommandList *cmd) {
            cmd->barriers(RenderBarrierStage::COMPUTE,
                RenderBufferBarrier(fx->output.get(), RenderBufferAccess::WRITE));
            cmd->setComputePipelineLayout(fx->layout.get());
            cmd->setPipeline(fx->pipeline.get());
            cmd->setComputeDescriptorSet(fx->set.get(), 0);
            cmd->dispatch(1, 1, 1);

            cmd->barriers(RenderBarrierStage::COPY,
                RenderBufferBarrier(fx->output.get(), RenderBufferAccess::READ));
            cmd->copyBufferRegion(fx->readback->at(0), fx->output->at(0), BYTES);
        };

        w.verify = [fx, EXPECTED]() -> VerifyResult {
            auto *p = static_cast<const float *>(fx->readback->map());
            if (p == nullptr) {
                return VerifyResult::fail("readback map() returned null");
            }
            VerifyResult result = VerifyResult::ok();
            for (int i = 0; i < 4; i++) {
                float d = p[i] > EXPECTED[i] ? p[i] - EXPECTED[i] : EXPECTED[i] - p[i];
                if (d > 0.001f) {
                    char buf[96];
                    snprintf(buf, sizeof(buf), "element %d = %f, expected %f", i, p[i], EXPECTED[i]);
                    result = VerifyResult::fail(buf);
                    break;
                }
            }
            fx->readback->unmap();
            return result;
        };

        return w;
    }

}
