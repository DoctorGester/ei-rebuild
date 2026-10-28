// The corner tables of a fake unit scene's meshes (called by LoadFakeUnitScene once the scene is loaded):
// every distinct face corner (merged vertex, position, texture vertex) becomes a sorted entry of the mesh's
// corner table (+0x108, three words each), each face corner its index in that table (+0x128, words); and
// the distinct (merged vertex, position) pairs, sorted and padded to a multiple of four, go to +0x118 (two
// words each). The sets are MFC CMaps with the game's hash functions; the sorts are std::sort by the game's
// comparators. Names provisional; see docs/notes/objects.md ("Rewritten").
#include "scene_loader.h"

#include "../lib/containers.h"
#include "../lib/crt.h"
#include "../lib/mfc.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/mfc_list.h"
#include "lib/mfc_slots.h"
#include "game_data.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace scene {

namespace {

// The mesh fields written here: corners, pairs and cornerIndices (SceneMesh, scene_loader.h).
using Corner = MeshCornerKey;

// The mesh's arrays: the corner table (6-byte Corners), the
// corner indices (words) and the pairs (dwords).
using DwordArray = std::vector<std::uint32_t>;

// The three CMaps (mfc::CMap) and their associations:
//   corners: 64-bit key -> 64-bit value
//   indices: 64-bit key -> dword
//   pairs:   dword key -> dword
using CornerMap = mfc::CMap<std::uint64_t, std::uint64_t>;
using IndexMap = mfc::CMap<std::uint64_t, std::uint32_t>;
using PairMap = mfc::CMap<std::uint32_t, std::uint32_t>;
using Assoc64 = CornerMap::Assoc;
using AssocIndex = IndexMap::Assoc;
using Assoc32 = PairMap::Assoc;
static_assert(sizeof(Assoc64) == 0x18 && sizeof(AssocIndex) == 0x14 && sizeof(Assoc32) == 0x10);

// The sorted corner list (a CArray of 16-byte records): SetSize and SetAtGrow.
struct CornerRecord {
    std::int32_t vertex, position, texture;
    std::int32_t index;
};
using RecordArray = std::vector<CornerRecord>;

// The game's two qsort comparators as orders: the records by vertex, position, texture (ints), the
// pairs by their two words (vertex, then position; unsigned). The game's qsort leaves equal elements in an order of
// its own, but equal elements are the same here: two records that compare equal hold the same three fields, the
// only ones read after the sort (`index` is not), and the pairs are a map's distinct values.
bool CornerRecordBefore(const CornerRecord& a, const CornerRecord& b) {
    if (a.vertex != b.vertex) return a.vertex < b.vertex;
    if (a.position != b.position) return a.position < b.position;
    return a.texture < b.texture;
}
bool WordPairBefore(std::uint32_t a, std::uint32_t b) {
    const std::uint16_t aVertex = static_cast<std::uint16_t>(a);
    const std::uint16_t bVertex = static_cast<std::uint16_t>(b);
    if (aVertex != bVertex) return aVertex < bVertex;
    return (a >> 16) < (b >> 16);
}

template <typename MapT>
MapT NewMap(void* vtable) { return MapT{vtable, nullptr, 17, 0, nullptr, nullptr, 10}; }

// The inlined destructor of the second and third maps: RemoveAll and the base vtable.
template <typename MapT>
void DestroyMap(MapT& map, void* vtable) {
    map.vtable = vtable;
    map.RemoveAll();
}

// map[key] (operator[], inlined in the game; the maps' GetAssocAt, InitHashTable and NewAssoc): the value, created
// when missing.
template <typename K, typename V>
V& Lookup(mfc::CMap<K, V>& map, K key) {
    return map.ElementAt(key);
}

const MeshFace& FaceAt(SceneMesh* mesh, int f) { return mesh->faces[f]; }

// The corner key: ((vertex << 16) | position) << 16 | texture vertex, in 64 bits (via __allshl), the
// indices zero-extended.
std::uint64_t CornerKey(const MeshFace& face, int k) {
    std::uint64_t key = (static_cast<std::uint64_t>(face.vertex[k]) << 16) | face.position[k];
    return (key << 16) | face.vertex2[k];
}

// The index of the corner (vertex, position, texture) in the table, -1 when absent.
int FindCorner(SceneMesh* mesh, std::uint32_t vertex, std::uint32_t position, std::uint32_t texture) {
    const std::vector<Corner>& table = mesh->corners;
    const Corner* c = table.data();
    for (int i = 0; i < std::ssize(table); ++i) {
        if (c[i].vertex == vertex && c[i].position == position && c[i].texture == texture) return i;
    }
    return -1;
}

// Steps 1-3: the distinct corners, sorted, into the corner table (`indices` maps each corner key
// to its place before the sort; nothing reads it).
void BuildCornerTable(SceneMesh* mesh, RecordArray& records, CornerMap& corners, IndexMap& indices) {
    for (int f = 0; f < std::ssize(mesh->faces); ++f) {
        for (int k = 0; k < 3; ++k) {
            std::uint64_t key = CornerKey(FaceAt(mesh, f), k);
            Lookup(corners, key) = key;
        }
    }

    records.clear();
    if (corners.count != 0) {
        for (Assoc64* a = corners.Next(nullptr); a; a = corners.Next(a)) {
            std::uint64_t key = a->key;
            CornerRecord r;
            r.vertex = static_cast<std::int32_t>(static_cast<std::uint32_t>(key >> 32) & 0xFFFF);
            r.position = static_cast<std::int32_t>(static_cast<std::uint32_t>(key >> 16) & 0xFFFF);
            r.texture = static_cast<std::int32_t>(static_cast<std::uint32_t>(key) & 0xFFFF);
            r.index = std::ssize(records);
            lib::SetAtGrow(records, std::ssize(records), r);
            Lookup(indices, key) = static_cast<std::uint32_t>(r.index);
        }
    }
    std::sort(records.begin(), records.end(), CornerRecordBefore);

    mesh->corners.resize(std::ssize(records));
    for (int i = 0; i < std::ssize(records); ++i) {
        Corner* c = mesh->corners.data() + i;
        c->vertex = static_cast<std::uint16_t>(records[i].vertex);
        c->position = static_cast<std::uint16_t>(records[i].position);
        c->texture = static_cast<std::uint16_t>(records[i].texture);
    }
}

// Step 4: each face corner's index in the table (-1 when absent), appended to +0x128.
void IndexFaceCorners(SceneMesh* mesh) {
    mesh->cornerIndices.clear();
    for (int f = 0; f < std::ssize(mesh->faces); ++f) {
        for (int k = 0; k < 3; ++k) {
            const MeshFace& face = FaceAt(mesh, f);
            int index = FindCorner(mesh, face.vertex[k], face.position[k], face.vertex2[k]);
            std::vector<std::uint16_t>& out = mesh->cornerIndices;
            lib::SetAtGrow(out, std::ssize(out), static_cast<unsigned short>(index));
        }
    }
}

// Step 5: the distinct (vertex, position) pairs, sorted, padded to a multiple of four with copies
// of the last vertex and following positions, into +0x118.
void BuildPairs(SceneMesh* mesh) {
    PairMap pairs = NewMap<PairMap>(mfc::impl::CornerMapCClass::Vtable());
    for (int f = 0; f < std::ssize(mesh->faces); ++f) {
        for (int k = 0; k < 3; ++k) {
            const MeshFace& face = FaceAt(mesh, f);
            std::uint32_t key = (face.vertex[k] << 16) | face.position[k];
            Lookup(pairs, key) = key;
        }
    }

    DwordArray list;
    if (pairs.count != 0) {
        for (Assoc32* a = pairs.Next(nullptr); a; a = pairs.Next(a)) {
            std::uint32_t value = a->value;
            std::uint32_t pair = (value >> 16) | (value << 16);   // words: vertex, position
            int at = std::ssize(list);
            list.resize(at + 1);
            list[at] = pair;
        }
    }
    std::sort(list.begin(), list.end(), WordPairBefore);
    if (std::ssize(list) & 3) {
        int missing = 4 - (std::ssize(list) & 3);
        const std::uint32_t last = list[std::ssize(list) - 1];   // words: vertex, position
        std::uint16_t vertex = static_cast<std::uint16_t>(last);
        std::uint16_t position = static_cast<std::uint16_t>(last >> 16);
        for (int i = 0; i < missing; ++i) {
            ++position;
            int at = std::ssize(list);
            list.resize(at + 1);
            list[at] = static_cast<std::uint32_t>(vertex) | (static_cast<std::uint32_t>(position) << 16);
        }
    }
    DwordArray& out = mesh->pairs;
    out.resize(std::ssize(list));
    std::memcpy(out.data(), list.data(), static_cast<std::size_t>(std::ssize(list)) * 4);
    lib::FreeStorage(list);
    DestroyMap(pairs, mfc::impl::CornerMapCClass::Vtable());
}

} // namespace

// The record list is shared by all meshes and freed at the end.
void IndexMeshCorners(MeshArray* meshes) {
    RecordArray records{};
    for (int m = 0; m < meshes->size; ++m) {
        CornerMap corners = NewMap<CornerMap>(mfc::impl::CornerMapAClass::Vtable());
        IndexMap indices = NewMap<IndexMap>(mfc::impl::CornerMapBClass::Vtable());
        BuildCornerTable(&meshes->data[m], records, corners, indices);
        IndexFaceCorners(&meshes->data[m]);
        BuildPairs(&meshes->data[m]);
        DestroyMap(indices, mfc::impl::CornerMapBClass::Vtable());
        corners.vtable = mfc::impl::CornerMapAClass::Vtable();
        corners.RemoveAll();
    }
}

} // namespace scene
