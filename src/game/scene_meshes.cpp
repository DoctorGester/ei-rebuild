// The corner tables of a fake unit scene's meshes (0044CFF0, called by LoadFakeUnitScene 00446C10
// once the scene is loaded): every distinct face corner (merged vertex, position, texture vertex)
// becomes a sorted entry of the mesh's corner table (+0x108, three words each), each face corner
// its index in that table (+0x128, words); and the distinct (merged vertex, position) pairs, sorted
// and padded to a multiple of four, go to +0x118 (two words each). The sets are MFC CMaps with the
// game's hash functions; the sorts are the game's qsort with the original comparators. Names
// provisional; see docs/notes/objects.md ("Rewritten").
#include "scene_loader.h"

#include "../lib/containers.h"
#include "../lib/crt.h"
#include "../lib/mfc.h"
#include "ei/hook.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/mfc_list.h"
#include "originals_library.h"
#include "game_data.h"

#include <cstdint>
#include <cstring>

namespace scene {
namespace {

// The mesh fields written here: corners, pairs and cornerIndices (SceneMesh, scene_loader.h).
using Corner = MeshCornerKey;

// The mesh's arrays are engine aligned arrays (lib::AlignedArray): the corner table (6-byte
// Corners; its inlined SetSize allocates through 00452E90), the corner indices (words) and the
// pairs (dwords).
using CornerTable = lib::AlignedArray<Corner>;
using WordArray = lib::AlignedArray<std::uint16_t>;
using DwordArray = lib::AlignedArray<std::uint32_t>;

// The three CMaps (mfc::CMap) and their associations:
//   corners (vtable 0073C190): 64-bit key -> 64-bit value (0x18 bytes)
//   indices (vtable 0073C184): 64-bit key -> dword (0x14 bytes)
//   pairs   (vtable 0073C178): dword key -> dword (0x10 bytes)
using CornerMap = mfc::CMap<std::uint64_t, std::uint64_t>;
using IndexMap = mfc::CMap<std::uint64_t, std::uint32_t>;
using PairMap = mfc::CMap<std::uint32_t, std::uint32_t>;
using Assoc64 = CornerMap::Assoc;
using AssocIndex = IndexMap::Assoc;
using Assoc32 = PairMap::Assoc;
static_assert(sizeof(Assoc64) == 0x18 && sizeof(AssocIndex) == 0x14 && sizeof(Assoc32) == 0x10);

// The sorted corner list (a CArray of 16-byte records, vtable 0073C19C): SetSize (00453CA0) and
// SetAtGrow (00453E00).
struct CornerRecord {
    std::int32_t vertex, position, texture;
    std::int32_t index;
};
using RecordArray = mfc::CArray<CornerRecord>;

// The game's qsort and the two comparators: records by vertex, position, texture as ints; pairs by
// their two words, unsigned.
void __cdecl GameQsort(void* a0, unsigned a1, unsigned a2, int(__cdecl* a3)(const void*, const void*)) { libcrt::Qsort(a0, a1, a2, a3); }
auto* const kCompareRecords = reinterpret_cast<int(__cdecl*)(const void*, const void*)>(0x0044CF00);
auto* const kComparePairs = reinterpret_cast<int(__cdecl*)(const void*, const void*)>(0x0044CF60);

template <typename MapT>
MapT NewMap(std::uintptr_t vtable) { return MapT{ei::GamePointer<void>(vtable), nullptr, 17, 0, nullptr, nullptr, 10}; }

// The inlined destructor of the second and third maps: RemoveAll and the base vtable.
template <typename MapT>
void DestroyMap(MapT& map, std::uintptr_t vtableAddress) {
    map.vtable = ei::GamePointer<void>(vtableAddress);
    map.RemoveAll();
}

// map[key] (operator[], inlined by the original; the maps' GetAssocAt, InitHashTable and NewAssoc:
// corners 004540A0 / 00453FD0 / 00454040, indices 004541C0 / 004540F0 / 00454160, pairs 004542E0 /
// 00454210 / 00454280): the value, created when missing.
template <typename K, typename V>
V& Lookup(mfc::CMap<K, V>& map, K key) {
    return *mfc::impl::MapAbi<K, V>::ElementAt(&map, nullptr, &key);
}

const MeshFace& FaceAt(SceneMesh* mesh, int f) { return mesh->faces.data[f]; }

// The corner key: ((vertex << 16) | position) << 16 | texture vertex, in 64 bits (the original's
// __allshl), the indices zero-extended.
std::uint64_t CornerKey(const MeshFace& face, int k) {
    std::uint64_t key = (static_cast<std::uint64_t>(face.vertex[k]) << 16) | face.position[k];
    return (key << 16) | face.vertex2[k];
}

// The index of the corner (vertex, position, texture) in the table, -1 when absent.
int FindCorner(SceneMesh* mesh, std::uint32_t vertex, std::uint32_t position, std::uint32_t texture) {
    const CornerTable& table = mesh->corners;
    const Corner* c = table.data;
    for (int i = 0; i < table.size; ++i) {
        if (c[i].vertex == vertex && c[i].position == position && c[i].texture == texture) return i;
    }
    return -1;
}

// Steps 1-3: the distinct corners, sorted, into the corner table (`indices` maps each corner key
// to its place before the sort; nothing reads it).
void BuildCornerTable(SceneMesh* mesh, RecordArray& records, CornerMap& corners, IndexMap& indices) {
    for (int f = 0; f < mesh->faces.size; ++f) {
        for (int k = 0; k < 3; ++k) {
            std::uint64_t key = CornerKey(FaceAt(mesh, f), k);
            Lookup(corners, key) = key;
        }
    }

    records.SetSize(0, -1);
    if (corners.count != 0) {
        for (Assoc64* a = corners.Next(nullptr); a; a = corners.Next(a)) {
            std::uint64_t key = a->key;
            CornerRecord r;
            r.vertex = static_cast<std::int32_t>(static_cast<std::uint32_t>(key >> 32) & 0xFFFF);
            r.position = static_cast<std::int32_t>(static_cast<std::uint32_t>(key >> 16) & 0xFFFF);
            r.texture = static_cast<std::int32_t>(static_cast<std::uint32_t>(key) & 0xFFFF);
            r.index = records.size;
            records.SetAtGrow(records.size, r);   // 00453E00
            Lookup(indices, key) = static_cast<std::uint32_t>(r.index);
        }
    }
    GameQsort(records.data, static_cast<unsigned>(records.size), sizeof(CornerRecord), kCompareRecords);

    mesh->corners.SetSize(records.size);
    for (int i = 0; i < records.size; ++i) {
        Corner* c = mesh->corners.data + i;
        c->vertex = static_cast<std::uint16_t>(records.data[i].vertex);
        c->position = static_cast<std::uint16_t>(records.data[i].position);
        c->texture = static_cast<std::uint16_t>(records.data[i].texture);
    }
}

// Step 4: each face corner's index in the table (-1 when absent), appended to +0x128.
void IndexFaceCorners(SceneMesh* mesh) {
    mesh->cornerIndices.SetSize(0, -1);
    for (int f = 0; f < mesh->faces.size; ++f) {
        for (int k = 0; k < 3; ++k) {
            const MeshFace& face = FaceAt(mesh, f);
            int index = FindCorner(mesh, face.vertex[k], face.position[k], face.vertex2[k]);
            WordArray& out = mesh->cornerIndices;
            WordsSetAtGrow(&out, nullptr, out.size, index);
        }
    }
}

// Step 5: the distinct (vertex, position) pairs, sorted, padded to a multiple of four with copies
// of the last vertex and following positions, into +0x118.
void BuildPairs(SceneMesh* mesh) {
    PairMap pairs = NewMap<PairMap>(0x0073C178);
    for (int f = 0; f < mesh->faces.size; ++f) {
        for (int k = 0; k < 3; ++k) {
            const MeshFace& face = FaceAt(mesh, f);
            std::uint32_t key = (face.vertex[k] << 16) | face.position[k];
            Lookup(pairs, key) = key;
        }
    }

    DwordArray list{nullptr, 0, 0, 0};
    if (pairs.count != 0) {
        for (Assoc32* a = pairs.Next(nullptr); a; a = pairs.Next(a)) {
            std::uint32_t value = a->value;
            std::uint32_t pair = (value >> 16) | (value << 16);   // words: vertex, position
            int at = list.size;
            list.SetSize(at + 1, -1);
            list.data[at] = pair;
        }
    }
    GameQsort(list.data, static_cast<unsigned>(list.size), 4, kComparePairs);
    if (list.size & 3) {
        int missing = 4 - (list.size & 3);
        const auto* last = reinterpret_cast<const std::uint16_t*>(list.data) + (list.size - 1) * 2;
        std::uint16_t vertex = last[0];
        std::uint16_t position = last[1];
        for (int i = 0; i < missing; ++i) {
            ++position;
            int at = list.size;
            list.SetSize(at + 1, -1);
            list.data[at] = static_cast<std::uint32_t>(vertex) |
                                                               (static_cast<std::uint32_t>(position) << 16);
        }
    }
    DwordArray& out = mesh->pairs;
    out.SetSize(list.size, -1);
    std::memcpy(out.data, list.data, static_cast<std::size_t>(list.size) * 4);
    list.FreeData();
    DestroyMap(pairs, 0x0073C178);
}

} // namespace

// 0044CFF0 (__fastcall: the unit's mesh array). The record list (vtable 0073C19C) is shared by all
// meshes and freed at the end.
void __fastcall IndexMeshCorners(VtArray* meshes) {
    RecordArray records{ei::GamePointer<void>(0x0073C19C), nullptr, 0, 0, 0};
    for (int m = 0; m < meshes->size; ++m) {
        CornerMap corners = NewMap<CornerMap>(0x0073C190);
        IndexMap indices = NewMap<IndexMap>(0x0073C184);
        BuildCornerTable(&Typed<MeshArray>(*meshes).data[m], records, corners, indices);
        IndexFaceCorners(&Typed<MeshArray>(*meshes).data[m]);
        BuildPairs(&Typed<MeshArray>(*meshes).data[m]);
        DestroyMap(indices, 0x0073C184);
        corners.vtable = ei::GamePointer<void>(0x0073C190);
        corners.RemoveAll();   // 00453F80
    }
    records.vtable = ei::GamePointer<void>(0x0073C19C);
    if (records.data) gamecrt::operator_delete(records.data);
}
EI_REWRITTEN(0x0044CFF0, IndexMeshCorners);

// 0044CF00 / 0044CF60 (no Ghidra functions; S16c): the two qsort comparators above (kCompareRecords,
// kComparePairs; called through their addresses so the checks run). 1, -1 or 0 by the first field
// that differs: the records' vertex, position and texture as ints, the pairs' two words unsigned.
namespace {
template <typename T>
int CompareFields(const T* a, const T* b, int count) {
    for (int i = 0; i < count; ++i) {
        if (a[i] > b[i]) return 1;
        if (a[i] < b[i]) return -1;
    }
    return 0;
}
} // namespace
int __cdecl CompareCornerRecords(const void* a, const void* b) {
    const auto* x = static_cast<const CornerRecord*>(a);
    const auto* y = static_cast<const CornerRecord*>(b);
    const std::int32_t first[3] = {x->vertex, x->position, x->texture};
    const std::int32_t second[3] = {y->vertex, y->position, y->texture};
    return CompareFields(first, second, 3);
}
int __cdecl CompareWordPairs(const void* a, const void* b) {
    return CompareFields(static_cast<const std::uint16_t*>(a), static_cast<const std::uint16_t*>(b), 2);
}
EI_REPLACE(0x0044CF00, CompareCornerRecords);
EI_REPLACE(0x0044CF60, CompareWordPairs);

} // namespace scene
