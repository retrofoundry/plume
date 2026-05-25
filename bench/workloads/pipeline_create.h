//
// plume bench
//

#pragma once

#include "workload.h"
#include "plume_render_interface.h"

#include "shader_select.h"
#ifdef __APPLE__
#include "shaders/SceneColorVert.hlsl.metal.h"
#include "shaders/SceneColorFrag.hlsl.metal.h"
#endif
#include "shaders/SceneColorVert.hlsl.spirv.h"
#include "shaders/SceneColorFrag.hlsl.spirv.h"
#ifdef _WIN32
#include "shaders/SceneColorVert.hlsl.dxil.h"
#include "shaders/SceneColorFrag.hlsl.dxil.h"
#endif

#include <memory>

namespace plume::bench {

    // Builds a graphics pipeline each iteration; the most-optimized RHI path.
    // Shaders/layout/input description are created once; only createGraphicsPipeline is timed.
    inline Workload createPipelineCreateWorkload(RenderDevice *device, RenderShaderFormat fmt) {
        struct Fixture {
            RenderDevice *device;
            std::unique_ptr<RenderShader> vertexShader;
            std::unique_ptr<RenderShader> pixelShader;
            std::unique_ptr<RenderPipelineLayout> layout;
            RenderInputSlot inputSlot;
            RenderInputElement inputElements[2];
            std::unique_ptr<RenderPipeline> last;
        };
        auto fx = std::make_shared<Fixture>();
        fx->device = device;

        Workload w;
        w.name = "graphics_pipeline_create";

        w.setup = [fx, fmt]() {
            fx->vertexShader = PLUME_SELECT_SHADER(fx->device, fmt, "VSMain", SceneColorVert);
            fx->pixelShader  = PLUME_SELECT_SHADER(fx->device, fmt, "PSMain", SceneColorFrag);
            RenderPipelineLayoutDesc layoutDesc;
            layoutDesc.allowInputLayout = true;
            fx->layout = fx->device->createPipelineLayout(layoutDesc);

            fx->inputSlot = RenderInputSlot(0, sizeof(float) * 7);
            fx->inputElements[0] = RenderInputElement("POSITION", 0, 0, RenderFormat::R32G32B32_FLOAT, 0, 0);
            fx->inputElements[1] = RenderInputElement("COLOR", 0, 1, RenderFormat::R32G32B32A32_FLOAT, 0, sizeof(float) * 3);
        };

        w.run = [fx]() {
            RenderGraphicsPipelineDesc desc;
            desc.inputSlots = &fx->inputSlot;
            desc.inputSlotsCount = 1;
            desc.inputElements = fx->inputElements;
            desc.inputElementsCount = 2;
            desc.pipelineLayout = fx->layout.get();
            desc.vertexShader = fx->vertexShader.get();
            desc.pixelShader = fx->pixelShader.get();
            desc.renderTargetFormat[0] = RenderFormat::R8G8B8A8_UNORM;
            desc.renderTargetBlend[0] = RenderBlendDesc::Copy();
            desc.renderTargetCount = 1;
            desc.primitiveTopology = RenderPrimitiveTopology::TRIANGLE_LIST;
            fx->last = fx->device->createGraphicsPipeline(desc);
        };

        w.verify = [fx]() -> VerifyResult {
            return fx->last != nullptr ? VerifyResult::ok()
                                       : VerifyResult::fail("createGraphicsPipeline returned null");
        };

        return w;
    }

}
