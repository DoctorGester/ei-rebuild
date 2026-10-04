// The device's draw of flat-coloured textured geometry (004EA4A0, thiscall on the device at the
// renderer's +0x20, RET 0x24): used for object shadows (0051CE60), by the renderer (00503A40) and
// 006CEDE0. The positions (blocks of four vertices) are projected by the pipeline (004F2650, with
// outcodes); every vertex gets one colour / specular pair and its own texture coordinate; z can be
// scaled. Crossing the screen edges, the triangles go through the transform object's clipper (slot
// 37) and the vertices are projected again (slot 34). Shares the buffers of device_mesh.cpp
// (007AEDE0..007AEE3C). See docs/notes/models.md ("Mesh draws").
#include "clip.h"

#include "ei/hook.h"

#include <cstdint>
#include <cstring>

namespace {

template <typename T>
T& At(void* base, int offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(base) + offset);
}

struct AlignedArray {
    char* data;
    int size;
    int capacity;
    int growBy;
};
AlignedArray& Vertices() { return *reinterpret_cast<AlignedArray*>(0x007AEE00); }        // TL vertices
AlignedArray& ClipIndices() { return *reinterpret_cast<AlignedArray*>(0x007AEDE0); }     // words
AlignedArray& Projected() { return *reinterpret_cast<AlignedArray*>(0x007AEE10); }       // x, y, z, rhw
AlignedArray& CornerOutcodes() { return *reinterpret_cast<AlignedArray*>(0x007AEE20); }  // a byte per vertex
AlignedArray& Outcodes() { return *reinterpret_cast<AlignedArray*>(0x007AEE30); }        // a byte per vertex

__declspec(naked) void __cdecl GameDelete(void*) EI_ORIGINAL(0x007005D8)
__declspec(naked) char* __fastcall Alloc16(int) EI_ORIGINAL(0x00468F70)
__declspec(naked) char* __fastcall AllocBytes(int) EI_ORIGINAL(0x004ECB50)
__declspec(naked) char* __fastcall AllocVertices(int) EI_ORIGINAL(0x004ECB70)
__declspec(naked) char* __fastcall AllocWords(int) EI_ORIGINAL(0x00406B30)
// 004F2650: the pipeline projects `blocks` blocks of positions with outcodes [positions, out,
// outcodes, blocks, last]; 0 on screen, 1 crossing, 2 outside.
__declspec(naked) int __fastcall ProjectBlocks(void*, void*, const void*, void*, void*, int, std::uint32_t) EI_ORIGINAL(0x004F2650)
// The device [thiscall]: a vertex buffer for n vertices [004E7820, always null], its draw
// [004E7E00: indices, count], the indexed draw [004EB550: primitive, FVF, vertices, count, indices,
// count, flags].
__declspec(naked) void* __fastcall LockVertexBuffer(char*, void*, int) EI_ORIGINAL(0x004E7820)
__declspec(naked) void __fastcall DrawVertexBuffer(char*, void*, const void*, int) EI_ORIGINAL(0x004E7E00)
__declspec(naked) int __fastcall DrawPrimitive(char*, void*, int, int, void*, int, const void*, int, int) EI_ORIGINAL(0x004EB550)

int GrowthStep(const AlignedArray& a) {
    if (a.growBy != 0) return a.growBy;
    int step = a.size / 8;
    if (step < 4) return 4;
    if (step > 0x400) return 0x400;
    return step;
}

// SetSize without keeping the contents.
void Resize(AlignedArray& a, int size, char* (__fastcall* alloc)(int)) {
    if (!a.data) {
        a.data = alloc(size);
        a.capacity = size;
        a.size = size;
        return;
    }
    if (size <= a.capacity) {
        a.size = size;
        return;
    }
    int grown = GrowthStep(a) + a.capacity;
    int capacity = size < grown ? grown : size;
    char* data = alloc(capacity);
    GameDelete(reinterpret_cast<void**>(a.data)[-1]);
    a.data = data;
    a.size = size;
    a.capacity = capacity;
}

// SetSize keeping the contents; 0 frees the block.
void SetSize(AlignedArray& a, int size, int elementSize, char* (__fastcall* alloc)(int)) {
    if (size == 0) {
        if (a.data) {
            GameDelete(reinterpret_cast<void**>(a.data)[-1]);
            a.data = nullptr;
        }
        a.capacity = 0;
        a.size = 0;
        return;
    }
    if (!a.data) {
        a.data = alloc(size);
        a.capacity = size;
        a.size = size;
        return;
    }
    if (size <= a.capacity) {
        a.size = size;
        return;
    }
    int grown = GrowthStep(a) + a.capacity;
    int capacity = size < grown ? grown : size;
    char* data = alloc(capacity);
    std::memcpy(data, a.data, static_cast<std::size_t>(a.size) * elementSize);
    GameDelete(reinterpret_cast<void**>(a.data)[-1]);
    a.data = data;
    a.size = size;
    a.capacity = capacity;
}

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
    const char* projected = Projected().data;
    for (int i = 0; i < count; ++i) {
        std::memcpy(&out[i].x, projected + i * 16, 16);
        std::memcpy(&out[i].color, colours, 8);
        std::memcpy(&out[i].u, uvs + i * 8, 8);
    }
}

void Scale(int count, const float* scale) {
    if (OneOrNaN(scale)) return;
    for (int i = 0; i < count; ++i) ScaleZ(&reinterpret_cast<TLVertex*>(Vertices().data)[i].z, scale);
}

} // namespace

// 004EA4A0. `positions` blocks of four vertices (`blocks` of them), `uvs` one (u, v) per vertex,
// `indices` / `indexCount` the triangles, `colour` and `specular` for every vertex, `last` passed
// to the projection, `zScale` multiplies z unless it compares equal to 1. Returns 2 when nothing is
// visible, else 0.
int __fastcall DeviceDrawFlat(char* device, void*, const void* positions, const char* uvs, int blocks,
                              const std::uint16_t* indices, int indexCount, std::uint32_t colour, std::uint32_t specular,
                              std::uint32_t last, float zScale) {
    void* pipeline = At<void*>(device, 0xAC);
    void* transform = At<void*>(pipeline, 0xC);
    int count = blocks * 4;
    std::uint32_t colours[2] = {colour, specular};
    Resize(Projected(), count * 10, &Alloc16);
    Resize(Outcodes(), count, &AllocBytes);
    int visibility = ProjectBlocks(pipeline, nullptr, positions, Projected().data, Outcodes().data, blocks, last);

    if (visibility == 0) {
        TLVertex* target = static_cast<TLVertex*>(LockVertexBuffer(device, nullptr, count));
        if (!target) {
            Resize(Vertices(), count, &AllocVertices);
            target = reinterpret_cast<TLVertex*>(Vertices().data);
        }
        Fill(target, count, colours, uvs);
        if (target == reinterpret_cast<TLVertex*>(Vertices().data)) {
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

    Resize(Vertices(), count * 10, &AllocVertices);
    Fill(reinterpret_cast<TLVertex*>(Vertices().data), count, colours, uvs);
    Resize(ClipIndices(), indexCount * 10, &AllocWords);
    Resize(CornerOutcodes(), Outcodes().size * 10, &AllocBytes);
    std::memcpy(CornerOutcodes().data, Outcodes().data, static_cast<std::size_t>(Outcodes().size));
    int vertexCount = count;
    int clippedIndices = indexCount;
    ThisCall<int>(transform, 37, static_cast<void*>(Vertices().data), static_cast<void*>(CornerOutcodes().data),
                  indices, static_cast<void*>(ClipIndices().data), &vertexCount, &clippedIndices);
    SetSize(Vertices(), vertexCount, sizeof(TLVertex), &AllocVertices);
    SetSize(ClipIndices(), clippedIndices, 2, &AllocWords);
    ThisCall<void>(transform, 34, static_cast<void*>(Vertices().data), Vertices().size);
    if (vertexCount == 0 || clippedIndices == 0) return 0;
    Scale(vertexCount, &zScale);
    DrawPrimitive(device, nullptr, 4, 0x1C4, Vertices().data, vertexCount, ClipIndices().data, clippedIndices, 0);
    return 0;
}
EI_REPLACE(0x004EA4A0, DeviceDrawFlat);
