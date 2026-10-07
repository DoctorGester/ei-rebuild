#pragma once

// The scene file loader of CFakeUnit (00471B10): reads a binary .sc* scene (meshes with their
// vertex animation, the node hierarchy, helpers) into the arrays of the figure being built.
// Called by 00446C10 (`LoadFakeUnitScene`, the CFakeUnit slot 62 loader) with the opened CFile.
// Names are provisional; see docs/notes/objects.md ("Scene loader").
//
// File layout (all integers little endian, no padding):
//   header (0x14): u32 version (0x1468FCCA: records carry an extra normal list), u32 record count,
//                  u32 skipped record count, u32 helper count, u16 flags (returned to the caller)
//   records (0x2C header each, one mesh and one node per record), then the skipped records (same
//   format, read and dropped), then the helpers (0x3E header each).

#include "../lib/containers.h"
#include "../lib/mfc.h"

#include <cstddef>
#include <cstdint>

namespace scene {

// An MFC CArray of records (vtable, data, size, capacity, grow step: 0 is an eighth of the size, 4..1024)
// seen as bytes: the scene's mesh, node, helper and figure arrays, whose records are reached through
// byte offsets. Resize them only through a typed instantiation (scene_loader.cpp); SetSize here would
// move one-byte elements.
using VtArray = mfc::CArray<char, mfc::RawElements<char>>;

// A record the scene code only reaches through byte offsets.
template <std::size_t K>
struct Rec {
    std::uint8_t bytes[K];
};

// ConstructElements / DestructElements(elements, count) of an instantiation the original calls out
// of line (`Unmake` null: nothing is destroyed).
template <typename T, void(__stdcall* Make)(void*, int), void(__stdcall* Unmake)(void*, int) = nullptr>
struct OriginalElements {
    static void Construct(T* elements, int count) { Make(elements, count); }
    static void Destruct(T* elements, int count) {
        if constexpr (Unmake != nullptr) Unmake(elements, count);
    }
};

// The caller's untyped array resized through the typed instantiation of the same layout.
template <typename Records>
Records& Typed(VtArray& a) {
    static_assert(sizeof(Records) == sizeof(VtArray));
    return reinterpret_cast<Records&>(a);
}

// The "aligned" arrays (vertex lists, 32-byte aligned blocks with the raw pointer stored before the
// data; lib::AlignedArray), per element type. Positions and normals are kept as the bits that were
// read: the loader copies some of them through the FPU, as the original does.
struct PointBits {
    std::uint32_t v[3];
};
struct UvBits {
    std::uint32_t v[2];
};
using PointBuffer = lib::AlignedArray<PointBits>;
using UvBuffer = lib::AlignedArray<UvBits>;
struct Block64 {
    std::uint8_t bytes[64];   // a matrix, or four normals / positions in lanes
};
using BlockBuffer = lib::AlignedArray<Block64>;
using DwordBuffer = lib::AlignedArray<std::uint32_t>;

// MFC CMap<DWORD, DWORD, DWORD, DWORD> (0x1C bytes) and its association (0x10 bytes).
struct MapAssoc {
    MapAssoc* next;
    std::uint32_t hash;
    std::uint32_t key;
    std::uint32_t value;
};

struct DwordMap {
    std::uintptr_t vtable;    // +00 0073C818
    MapAssoc** table;         // +04
    std::uint32_t hashSize;   // +08 17
    int count;                // +0C
    MapAssoc* freeList;       // +10
    mfc::CPlex* blocks;       // +14
    int blockSize;            // +18 10

    // 00477340. The association of `key` (null if none); `hash` receives its bucket.
    MapAssoc* GetAssocAt(const std::uint32_t& key, std::uint32_t& hash) const;
    // 00476050. operator[]: the value of `key`, inserted (0) when missing.
    std::uint32_t& At(const std::uint32_t& key);
    // 00476140. The destructor.
    void Destruct();
};
static_assert(sizeof(DwordMap) == 0x1C);

// One face of a record (0x1C): three position indices, three texture-coordinate indices, one more
// dword the loader does not use.
struct FileFace {
    std::uint32_t position[3];
    std::uint32_t texture[3];
    std::uint32_t unknown_18;
};
static_assert(sizeof(FileFace) == 0x1C);

// A mesh face as the mesh keeps it (0x34). The loader copies the file face's six indices to
// +00..+14 and its three position indices again to +18..+20, then replaces both index triples at
// +00 and +0C by the merged vertex of each corner. +24..+30 are not written.
struct MeshFace {
    std::uint32_t vertex[3];       // +00
    std::uint32_t vertex2[3];      // +0C
    std::uint32_t position[3];     // +18 the file's position indices
    float normal[3];               // +24 unit normal of the plane (0049AEC0)
    float distance;                // +30 the plane's distance: dot(position[1]'s point, normal)
};
static_assert(sizeof(MeshFace) == 0x34);
using FaceBuffer = lib::AlignedArray<MeshFace>;

// face_normal.cpp (A-S4). 0049AEC0 (__thiscall, RET 4): the face's plane from the points
// `positions` (an array of 3-float points) holds: the edges from vertex 1 to vertex 2 and to
// vertex 0 are normalised unless shorter than 1e-5, crossed, the result normalised (a zero
// length gives the vector at 007A9C98), then the distance.
void __fastcall ComputeFaceNormal(MeshFace* face, void*, const PointBuffer* positions);

// A material group (0x18): its name and the faces it uses.
struct FaceGroup {
    mfc::CString name;        // +00
    mfc::CArray<std::uint32_t> faces;   // +04 CDWordArray of face indices
};
static_assert(sizeof(FaceGroup) == 0x18);

// Mesh (element of the caller's +0x544 array, 0x18C bytes, constructed by 004545C0).
namespace mesh {
constexpr std::size_t kSize = 0x18C;
constexpr std::size_t kName = 0x94;        // CString
constexpr std::size_t kPositions = 0xD8;   // aligned array of 12-byte points
constexpr std::size_t kTexCoords = 0xE8;   // aligned array of 8-byte (u, v)
constexpr std::size_t kFaces = 0x138;      // aligned array of MeshFace
constexpr std::size_t kGroups = 0x148;     // CArray of FaceGroup (vtable at +0x148)
} // namespace mesh

// Node (element of the caller's local array, 0x120 bytes, constructed by 00454F80).
namespace node {
constexpr std::size_t kSize = 0x120;
constexpr std::size_t kName = 0x04;        // CString
constexpr std::size_t kParent = 0x08;      // CString
constexpr std::size_t kOffset = 0x54;      // 3 floats
constexpr std::size_t kRotKeys = 0xA8;     // CArray of 0x14 records: u16 frame, (+4 kept), 3 floats at +8
constexpr std::size_t kPosKeys = 0xBC;     // CArray of 0x18 records: u16 frame, +8 dword, 3 floats at +0xC
constexpr std::size_t kMorphKeys = 0xD0;   // CArray of 0x14 records: u16 frame, aligned point array at +4
constexpr std::size_t kNormals = 0xE4;     // CArray of 12-byte records (version 0x1468FCCA only)
} // namespace node

// Helper (element of the caller's +0x56C array, 0x9C bytes).
namespace helper {
constexpr std::size_t kSize = 0x9C;
constexpr std::size_t kName = 0x1C;        // CString
constexpr std::size_t kVectorB = 0x20;     // 3 dwords (file +0x0C)
constexpr std::size_t kValue2C = 0x2C;     // file +0x30
constexpr std::size_t kVectorA = 0x30;     // 3 dwords (file +0x00)
constexpr std::size_t kVectorC = 0x40;     // 3 dwords (file +0x18)
constexpr std::size_t kValue60 = 0x60;     // file +0x24
constexpr std::size_t kName2 = 0x8C;       // CString
constexpr std::size_t kName3 = 0x94;       // CString
} // namespace helper

constexpr std::uint32_t kVersionWithNormals = 0x1468FCCA;

// The loader's `this`: the caller's scene info; only its first word is written.
class SceneInfo {
public:
    // 00471B10. Reads the scene from `file` (an open CFile; only slot 13, Read, is used). `meshes`
    // and `nodes` get one element per record, `helpers` one per helper. Returns 1.
    mfc::BOOL Load(void* file, VtArray* meshes, VtArray* helpers, VtArray* nodes);

    std::uint16_t flags_;
};

// 00446C10. Loads the scene file into a CFakeUnit (its slot 62, 00446BA0, calls this) and builds the
// unit's figures from it. See scene_loader.cpp.
mfc::BOOL __stdcall LoadFakeUnitScene(const char* fileName, const char* registryName, void* fakeUnit);

} // namespace scene
