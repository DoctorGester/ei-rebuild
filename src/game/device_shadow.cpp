// The device's draw of flat-coloured textured geometry (004EA4A0, thiscall on the device at the
// renderer's +0x20, RET 0x24): used for object shadows (0051CE60), by the renderer (00503A40) and
// 006CEDE0. The positions (blocks of four vertices) are projected by the pipeline (004F2650, with
// outcodes); every vertex gets one colour / specular pair and its own texture coordinate; z can be
// scaled. Crossing the screen edges, the triangles go through the transform object's clipper (slot
// 37) and the vertices are projected again (slot 34). Shares the buffers of device_mesh.cpp
// (007AEDE0..007AEE3C). See docs/notes/models.md ("Mesh draws").
#include "clip.h"

#include "ei/hook.h"
#include "device_buffers.h"
#include "render/render.h"
#include "originals_core.h"
#include "originals_library.h"
#include "originals_render.h"

#include <cstdint>
#include <cstring>

namespace {

template <typename T>
T& At(void* base, int offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(base) + offset);
}

using devicebuffers::ClipIndices;
using devicebuffers::CornerOutcodes;
using devicebuffers::Outcodes;
using devicebuffers::Projected;
using devicebuffers::ProjectedVertex;
using devicebuffers::Vertices;

// 004F2650: the pipeline projects `blocks` blocks of positions with outcodes [positions, out,
// outcodes, blocks, last]; 0 on screen, 1 crossing, 2 outside.
__declspec(naked) int __fastcall ProjectBlocks(void*, void*, const void*, void*, void*, int, std::uint32_t) EI_ORIGINAL(0x004F2650)
// The device [thiscall]: a vertex buffer for n vertices [004E7820, always null], its draw
// [004E7E00: indices, count], the indexed draw [004EB550: primitive, FVF, vertices, count, indices,
// count, flags].
void __fastcall DrawVertexBuffer(char* a0, void*, const void* a2, int a3) { DeviceDrawVertexBufferOriginal((void*)a0, nullptr, a2, a3); }
__declspec(naked) int __fastcall DrawPrimitive(char*, void*, int, int, void*, int, const void*, int, int) EI_ORIGINAL(0x004EB550)

template <typename R, typename... Args>
R ThisCall(void* object, int slot, Args... args) {
    void* fn = (*static_cast<void***>(object))[slot];
    return reinterpret_cast<R(__fastcall*)(void*, void*, Args...)>(fn)(object, nullptr, args...);
}

// *z = *scale * *z (FLD scale, FMUL z, FSTP). __fastcall.
__declspec(naked) void __fastcall ScaleZ(float* /*z*/, const float* /*scale*/) {
    __asm {
        fld dword ptr [edx]
        fmul dword ptr [ecx]
        fstp dword ptr [ecx]
        ret
    }
}

// FCOMP with 1.0 (0073B84C) and C3: equal, or unordered.
bool OneOrNaN(const float* v) {
    std::uint32_t bits;
    std::memcpy(&bits, v, 4);
    return bits == 0x3F800000u || ((bits & 0x7F800000u) == 0x7F800000u && (bits & 0x007FFFFFu) != 0);
}

// The vertices: projected position, the colour / specular pair, the vertex's texture coordinate
// (each copied as qwords).
void Fill(TLVertex* out, int count, const void* colours, const char* uvs) {
    const ProjectedVertex* projected = Projected().data;
    for (int i = 0; i < count; ++i) {
        std::memcpy(&out[i].x, &projected[i], 16);
        std::memcpy(&out[i].color, colours, 8);
        std::memcpy(&out[i].u, uvs + i * 8, 8);
    }
}

void Scale(int count, const float* scale) {
    if (OneOrNaN(scale)) return;
    for (int i = 0; i < count; ++i) ScaleZ(&Vertices().data[i].z, scale);
}

// The new renderer (src/render) gets the geometry in model space with the screen stack's matrix
// (the projection 004F2650 uses the clip stack +0x24 or the screen stack +0x1C, which get the same
// pushes); each pass the indexed draw then makes hands over the device's state.
void HandOver(char* device, const void* positions, const char* uvs, int count, const std::uint16_t* indices,
              int indexCount, const std::uint32_t* colours, const float* zScale, bool clipped) {
    char* pipeline = At<char*>(device, 0xAC);
    render::MeshDraw m{};
    m.positions = static_cast<const float*>(positions);
    m.positionCount = count;
    m.colours = &colours[0];
    m.speculars = &colours[1];
    m.uvs = reinterpret_cast<const float*>(uvs);
    m.corners = nullptr;
    m.cornerCount = count;
    m.indices = indices;
    m.indexCount = indexCount;
    m.matrix = At<float*>(pipeline, 0x1C) + (At<int>(pipeline, 0x20) - 1) * 16;
    m.screenMatrix = true;
    m.clipped = clipped;
    m.distanceFog = false;   // no fog pass (slot 35) here
    m.zScale = OneOrNaN(zScale) ? 1.0f : *zScale;
    render::BeginMesh(device, m);
}

int DrawFlat(char* device, const void* positions, const char* uvs, int blocks, const std::uint16_t* indices,
             int indexCount, std::uint32_t colour, std::uint32_t specular, std::uint32_t last, float zScale) {
    void* pipeline = At<void*>(device, 0xAC);
    void* transform = At<void*>(pipeline, 0xC);
    int count = blocks * 4;
    std::uint32_t colours[2] = {colour, specular};
    Projected().SetSizeDiscard(count * 10);
    Outcodes().SetSizeDiscard(count);
    int visibility = ProjectBlocks(pipeline, nullptr, positions, Projected().data, Outcodes().data, blocks, last);
    if (visibility != 2 && render::Active())
        HandOver(device, positions, uvs, count, indices, indexCount, colours, &zScale, visibility == 1);

    if (visibility == 0) {
        TLVertex* target = static_cast<TLVertex*>(LockVertexBuffer(device, nullptr, count));
        if (!target) {
            Vertices().SetSizeDiscard(count);
            target = Vertices().data;
        }
        Fill(target, count, colours, uvs);
        if (target == Vertices().data) {
            Scale(count, &zScale);
            DrawPrimitive(device, nullptr, 4, 0x1C4, Vertices().data, count, indices, indexCount, 0);
            return 0;
        }
        DrawVertexBuffer(device, nullptr, indices, indexCount);
        At<int>(device, 0xE0) += count;
        return 0;
    }
    if (visibility == 2) return 2;
    if (visibility != 1) return 0;

    Vertices().SetSizeDiscard(count * 10);
    Fill(Vertices().data, count, colours, uvs);
    ClipIndices().SetSizeDiscard(indexCount * 10);
    CornerOutcodes().SetSizeDiscard(Outcodes().size * 10);
    std::memcpy(CornerOutcodes().data, Outcodes().data, static_cast<std::size_t>(Outcodes().size));
    int vertexCount = count;
    int clippedIndices = indexCount;
    ThisCall<int>(transform, 37, static_cast<void*>(Vertices().data), static_cast<void*>(CornerOutcodes().data),
                  indices, static_cast<void*>(ClipIndices().data), &vertexCount, &clippedIndices);
    Vertices().SetSize(vertexCount);
    ClipIndices().SetSize(clippedIndices);
    ThisCall<void>(transform, 34, static_cast<void*>(Vertices().data), Vertices().size);
    if (vertexCount == 0 || clippedIndices == 0) return 0;
    Scale(vertexCount, &zScale);
    DrawPrimitive(device, nullptr, 4, 0x1C4, Vertices().data, vertexCount, ClipIndices().data, clippedIndices, 0);
    return 0;
}

} // namespace

// 004EA4A0. `positions` blocks of four vertices (`blocks` of them), `uvs` one (u, v) per vertex,
// `indices` / `indexCount` the triangles, `colour` and `specular` for every vertex, `last` passed
// to the projection, `zScale` multiplies z unless it compares equal to 1. Returns 2 when nothing is
// visible, else 0.
int __fastcall DeviceDrawFlat(char* device, void*, const void* positions, const char* uvs, int blocks,
                              const std::uint16_t* indices, int indexCount, std::uint32_t colour, std::uint32_t specular,
                              std::uint32_t last, float zScale) {
    int result = DrawFlat(device, positions, uvs, blocks, indices, indexCount, colour, specular, last, zScale);
    render::EndMesh();
    return result;
}
EI_REPLACE(0x004EA4A0, DeviceDrawFlat);
