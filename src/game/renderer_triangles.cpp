// The renderer's triangle draw (00501BB0, virtual slot 12 of the renderer at 007AF060, vtable
// 0073D93C; thiscall, RET 0x10): pre-transformed vertices (FVF 0x1C4) drawn as an indexed triangle
// list with the current state, fogged first by distance when the device's distanceFog_ is set (the
// transform object's FogVertices). Used by the flying hit points (005E18E0). The new renderer
// (src/render) takes the draw; with the old path's objects off it is skipped. See
// docs/notes/renderer.md.
#include "clip.h"     // TLVertex
#include "device.h"
#include "model.h"    // CSceneRenderer, ITransformPipeline

#include "ei/hook.h"
#include "lib/gameapi.h"
#include "render/render.h"

#include <cstdint>

namespace {
constexpr DWORD kFvf = 0x1C4;   // D3DFVF_TLVERTEX
}

// 00501BB0. Returns 1.
int CSceneRenderer::DrawTrianglesImpl(void* vertices, int vertexCount, const std::uint16_t* indices, unsigned indexCount) {
    CTextureDevice* device = device_;
    if (device->distanceFog_) device->pipeline_->transform_->FogVertices(static_cast<TLVertex*>(vertices), vertexCount);
    if (render::TlDraw(device, vertices, static_cast<int>(kFvf), vertexCount, indices, static_cast<int>(indexCount),
                       static_cast<int>(D3DPT_TRIANGLELIST))) {
        gameapi::EnterCriticalSection(device::Lock());
        device->lastLoadResult_ = device::DrawIndexedPrimitive(device::D3D(device), 
            D3DPT_TRIANGLELIST, kFvf, vertices, static_cast<DWORD>(vertexCount), const_cast<WORD*>(indices),
            static_cast<DWORD>(indexCount), 0);
        gameapi::LeaveCriticalSection(device::Lock());
    }
    device->vertexCount_ += vertexCount;
    device->primitiveCount_ += static_cast<int>(indexCount / 3);
    device->drawCount_ += 1;
    return 1;
}
EI_REPLACE_METHOD(0x00501BB0, CSceneRenderer::DrawTrianglesImpl);
