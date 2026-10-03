#pragma once

// Figure meshes: the game-side figure part objects created by the figure factory 005AA8E0
// (class ids 0x41-0x44) and their loaders (0x005A0000-0x005B0000). Names provisional; see the
// "Figure meshes" section of docs/notes/world.md.

#include "maths.h"
#include "model.h"

#include <cstddef>
#include <cstdint>

// The 32-byte aligned growable arrays of the mesh (the operator_new block pointer is stored just
// before the data). 16 bytes; the same template as TriangleIndexArray (004F57B0) and 005A6E70.
template <typename T>
struct AlignedArray {
    T* data;        // +0x00
    int size;       // +0x04
    int capacity;   // +0x08
    int growBy;     // +0x0C 0 = size / 8 clamped to 4..1024

    // Resizes to `count` elements keeping the contents; 0 frees the buffer. This is the code the
    // original inlines in every loader (and 005A6E70 / 0044FA90 / 004F57B0 out of line with a
    // growBy argument, -1 = keep).
    void SetSize(int count);

    static T* Allocate(int count);
    static void Free(T* data);
};

// A vertex of a mesh: three 16-byte blocks (position, normal and a third block) and a fourth
// block the loaders set to 1.0. 0x40 bytes.
struct MeshVertex {
    float block[4][4];
};
static_assert(sizeof(MeshVertex) == 0x40);

// The other per-element records the loaders copy verbatim.
struct MeshRecord40 {
    std::uint32_t w[16];
};
struct MeshRecord8 {
    std::uint32_t w[2];
};
struct MeshTriangle {
    std::uint16_t index[3];
};
static_assert(sizeof(MeshRecord40) == 0x40 && sizeof(MeshRecord8) == 8 && sizeof(MeshTriangle) == 6);

// What the renderer draws from (+0x9C): pointers and counts of the mesh's arrays (class 0x41 points
// the last four into the loaded file data instead).
struct MeshBuffers {
    void* vertices;     // +0x00 (+0x9C)
    int vertexCount;
    void* records;      // +0x08 (+0xA4)
    int recordCount;
    void* pairs;        // +0x10 (+0xAC) 8-byte elements
    int pairCount;
    void* triangles;    // +0x18 (+0xB4)
    int triangleCount;
    void* words;        // +0x20 (+0xBC) 4-byte elements
    int wordCount;
    void* shorts;       // +0x28 (+0xC4) 2-byte elements
    int shortCount;
};
static_assert(sizeof(MeshBuffers) == 0x30);

// The figure mesh of class 0x42 (0x220 bytes, vtable 0073C1C4); 0x43 (0073FC48) and 0x44 derive
// from it and share its loaders. Class 0x41 (0x1E0 bytes, vtable 0073FBF0) has the same layout up
// to +0x1E0 and its own loaders. No virtuals are declared: the vtable is a plain field.
class CFigureMesh {
public:
    // 005A18E0. Reads the bounding volume (centre, box, radius) that follows the mesh header and
    // morphs it: "FIG8" files hold 8 corner versions of each value, blended trilinearly by the
    // figure's three morph coefficients; other versions hold one. Sets the box and centre of
    // `bounds_`. RET 0x14.
    void LoadBounds(char** cursor, float s, float t, float u, std::uint32_t magic);

    // 005A2500 (vtable slot 20 of classes 0x42-0x44). Loads a "FIG" mesh from memory at *cursor
    // (advanced past it), morphing FIG8 vertices; once only (flag 0x20). RET 0x10.
    void LoadFig(char** cursor, float s, float t, float u);
    // 005A5C70 (slot 20 of class 0x41): the same, but only the vertices and records are copied;
    // the other buffers point into the file data.
    void LoadFigShared(char** cursor, float s, float t, float u);
    // 005A3800 (slot 21 of classes 0x42-0x44): the older format without a FIG header, chosen by
    // slot 19 (005A2490) when the data does not start with "FIG": 6 corner versions of the
    // bounds and of every vertex, each array preceded by its count. RET 0x10.
    void LoadOld(char** cursor, float s, float t, float u);
    // 005A4D60 (slot 21 of class 0x41): the same, the last four buffers pointing into the data.
    void LoadOldShared(char** cursor, float s, float t, float u);

    void* vtable_;                      // +0x000
    std::uint8_t unknown_004[4];
    std::uint32_t flags_;               // +0x008 0x20: loaded
    std::uint8_t unknown_00C[0x9C - 0x0C];
    MeshBuffers buffers_;               // +0x09C
    std::uint8_t unknown_0CC[0x138 - 0xCC];
    AlignedArray<std::uint32_t> colorsA_;   // +0x138 4 per record
    AlignedArray<std::uint32_t> colorsB_;   // +0x148 4 per record
    std::uint32_t unknown_158;          // +0x158 from the file header
    std::uint32_t unknown_15C;          // +0x15C from the file header
    Vector3 center_;                    // +0x160
    Vector3 boxMin_;                    // +0x16C
    Vector3 boxMax_;                    // +0x178
    float radius_;                      // +0x184
    ClipBounds bounds_;                 // +0x188
    std::uint8_t unknown_1B4[0x1C0 - 0x1B4];
    AlignedArray<MeshVertex> vertices_;     // +0x1C0
    AlignedArray<MeshRecord40> records_;    // +0x1D0
    // Class 0x41 ends here (0x1E0 bytes).
    AlignedArray<MeshTriangle> triangles_;  // +0x1E0
    AlignedArray<std::uint32_t> words_;     // +0x1F0
    AlignedArray<std::uint16_t> shorts_;    // +0x200
    AlignedArray<MeshRecord8> pairs_;       // +0x210
};
static_assert(offsetof(CFigureMesh, flags_) == 0x08);
static_assert(offsetof(CFigureMesh, buffers_) == 0x9C);
static_assert(offsetof(CFigureMesh, colorsA_) == 0x138);
static_assert(offsetof(CFigureMesh, unknown_158) == 0x158);
static_assert(offsetof(CFigureMesh, center_) == 0x160);
static_assert(offsetof(CFigureMesh, radius_) == 0x184);
static_assert(offsetof(CFigureMesh, bounds_) == 0x188);
static_assert(offsetof(CFigureMesh, vertices_) == 0x1C0);
static_assert(offsetof(CFigureMesh, triangles_) == 0x1E0);
static_assert(offsetof(CFigureMesh, pairs_) == 0x210);
static_assert(sizeof(CFigureMesh) == 0x220);

// The header in front of a FIG mesh: the magic ("FIG8" = 0x38474946 morphs) and the element
// counts, then the two dwords at +0x158 / +0x15C.
struct FigHeader {
    std::uint32_t magic;
    int vertexCount;
    int recordCount;
    int pairCount;
    int shortCount;
    int triangleCount;
    int wordCount;
    std::uint32_t unknown_1C;
};
static_assert(sizeof(FigHeader) == 0x20);

constexpr std::uint32_t kFig8Magic = 0x38474946;   // "FIG8"

// 005AAE60. The figure loader's (005AC010) bounding volume of a FIG figure part: `data` holds the
// centre, box minimum, box maximum and radius, 8 versions each for FIG8 (morphed as in
// CFigureMesh::LoadBounds, except that the radius keeps its lower blend on the FPU) or one
// otherwise. Called with the loader in ECX, which it does not use. RET 0x18.
void __stdcall MorphFigureBounds(ClipBounds* out, const float* data, float s, float t, float u, std::uint32_t magic);
// 005AB9A0. The same for figure parts without a FIG header: 6 corner versions of each value,
// blended as the bones are. RET 0x14.
void __stdcall MorphFigureBounds6(ClipBounds* out, const float* data, float s, float t, float u);

// ---- AlignedArray -------------------------------------------------------------------------------

#include "lib/crt.h"

#include <cstring>

template <typename T>
T* AlignedArray<T>::Allocate(int count) {
    char* block = static_cast<char*>(gamecrt::operator_new(static_cast<std::size_t>(count * static_cast<int>(sizeof(T)) + 0x24)));
    T* data = reinterpret_cast<T*>(reinterpret_cast<std::uintptr_t>(block + 0x23) & ~std::uintptr_t(0x1F));
    reinterpret_cast<char**>(data)[-1] = block;
    return data;
}

template <typename T>
void AlignedArray<T>::Free(T* p) {
    gamecrt::operator_delete(reinterpret_cast<char**>(p)[-1]);
}

template <typename T>
void AlignedArray<T>::SetSize(int count) {
    if (count == 0) {
        if (data) {
            Free(data);
            data = nullptr;
        }
        capacity = 0;
        size = 0;
        return;
    }
    if (!data) {
        data = Allocate(count);
        capacity = count;
        size = count;
        return;
    }
    if (count <= capacity) {
        size = count;
        return;
    }
    int step = growBy;
    if (step == 0) {
        step = size / 8;
        if (step < 4) step = 4;
        else if (step > 0x400) step = 0x400;
    }
    int newCapacity = step + capacity;
    if (count >= newCapacity) newCapacity = count;
    T* fresh = Allocate(newCapacity);
    std::memcpy(fresh, data, static_cast<std::size_t>(size) * sizeof(T));
    Free(data);
    data = fresh;
    size = count;
    capacity = newCapacity;
}
