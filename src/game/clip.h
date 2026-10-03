#pragma once

// Screen-space triangle clipping (0042D910-0042E6A0), used by the mesh renderer's software clip
// path (slot 17 of vtable 0073D93C, 00502CA0) for triangles whose vertices fall outside the
// viewport. Vertices are Direct3D TL vertices; the clipped polygon is re-triangulated as a fan
// into the caller's index list, and new vertices are appended to the caller's vertex array.
// Names provisional; see docs/notes/unmapped_410_470.md.

#include <cstddef>
#include <cstdint>

// D3DTLVERTEX (0x20 bytes).
struct TLVertex {
    float x, y, z, rhw;
    std::uint32_t color;      // ARGB
    std::uint32_t specular;   // ARGB
    float u, v;
};
static_assert(sizeof(TLVertex) == 0x20);
static_assert(offsetof(TLVertex, color) == 0x10);

namespace clip {

// The work buffer: an MFC CWordArray at 0079B668 (data pointer 0079B66C), the number of indices
// in it (0079B68C) and the last intersection parameter (0079B664).
inline std::uint16_t*& Buffer() { return *reinterpret_cast<std::uint16_t**>(0x0079B66C); }
inline int& BufferCount() { return *reinterpret_cast<int*>(0x0079B68C); }
inline float& LastRatio() { return *reinterpret_cast<float*>(0x0079B664); }

// 0042E4D0. `out` = a + (b - a) * t for position, rhw and texture coordinates; colour and specular
// per byte, each step rounded through a float and FISTP. __fastcall: a in ECX, b in EDX.
void __fastcall LerpVertex(const TLVertex* a, const TLVertex* b, float t, TLVertex* out);

// The six plane clippers. Each clips the polygon `indices[0..*count)` (vertex indices) against one
// plane and writes the result back to `indices` and `*count`; intersection vertices are appended
// at `vertices[*vertexCount]`. Returns the new count. ECX carries the triangle's combined
// outcode in the original's calls and is not used.
//   KeepBelow (0042DB70 x, 0042DE90 y, 0042E1B0 z): keeps vertices with coordinate <= limit.
//   KeepAbove (0042DD00 x, 0042E020 y, 0042E340 z): keeps vertices with coordinate > limit.
int __fastcall ClipXBelow(int, TLVertex* vertices, int* vertexCount, std::uint16_t* indices, int* count, float limit);
int __fastcall ClipXAbove(int, TLVertex* vertices, int* vertexCount, std::uint16_t* indices, int* count, float limit);
int __fastcall ClipYBelow(int, TLVertex* vertices, int* vertexCount, std::uint16_t* indices, int* count, float limit);
int __fastcall ClipYAbove(int, TLVertex* vertices, int* vertexCount, std::uint16_t* indices, int* count, float limit);
int __fastcall ClipZBelow(int, TLVertex* vertices, int* vertexCount, std::uint16_t* indices, int* count, float limit);
int __fastcall ClipZAbove(int, TLVertex* vertices, int* vertexCount, std::uint16_t* indices, int* count, float limit);

// 0042D910. Clips the triangle `tri[0..2]` to 0 <= x <= xMax, 0 <= y <= yMax, 0 <= z <= 1.
// `outcodes[i]` holds vertex i's six outside bits. Returns -1 (all three outside one plane:
// *indexCount = 0), 0 (all inside: *indexCount = 3) or 1 (clipped: `tri` holds *indexCount
// indices, a fan of triangles). __fastcall: vertices in ECX, outcodes in EDX.
int __fastcall ClipTriangle(TLVertex* vertices, const std::uint8_t* outcodes, std::uint16_t* tri,
                            int* vertexCount, int* indexCount, float xMax, float yMax);

} // namespace clip
