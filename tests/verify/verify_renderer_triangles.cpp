// Checks for the renderer's triangle draw (renderer_triangles.cpp).
#include "verify.h"

#include <cstdint>

int __fastcall RendererTriangles(char* renderer, void*, void* vertices, int vertexCount, const std::uint16_t* indices,
                                 unsigned indexCount);

// 00501BB0 draws with Direct3D: ours only. The result is 1 and the device object counts one call,
// the vertices and indexCount / 3 triangles.
static void* s_triangles;
static int __fastcall RendererTrianglesVerify(char* renderer, void*, void* vertices, int vertexCount,
                                              const std::uint16_t* indices, unsigned indexCount) {
    char* device = *reinterpret_cast<char**>(renderer + 0x20);
    int calls = *reinterpret_cast<int*>(device + 0x174);
    int counted = *reinterpret_cast<int*>(device + 0x16C);
    unsigned triangles = *reinterpret_cast<unsigned*>(device + 0x170);
    int result = RendererTriangles(renderer, nullptr, vertices, vertexCount, indices, indexCount);
    bool ok = result == 1 && *reinterpret_cast<int*>(device + 0x174) == calls + 1 &&
              *reinterpret_cast<int*>(device + 0x16C) == counted + vertexCount &&
              *reinterpret_cast<unsigned*>(device + 0x170) == triangles + indexCount / 3;
    ei::verify::Check("RendererTriangles", true, ok, "result %d vertices %d indices %u", result, vertexCount, indexCount);
    return result;
}
EI_VERIFY(0x00501BB0, RendererTrianglesVerify, 0, s_triangles);
