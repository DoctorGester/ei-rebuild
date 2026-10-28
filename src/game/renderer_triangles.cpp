// The renderer's triangle draw of
// pre-transformed vertices (FVF 0x1C4) as an indexed triangle
// list with the current state, fogged first by distance when the device's distanceFog_ is set (the
// transform object's FogVertices). Used by the flying hit points. The new renderer
// (src/render) takes the draw. See docs/notes/renderer.md.
#include "clip.h"     // TLVertex
#include "device.h"
#include "model.h"    // CSceneRenderer, ITransformPipeline

#include "render/render.h"

#include <cstdint>

namespace {
constexpr DWORD kFvf = 0x1C4;   // D3DFVF_TLVERTEX
}

int CSceneRenderer::DrawTriangles(const void* vertexData, int vertexCount, const std::uint16_t* indices, unsigned indexCount) {
    void* vertices = const_cast<void*>(vertexData);
    CTextureDevice* device = device_;
    if (device->distanceFog_) device->pipeline_->transform_->FogVertices(static_cast<TLVertex*>(vertices), vertexCount);
    render::TlDraw(device, vertices, static_cast<int>(kFvf), vertexCount, indices, static_cast<int>(indexCount),
                   static_cast<int>(D3DPT_TRIANGLELIST));
    device->vertexCount_ += vertexCount;
    device->primitiveCount_ += static_cast<int>(indexCount / 3);
    device->drawCount_ += 1;
    return 1;
}
