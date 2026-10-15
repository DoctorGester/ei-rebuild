// Checks for the renderer's triangle draw (renderer_triangles.cpp).
#include "verify.h"

#include "../../src/game/model.h"
#include "../../src/game/texture.h"

#include <cstdint>

// 00501BB0 draws with Direct3D: ours only. The result is 1 and the device object counts one call,
// the vertices and indexCount / 3 triangles.
static void* s_triangles;
static int __fastcall RendererTrianglesVerify(CSceneRenderer* renderer, void*, void* vertices, int vertexCount,
                                              const std::uint16_t* indices, unsigned indexCount) {
    const CTextureDevice* device = renderer->device_;
    int calls = device->drawCount_;
    int counted = device->vertexCount_;
    unsigned triangles = static_cast<unsigned>(device->primitiveCount_);
    int result = renderer->DrawTrianglesImpl(vertices, vertexCount, indices, indexCount);
    bool ok = result == 1 && device->drawCount_ == calls + 1 && device->vertexCount_ == counted + vertexCount &&
              static_cast<unsigned>(device->primitiveCount_) == triangles + indexCount / 3;
    ei::verify::Check("RendererTriangles", true, ok, "result %d vertices %d indices %u", result, vertexCount, indexCount);
    return result;
}
EI_VERIFY(0x00501BB0, RendererTrianglesVerify, 0, s_triangles);
