// The corner tables of a fake unit scene's meshes (0044CFF0, called by LoadFakeUnitScene 00446C10
// once the scene is loaded): every distinct face corner (merged vertex, position, texture vertex)
// becomes a sorted entry of the mesh's corner table (+0x108, three words each), each face corner
// its index in that table (+0x128, words); and the distinct (merged vertex, position) pairs, sorted
// and padded to a multiple of four, go to +0x118 (two words each). The sets are MFC CMaps with the
// game's hash functions; the sorts are the game's qsort with the original comparators. Names
// provisional; see docs/notes/objects.md ("Fifth round").
#include "scene_loader.h"

#include "../lib/crt.h"
#include "ei/hook.h"
#include "originals_library.h"

#include <cstdint>
#include <cstring>

namespace scene {
namespace {

template <typename T>
T& At(void* base, std::size_t offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(base) + offset);
}

// Mesh fields written here.
constexpr std::size_t kCornerTable = 0x108;   // aligned array of Corner (6 bytes)
constexpr std::size_t kPairs = 0x118;         // aligned array of dwords (two words)
constexpr std::size_t kCornerIndices = 0x128; // aligned array of words, three per face
constexpr std::size_t kFaceCount = mesh::kFaces + 4;

struct Corner {
    std::uint16_t vertex, position, texture;
};
static_assert(sizeof(Corner) == 6);

// The engine's aligned array (data 32-byte aligned, raw block before it).
struct AlignedArray {
    char* data;
    int size;
    int capacity;
    int growBy;
};

// The three CMaps (MFC layout, 0x1C bytes) and their associations:
//   corners (vtable 0073C190): 64-bit key -> 64-bit value (0x18 bytes)
//   indices (vtable 0073C184): 64-bit key -> dword (0x14 bytes)
//   pairs   (vtable 0073C178): dword key -> dword (0x10 bytes)
struct Map {
    std::uintptr_t vtable;
    void** table;
    std::uint32_t hashSize;
    int count;
    void* freeList;
    mfc::CPlex* blocks;
    int blockSize;
};
static_assert(sizeof(Map) == 0x1C);
#pragma pack(push, 4)   // the 64-bit keys sit at +8 with dword alignment
struct Assoc64 {
    Assoc64* next;
    std::uint32_t hash;
    std::uint64_t key;
    std::uint64_t value;
};
struct AssocIndex {
    AssocIndex* next;
    std::uint32_t hash;
    std::uint64_t key;
    std::uint32_t value;
};
struct Assoc32 {
    Assoc32* next;
    std::uint32_t hash;
    std::uint32_t key;
    std::uint32_t value;
};
#pragma pack(pop)
static_assert(sizeof(Assoc64) == 0x18 && sizeof(AssocIndex) == 0x14 && sizeof(Assoc32) == 0x10);

// Each map's GetAssocAt [key, out hash], InitHashTable [size, allocate] and NewAssoc, and the
// first map's destructor; the other two destructors are inlined.
__declspec(naked) Assoc64* __fastcall CornersGetAssocAt(Map*, void*, const std::uint64_t*, std::uint32_t*) EI_ORIGINAL(0x004540A0)
__declspec(naked) void __fastcall CornersInitHashTable(Map*, void*, std::uint32_t, int) EI_ORIGINAL(0x00453FD0)
__declspec(naked) Assoc64* __fastcall CornersNewAssoc(Map*) EI_ORIGINAL(0x00454040)
__declspec(naked) void __fastcall CornersDestruct(Map*) EI_ORIGINAL(0x00453F80)
__declspec(naked) AssocIndex* __fastcall IndicesGetAssocAt(Map*, void*, const std::uint64_t*, std::uint32_t*) EI_ORIGINAL(0x004541C0)
__declspec(naked) void __fastcall IndicesInitHashTable(Map*, void*, std::uint32_t, int) EI_ORIGINAL(0x004540F0)
__declspec(naked) AssocIndex* __fastcall IndicesNewAssoc(Map*) EI_ORIGINAL(0x00454160)
__declspec(naked) Assoc32* __fastcall PairsGetAssocAt(Map*, void*, const std::uint32_t*, std::uint32_t*) EI_ORIGINAL(0x004542E0)
__declspec(naked) void __fastcall PairsInitHashTable(Map*, void*, std::uint32_t, int) EI_ORIGINAL(0x00454210)
__declspec(naked) Assoc32* __fastcall PairsNewAssoc(Map*) EI_ORIGINAL(0x00454280)

// The sorted corner list (a CArray of 16-byte records, vtable 0073C19C): SetSize (00453CA0) and
// SetAtGrow (00453E00).
struct CornerRecord {
    std::int32_t vertex, position, texture;
    std::int32_t index;
};
struct RecordArray {
    std::uintptr_t vtable;
    CornerRecord* data;
    int size;
    int capacity;
    int growBy;
};
__declspec(naked) void __fastcall RecordsSetSize(RecordArray*, void*, int, int) EI_ORIGINAL(0x00453CA0)
__declspec(naked) void __fastcall RecordsSetAtGrow(RecordArray*, void*, int, const CornerRecord*) EI_ORIGINAL(0x00453E00)

// The mesh's arrays: the corner table's allocation [00452E90, 6-byte elements], the index array's
// SetSize [0044FA90] and SetAtGrow [00452FC0], the dword array's SetSize [00452EB0].
__declspec(naked) char* __fastcall AllocCorners(int) EI_ORIGINAL(0x00452E90)
__declspec(naked) void __fastcall WordsSetSize(AlignedArray*, void*, int, int) EI_ORIGINAL(0x0044FA90)
__declspec(naked) void __fastcall WordsSetAtGrow(AlignedArray*, void*, int, int) EI_ORIGINAL(0x00452FC0)
__declspec(naked) void __fastcall DwordsSetSize(AlignedArray*, void*, int, int) EI_ORIGINAL(0x00452EB0)
// The game's qsort and the two comparators: records by vertex, position, texture as ints; pairs by
// their two words, unsigned.
void __cdecl GameQsort(void* a0, unsigned a1, unsigned a2, int(__cdecl* a3)(const void*, const void*)) { GameQsortOriginal(a0, a1, a2, a3); }
auto* const kCompareRecords = reinterpret_cast<int(__cdecl*)(const void*, const void*)>(0x0044CF00);
auto* const kComparePairs = reinterpret_cast<int(__cdecl*)(const void*, const void*)>(0x0044CF60);

Map NewMap(std::uintptr_t vtable) { return Map{vtable, nullptr, 17, 0, nullptr, nullptr, 10}; }

// The inlined destructor of the second and third maps: RemoveAll (the table deleted even when
// null) and the blocks freed.
void DestroyMap(Map& map, std::uintptr_t vtable) {
    map.vtable = vtable;
    gamecrt::operator_delete(map.table);
    map.table = nullptr;
    map.count = 0;
    map.freeList = nullptr;
    mfc::PlexFreeDataChain(map.blocks);
    map.blocks = nullptr;
}

// map[key] (operator[], inlined by the original): the association, created when missing.
template <typename AssocT, typename KeyT>
AssocT* Lookup(Map& map, KeyT key, AssocT* (__fastcall* getAssocAt)(Map*, void*, const KeyT*, std::uint32_t*),
               void(__fastcall* initHashTable)(Map*, void*, std::uint32_t, int), AssocT* (__fastcall* newAssoc)(Map*)) {
    std::uint32_t hash;
    AssocT* assoc = getAssocAt(&map, nullptr, &key, &hash);
    if (assoc) return assoc;
    if (!map.table) initHashTable(&map, nullptr, map.hashSize, 1);
    assoc = newAssoc(&map);
    assoc->hash = hash;
    assoc->key = key;
    assoc->next = static_cast<AssocT*>(map.table[hash]);
    map.table[hash] = assoc;
    return assoc;
}

// GetNextAssoc as inlined: the association after `assoc` (the next in its chain, else the first of
// a later bucket), or null. `assoc` null starts at the first bucket.
template <typename AssocT>
AssocT* Next(const Map& map, AssocT* assoc) {
    if (!assoc) {
        for (std::uint32_t b = 0; b < map.hashSize; ++b) {
            if (map.table[b]) return static_cast<AssocT*>(map.table[b]);
        }
        return nullptr;
    }
    if (assoc->next) return assoc->next;
    for (std::uint32_t b = assoc->hash + 1; b < map.hashSize; ++b) {
        if (map.table[b]) return static_cast<AssocT*>(map.table[b]);
    }
    return nullptr;
}

const MeshFace& FaceAt(char* mesh, int f) { return At<MeshFace*>(mesh, mesh::kFaces)[f]; }

// The corner key: ((vertex << 16) | position) << 16 | texture vertex, in 64 bits (the original's
// __allshl), the indices zero-extended.
std::uint64_t CornerKey(const MeshFace& face, int k) {
    std::uint64_t key = (static_cast<std::uint64_t>(face.vertex[k]) << 16) | face.position[k];
    return (key << 16) | face.vertex2[k];
}

// The index of the corner (vertex, position, texture) in the table, -1 when absent.
int FindCorner(char* mesh, std::uint32_t vertex, std::uint32_t position, std::uint32_t texture) {
    const AlignedArray& table = At<AlignedArray>(mesh, kCornerTable);
    const auto* c = reinterpret_cast<const Corner*>(table.data);
    for (int i = 0; i < table.size; ++i) {
        if (c[i].vertex == vertex && c[i].position == position && c[i].texture == texture) return i;
    }
    return -1;
}

int GrowthStep(const AlignedArray& a) {
    if (a.growBy != 0) return a.growBy;
    int step = a.size / 8;
    if (step < 4) return 4;
    if (step > 0x400) return 0x400;
    return step;
}

// SetSize of the corner table as inlined: kept contents, 0 frees.
void SetCornerTableSize(AlignedArray& a, int size) {
    if (size == 0) {
        if (a.data) {
            gamecrt::operator_delete(reinterpret_cast<void**>(a.data)[-1]);
            a.data = nullptr;
        }
        a.capacity = 0;
        a.size = 0;
        return;
    }
    if (!a.data) {
        a.data = AllocCorners(size);
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
    char* data = AllocCorners(capacity);
    std::memcpy(data, a.data, static_cast<std::size_t>(a.size) * sizeof(Corner));
    gamecrt::operator_delete(reinterpret_cast<void**>(a.data)[-1]);
    a.data = data;
    a.size = size;
    a.capacity = capacity;
}

// Steps 1-3: the distinct corners, sorted, into the corner table (`indices` maps each corner key
// to its place before the sort; nothing reads it).
void BuildCornerTable(char* mesh, RecordArray& records, Map& corners, Map& indices) {
    for (int f = 0; f < At<int>(mesh, kFaceCount); ++f) {
        for (int k = 0; k < 3; ++k) {
            std::uint64_t key = CornerKey(FaceAt(mesh, f), k);
            Lookup(corners, key, &CornersGetAssocAt, &CornersInitHashTable, &CornersNewAssoc)->value = key;
        }
    }

    RecordsSetSize(&records, nullptr, 0, -1);
    if (corners.count != 0) {
        for (Assoc64* a = Next<Assoc64>(corners, nullptr); a; a = Next(corners, a)) {
            std::uint64_t key = a->key;
            CornerRecord r;
            r.vertex = static_cast<std::int32_t>(static_cast<std::uint32_t>(key >> 32) & 0xFFFF);
            r.position = static_cast<std::int32_t>(static_cast<std::uint32_t>(key >> 16) & 0xFFFF);
            r.texture = static_cast<std::int32_t>(static_cast<std::uint32_t>(key) & 0xFFFF);
            r.index = records.size;
            RecordsSetAtGrow(&records, nullptr, records.size, &r);
            Lookup(indices, key, &IndicesGetAssocAt, &IndicesInitHashTable, &IndicesNewAssoc)->value = static_cast<std::uint32_t>(r.index);
        }
    }
    GameQsort(records.data, static_cast<unsigned>(records.size), sizeof(CornerRecord), kCompareRecords);

    SetCornerTableSize(At<AlignedArray>(mesh, kCornerTable), records.size);
    for (int i = 0; i < records.size; ++i) {
        Corner* c = reinterpret_cast<Corner*>(At<AlignedArray>(mesh, kCornerTable).data) + i;
        c->vertex = static_cast<std::uint16_t>(records.data[i].vertex);
        c->position = static_cast<std::uint16_t>(records.data[i].position);
        c->texture = static_cast<std::uint16_t>(records.data[i].texture);
    }
}

// Step 4: each face corner's index in the table (-1 when absent), appended to +0x128.
void IndexFaceCorners(char* mesh) {
    WordsSetSize(&At<AlignedArray>(mesh, kCornerIndices), nullptr, 0, -1);
    for (int f = 0; f < At<int>(mesh, kFaceCount); ++f) {
        for (int k = 0; k < 3; ++k) {
            const MeshFace& face = FaceAt(mesh, f);
            int index = FindCorner(mesh, face.vertex[k], face.position[k], face.vertex2[k]);
            AlignedArray& out = At<AlignedArray>(mesh, kCornerIndices);
            WordsSetAtGrow(&out, nullptr, out.size, index);
        }
    }
}

// Step 5: the distinct (vertex, position) pairs, sorted, padded to a multiple of four with copies
// of the last vertex and following positions, into +0x118.
void BuildPairs(char* mesh) {
    Map pairs = NewMap(0x0073C178);
    for (int f = 0; f < At<int>(mesh, kFaceCount); ++f) {
        for (int k = 0; k < 3; ++k) {
            const MeshFace& face = FaceAt(mesh, f);
            std::uint32_t key = (face.vertex[k] << 16) | face.position[k];
            Lookup(pairs, key, &PairsGetAssocAt, &PairsInitHashTable, &PairsNewAssoc)->value = key;
        }
    }

    AlignedArray list{nullptr, 0, 0, 0};
    if (pairs.count != 0) {
        for (Assoc32* a = Next<Assoc32>(pairs, nullptr); a; a = Next(pairs, a)) {
            std::uint32_t value = a->value;
            std::uint32_t pair = (value >> 16) | (value << 16);   // words: vertex, position
            int at = list.size;
            DwordsSetSize(&list, nullptr, at + 1, -1);
            reinterpret_cast<std::uint32_t*>(list.data)[at] = pair;
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
            DwordsSetSize(&list, nullptr, at + 1, -1);
            reinterpret_cast<std::uint32_t*>(list.data)[at] = static_cast<std::uint32_t>(vertex) |
                                                               (static_cast<std::uint32_t>(position) << 16);
        }
    }
    AlignedArray& out = At<AlignedArray>(mesh, kPairs);
    DwordsSetSize(&out, nullptr, list.size, -1);
    std::memcpy(out.data, list.data, static_cast<std::size_t>(list.size) * 4);
    if (list.data) gamecrt::operator_delete(reinterpret_cast<void**>(list.data)[-1]);
    DestroyMap(pairs, 0x0073C178);
}

} // namespace

// 0044CFF0 (__fastcall: the unit's mesh array). The record list (vtable 0073C19C) is shared by all
// meshes and freed at the end.
void __fastcall IndexMeshCorners(VtArray* meshes) {
    RecordArray records{0x0073C19C, nullptr, 0, 0, 0};
    for (int m = 0; m < meshes->body.size; ++m) {
        Map corners = NewMap(0x0073C190);
        Map indices = NewMap(0x0073C184);
        BuildCornerTable(static_cast<char*>(meshes->body.data) + m * mesh::kSize, records, corners, indices);
        IndexFaceCorners(static_cast<char*>(meshes->body.data) + m * mesh::kSize);
        BuildPairs(static_cast<char*>(meshes->body.data) + m * mesh::kSize);
        DestroyMap(indices, 0x0073C184);
        corners.vtable = 0x0073C190;
        CornersDestruct(&corners);
    }
    records.vtable = 0x0073C19C;
    if (records.data) gamecrt::operator_delete(records.data);
}
EI_REPLACE(0x0044CFF0, IndexMeshCorners);

} // namespace scene
