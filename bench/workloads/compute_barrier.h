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
#include "shaders/ComputeIncrement.hlsl.metal.h"
#endif
#include "shaders/ComputeFill.hlsl.spirv.h"
#include "shaders/ComputeIncrement.hlsl.spirv.h"
#ifdef _WIN32
#include "shaders/ComputeFill.hlsl.dxil.h"
#include "shaders/ComputeIncrement.hlsl.dxil.h"
#endif

#include <cstdint>
#include <cstdio>
#include <memory>

namespace plume::bench {

    // Dispatch A writes Buf[i]=i*2+1; barrier; dispatch B does Buf[i]+=1. The barrier
    // must order them, so the readback is i*2+2. Validates UAV->UAV barrier ordering.
    inline Workload createComputeBarrierWorkload(RenderDevice *device, RenderCommandQueue *queue, RenderShaderFormat fmt) {
        struct Fixture {
            RenderDevice *device;
            std::unique_ptr<RenderShader> writeShader;
            std::unique_ptr<RenderShader> incrShader;
            std::unique_ptr<RenderPipelineLayout> layout;
            std::unique_ptr<RenderPipeline> writePipe;
            std::unique_ptr<RenderPipeline> incrPipe;
            std::unique_ptr<RenderDescriptorSet> set;
            std::unique_ptr<RenderBuffer> buf;
            std::unique_ptr<RenderBuffer> readback;
        };
        auto fx = std::make_shared<Fixture>();
        fx->device = device;

        static constexpr uint32_t COUNT = 256;
        static constexpr uint32_t BYTES = COUNT * 4;
        static constexpr uint32_t GROUP = 64;

        Workload w;
        w.name = "compute_barrier_256";

        w.setup = [fx, fmt]() {
            fx->writeShader = PLUME_SELECT_SHADER(fx->device, fmt, "CSMain", ComputeFill);
            fx->incrShader = PLUME_SELECT_SHADER(fx->device, fmt, "CSMain", ComputeIncrement);

            RenderDescriptorSetBuilder b;
            b.begin();
            b.addReadWriteStructuredBuffer(0);
            b.end();
            fx->set = fx->device->createDescriptorSet(b.descriptorSetDesc);

            RenderPipelineLayoutDesc layoutDesc;
            layoutDesc.descriptorSetDescs = &b.descriptorSetDesc;
            layoutDesc.descriptorSetDescsCount = 1;
            fx->layout = fx->device->createPipelineLayout(layoutDesc);

            RenderComputePipelineDesc wp(fx->layout.get(), fx->writeShader.get(), GROUP, 1, 1);
            fx->writePipe = fx->device->createComputePipeline(wp);
            RenderComputePipelineDesc ip(fx->layout.get(), fx->incrShader.get(), GROUP, 1, 1);
            fx->incrPipe = fx->device->createComputePipeline(ip);

            fx->buf = fx->device->createBuffer(
                RenderBufferDesc::DefaultBuffer(BYTES, RenderBufferFlag::UNORDERED_ACCESS));
            fx->readback = fx->device->createBuffer(RenderBufferDesc::ReadbackBuffer(BYTES));
            // D3D12 needs the structured-buffer stride at descriptor creation (RWStructuredBuffer<uint>).
            RenderBufferStructuredView bufView(sizeof(uint32_t));
            fx->set->setBuffer(0, fx->buf.get(), BYTES, &bufView);
        };

        w.encode = [fx](RenderCommandList *cmd) {
            cmd->setComputePipelineLayout(fx->layout.get());
            cmd->setComputeDescriptorSet(fx->set.get(), 0);

            cmd->barriers(RenderBarrierStage::COMPUTE,
                RenderBufferBarrier(fx->buf.get(), RenderBufferAccess::WRITE));
            cmd->setPipeline(fx->writePipe.get());
            cmd->dispatch(COUNT / GROUP, 1, 1);

            cmd->barriers(RenderBarrierStage::COMPUTE,
                RenderBufferBarrier(fx->buf.get(), RenderBufferAccess::WRITE));
            cmd->setPipeline(fx->incrPipe.get());
            cmd->dispatch(COUNT / GROUP, 1, 1);

            cmd->barriers(RenderBarrierStage::COPY,
                RenderBufferBarrier(fx->buf.get(), RenderBufferAccess::READ));
            cmd->copyBufferRegion(fx->readback->at(0), fx->buf->at(0), BYTES);
        };

        w.verify = [fx]() -> VerifyResult {
            auto *p = static_cast<const uint32_t *>(fx->readback->map());
            if (p == nullptr) {
                return VerifyResult::fail("readback map() returned null");
            }
            VerifyResult result = VerifyResult::ok();
            for (uint32_t i = 0; i < COUNT; i++) {
                uint32_t expected = i * 2u + 2u;
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
