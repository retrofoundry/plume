//
// plume bench
//

#pragma once

#include "workload.h"
#include "plume_render_interface.h"
#include "plume_render_interface_builders.h"

#include "shader_select.h"
#ifdef __APPLE__
#include "shaders/ComputeFill.hlsl.metal.h"
#endif
#include "shaders/ComputeFill.hlsl.spirv.h"
#ifdef _WIN32
#include "shaders/ComputeFill.hlsl.dxil.h"
#endif

#include <cstdint>
#include <cstdio>
#include <memory>

namespace plume::bench {

    // Dispatches the compute_fill kernel (Output[i] = i*2+1), reads the buffer back,
    // and verifies. First end-to-end check of compute descriptor (UAV) binding.
    inline Workload createComputeDispatchWorkload(RenderDevice *device, RenderCommandQueue *queue, RenderShaderFormat fmt) {
        struct Fixture {
            RenderDevice *device;
            std::unique_ptr<RenderShader> shader;
            std::unique_ptr<RenderPipelineLayout> layout;
            std::unique_ptr<RenderPipeline> pipeline;
            std::unique_ptr<RenderDescriptorSet> set;
            std::unique_ptr<RenderBuffer> output;
            std::unique_ptr<RenderBuffer> readback;
        };
        auto fx = std::make_shared<Fixture>();
        fx->device = device;

        static constexpr uint32_t COUNT = 256;
        static constexpr uint32_t BYTES = COUNT * 4;
        static constexpr uint32_t GROUP = 64; // matches [numthreads(64,1,1)]

        Workload w;
        w.name = "compute_dispatch_256";

        w.setup = [fx, fmt]() {
            fx->shader = PLUME_SELECT_SHADER(fx->device, fmt, "CSMain", ComputeFill);

            RenderDescriptorSetBuilder b;
            b.begin();
            b.addReadWriteStructuredBuffer(0); // register(u0)
            b.end();
            fx->set = fx->device->createDescriptorSet(b.descriptorSetDesc);

            RenderPipelineLayoutDesc layoutDesc;
            layoutDesc.descriptorSetDescs = &b.descriptorSetDesc;
            layoutDesc.descriptorSetDescsCount = 1;
            fx->layout = fx->device->createPipelineLayout(layoutDesc);

            RenderComputePipelineDesc pipeDesc(fx->layout.get(), fx->shader.get(), GROUP, 1, 1);
            fx->pipeline = fx->device->createComputePipeline(pipeDesc);

            fx->output = fx->device->createBuffer(
                RenderBufferDesc::DefaultBuffer(BYTES, RenderBufferFlag::UNORDERED_ACCESS));
            fx->readback = fx->device->createBuffer(RenderBufferDesc::ReadbackBuffer(BYTES));
            // D3D12 needs the structured-buffer stride at descriptor creation (RWStructuredBuffer<uint>).
            RenderBufferStructuredView outputView(sizeof(uint32_t));
            fx->set->setBuffer(0, fx->output.get(), BYTES, &outputView);
        };

        w.encode = [fx](RenderCommandList *cmd) {
            cmd->barriers(RenderBarrierStage::COMPUTE,
                RenderBufferBarrier(fx->output.get(), RenderBufferAccess::WRITE));
            cmd->setComputePipelineLayout(fx->layout.get());
            cmd->setPipeline(fx->pipeline.get());
            cmd->setComputeDescriptorSet(fx->set.get(), 0);
            cmd->dispatch(COUNT / GROUP, 1, 1);

            cmd->barriers(RenderBarrierStage::COPY,
                RenderBufferBarrier(fx->output.get(), RenderBufferAccess::READ));
            cmd->copyBufferRegion(fx->readback->at(0), fx->output->at(0), BYTES);
        };

        w.verify = [fx]() -> VerifyResult {
            auto *p = static_cast<const uint32_t *>(fx->readback->map());
            if (p == nullptr) {
                return VerifyResult::fail("readback map() returned null");
            }
            VerifyResult result = VerifyResult::ok();
            for (uint32_t i = 0; i < COUNT; i++) {
                uint32_t expected = i * 2u + 1u;
                if (p[i] != expected) {
                    char buf[96];
                    snprintf(buf, sizeof(buf), "element %u = %u, expected %u", i, p[i], expected);
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
