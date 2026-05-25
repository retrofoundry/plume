//
// plume bench
//

#pragma once

#include "workload.h"
#include "plume_render_interface.h"
#include "plume_render_interface_builders.h"

#include "shader_select.h"
#ifdef __APPLE__
#include "shaders/TwoSets.hlsl.metal.h"
#endif
#include "shaders/TwoSets.hlsl.spirv.h"
#ifdef _WIN32
#include "shaders/TwoSets.hlsl.dxil.h"
#endif

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>

namespace plume::bench {

    // Dispatches a kernel that reads a cbuffer from descriptor set 0 and another from
    // set 1, summing them into a UAV in set 0. Reads back and verifies the sum.
    // Exercises binding multiple descriptor sets (setIndex 0 and 1). If set-1 binding is
    // broken, colorB is missing and the sum is wrong.
    inline Workload createMultiDescriptorSetWorkload(RenderDevice *device, RenderCommandQueue *queue, RenderShaderFormat fmt) {
        struct Fixture {
            RenderDevice *device;
            std::unique_ptr<RenderShader> shader;
            std::unique_ptr<RenderPipelineLayout> layout;
            std::unique_ptr<RenderPipeline> pipeline;
            std::unique_ptr<RenderDescriptorSet> set0;
            std::unique_ptr<RenderDescriptorSet> set1;
            std::unique_ptr<RenderBuffer> constantsA;
            std::unique_ptr<RenderBuffer> constantsB;
            std::unique_ptr<RenderBuffer> output;
            std::unique_ptr<RenderBuffer> readback;
        };
        auto fx = std::make_shared<Fixture>();
        fx->device = device;

        static constexpr uint32_t BYTES = sizeof(float) * 4;
        const float COLOR_A[4] = { 0.1f, 0.2f, 0.3f, 0.4f };
        const float COLOR_B[4] = { 0.5f, 0.5f, 0.5f, 0.5f };
        const float EXPECTED[4] = { 0.6f, 0.7f, 0.8f, 0.9f };

        Workload w;
        w.name = "multi_descriptor_set";

        w.setup = [fx, fmt, COLOR_A, COLOR_B]() {
            fx->shader = PLUME_SELECT_SHADER(fx->device, fmt, "CSMain", TwoSets);

            // Set 0: cbuffer b0 + UAV u1.
            RenderDescriptorSetBuilder b0;
            b0.begin();
            b0.addConstantBuffer(0);
            b0.addReadWriteStructuredBuffer(1);
            b0.end();
            fx->set0 = fx->device->createDescriptorSet(b0.descriptorSetDesc);

            // Set 1: cbuffer b0 (space1).
            RenderDescriptorSetBuilder b1;
            b1.begin();
            b1.addConstantBuffer(0);
            b1.end();
            fx->set1 = fx->device->createDescriptorSet(b1.descriptorSetDesc);

            RenderDescriptorSetDesc setDescs[2] = { b0.descriptorSetDesc, b1.descriptorSetDesc };
            RenderPipelineLayoutDesc layoutDesc;
            layoutDesc.descriptorSetDescs = setDescs;
            layoutDesc.descriptorSetDescsCount = 2;
            fx->layout = fx->device->createPipelineLayout(layoutDesc);

            RenderComputePipelineDesc pipeDesc(fx->layout.get(), fx->shader.get(), 1, 1, 1);
            fx->pipeline = fx->device->createComputePipeline(pipeDesc);

            fx->constantsA = fx->device->createBuffer(RenderBufferDesc::UploadBuffer(BYTES));
            std::memcpy(fx->constantsA->map(), COLOR_A, BYTES);
            fx->constantsA->unmap();

            fx->constantsB = fx->device->createBuffer(RenderBufferDesc::UploadBuffer(BYTES));
            std::memcpy(fx->constantsB->map(), COLOR_B, BYTES);
            fx->constantsB->unmap();

            fx->output = fx->device->createBuffer(
                RenderBufferDesc::DefaultBuffer(BYTES, RenderBufferFlag::UNORDERED_ACCESS));
            fx->readback = fx->device->createBuffer(RenderBufferDesc::ReadbackBuffer(BYTES));

            // D3D12 needs the structured-buffer stride at descriptor creation (RWStructuredBuffer<float4>).
            RenderBufferStructuredView outputView(sizeof(float) * 4);
            fx->set0->setBuffer(0, fx->constantsA.get(), BYTES);
            fx->set0->setBuffer(1, fx->output.get(), BYTES, &outputView);
            fx->set1->setBuffer(0, fx->constantsB.get(), BYTES);
        };

        w.encode = [fx](RenderCommandList *cmd) {
            cmd->barriers(RenderBarrierStage::COMPUTE,
                RenderBufferBarrier(fx->output.get(), RenderBufferAccess::WRITE));
            cmd->setComputePipelineLayout(fx->layout.get());
            cmd->setPipeline(fx->pipeline.get());
            cmd->setComputeDescriptorSet(fx->set0.get(), 0);
            cmd->setComputeDescriptorSet(fx->set1.get(), 1);
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
