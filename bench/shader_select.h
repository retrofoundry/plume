//
// plume bench
//

#pragma once

#include "plume_render_interface.h"

// Selects the shader blob matching the active backend's shader format. METAL and
// SPIRV are both emitted on Apple builds; SPIRV (and DXIL on Windows) elsewhere. Use
// inside setup():
//   fx->vs = PLUME_SELECT_SHADER(device, fmt, "VSMain", SceneColorVert);
// which expands to a switch over <Name>BlobMSL / <Name>BlobSPIRV / <Name>BlobDXIL.

// The DXIL case is only meaningful on Windows, where the CMake emits the
// <Name>.hlsl.dxil.h headers (defining <Name>BlobDXIL). Off Windows it expands to
// nothing so the symbol is never referenced.
#ifdef _WIN32
#define PLUME_SELECT_SHADER_DXIL_CASE(device, fmt, entry, name)                        \
    case plume::RenderShaderFormat::DXIL:                                              \
        return (device)->createShader(name##BlobDXIL, sizeof(name##BlobDXIL),          \
                                      entry, fmt);
#else
#define PLUME_SELECT_SHADER_DXIL_CASE(device, fmt, entry, name)
#endif

#ifdef __APPLE__
#define PLUME_SELECT_SHADER(device, fmt, entry, name)                                  \
    ([&]() -> std::unique_ptr<plume::RenderShader> {                                    \
        switch (fmt) {                                                                  \
            case plume::RenderShaderFormat::METAL:                                      \
                return (device)->createShader(name##BlobMSL, sizeof(name##BlobMSL),     \
                                              entry, fmt);                              \
            case plume::RenderShaderFormat::SPIRV:                                      \
                return (device)->createShader(name##BlobSPIRV, sizeof(name##BlobSPIRV), \
                                              entry, fmt);                              \
            default: return nullptr;                                                    \
        }                                                                               \
    }())
#else
#define PLUME_SELECT_SHADER(device, fmt, entry, name)                                  \
    ([&]() -> std::unique_ptr<plume::RenderShader> {                                    \
        switch (fmt) {                                                                  \
            case plume::RenderShaderFormat::SPIRV:                                      \
                return (device)->createShader(name##BlobSPIRV, sizeof(name##BlobSPIRV), \
                                              entry, fmt);                              \
            PLUME_SELECT_SHADER_DXIL_CASE(device, fmt, entry, name)                     \
            default: return nullptr;                                                    \
        }                                                                               \
    }())
#endif
