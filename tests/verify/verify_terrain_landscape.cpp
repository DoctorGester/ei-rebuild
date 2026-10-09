// Verify wrappers for the landscape object's members and its aligned arrays (terrain_landscape.cpp).
#include "verify.h"

#include "../../src/game/terrain.h"
#include "../../src/game/terrain_sector.h"

#include <cstdint>
#include <cstring>

namespace {

using Words = lib::AlignedArray<std::uint32_t>;

// A private copy of an aligned array: the header, and its block copied (the original may grow or
// free it, so it must not share ours).
template <typename T>
lib::AlignedArray<T> CopyArray(const lib::AlignedArray<T>& source) {
    lib::AlignedArray<T> copy = source;
    if (source.data) {
        copy.data = lib::AlignedArray<T>::Allocate(source.capacity);
        std::memcpy(static_cast<void*>(copy.data), source.data, static_cast<std::size_t>(source.size) * sizeof(T));
    }
    return copy;
}

// The header and the first `size` elements agree.
template <typename T>
bool SameArray(const lib::AlignedArray<T>& a, const lib::AlignedArray<T>& b) {
    return a.size == b.size && a.capacity == b.capacity && a.growBy == b.growBy && !a.data == !b.data &&
           (!a.data || a.size <= 0 || std::memcmp(a.data, b.data, static_cast<std::size_t>(a.size) * sizeof(T)) == 0);
}

template <typename T>
void FreeCopy(lib::AlignedArray<T>& copy) {
    if (copy.data) lib::AlignedArray<T>::Free(copy.data);
}

// ---- Terrain::SetVisibleSectors ------------------------------------------------------------------
// 006CA260: PUSH EBP; MOV EBP,ESP; SUB ESP,0xC (6 bytes). Every frame from the draw pass (all
// scenarios). The original runs on a raw copy of the object whose two arrays have their own blocks
// (it reads only +0x104 / +0x108 and writes +0xAA4..+0xAC4), ours on the real one; the first row and
// both arrays (header and elements) compared.
void(__fastcall* s_setVisible)(Terrain*, void*, float, float, float);

void __fastcall SetVisibleSectorsVerify(Terrain* self, void*, float x, float y, float radius) {
    alignas(Terrain) static unsigned char scratchBytes[sizeof(Terrain)];
    std::memcpy(scratchBytes, static_cast<void*>(self), sizeof(Terrain));
    auto* scratch = reinterpret_cast<Terrain*>(scratchBytes);
    scratch->visibleColumnFirst = CopyArray(self->visibleColumnFirst);
    scratch->visibleColumnLast = CopyArray(self->visibleColumnLast);
    s_setVisible(scratch, nullptr, x, y, radius);
    self->SetVisibleSectors(x, y, radius);
    bool same = scratch->visibleRowFirst == self->visibleRowFirst &&
                SameArray(scratch->visibleColumnFirst, self->visibleColumnFirst) &&
                SameArray(scratch->visibleColumnLast, self->visibleColumnLast);
    ei::verify::Check("Terrain::SetVisibleSectors", true, same, "(%g, %g) r %g: row %d/%d, rows %d/%d", x, y, radius,
                      scratch->visibleRowFirst, self->visibleRowFirst, scratch->visibleColumnFirst.size,
                      self->visibleColumnFirst.size);
    FreeCopy(scratch->visibleColumnFirst);
    FreeCopy(scratch->visibleColumnLast);
}
EI_VERIFY(0x006CA260, SetVisibleSectorsVerify, 6, s_setVisible);

// ---- Terrain::SectorAt ------------------------------------------------------------------------------
// 006C8140: ours only (it loads, builds, reuses and frees sectors). Every terrain query of every
// scenario reaches it. Checked: a returned sector is (sx, sy) and is the grid's entry; every 64th
// call the whole cache: as many nodes as its count, each the grid's entry for its sector, linked
// both ways, the frame stamps not increasing towards the tail.
TerrainSector*(__fastcall* s_sectorAt)(Terrain*, void*, int, int, std::uint32_t, int);

void CheckSectorCache(Terrain* self) {
    int nodes = 0;
    bool linked = true, gridded = true, ordered = true;
    Terrain::SectorCache::Node* prev = nullptr;
    for (Terrain::SectorCache::Node* n = self->sectorCache.head; n && nodes <= self->sectorCache.count; n = n->next) {
        ++nodes;
        if (n->prev != prev) linked = false;
        if (self->sectorGrid[n->data->sx][n->data->sy] != n) gridded = false;
        if (prev && prev->data->lastUsed < n->data->lastUsed) ordered = false;
        prev = n;
    }
    if (prev != self->sectorCache.tail) linked = false;
    ei::verify::Check("Terrain::SectorAt cache", true, nodes == self->sectorCache.count && linked && gridded && ordered,
                      "nodes %d count %d linked %d grid %d ordered %d", nodes, self->sectorCache.count, linked, gridded,
                      ordered);
}

TerrainSector* __fastcall SectorAtVerify(Terrain* self, void*, int sx, int sy, std::uint32_t wanted, int create) {
    static unsigned calls;
    TerrainSector* result = self->SectorAt(sx, sy, wanted, create);
    if (result) {
        auto* sector = reinterpret_cast<terrain::SectorData*>(result);
        Terrain::SectorCache::Node* node = self->sectorGrid[sx][sy];
        ei::verify::Check("Terrain::SectorAt", true,
                          sector->sx == sx && sector->sy == sy && node && node->data == sector && (sector->built & wanted),
                          "(%d, %d) wanted %u create %d", sx, sy, wanted, create);
    }
    if ((++calls & 63) == 0) CheckSectorCache(self);
    return result;
}
EI_VERIFY(0x006C8140, SectorAtVerify, 0, s_sectorAt);

// ---- Aligned array SetSize / SetAtGrow -------------------------------------------------------------
// The original on a copy with its own block, ours on the real array; header and elements compared.

using SetSizeFn = void(__fastcall*)(Words*, void*, int, int);
using SetAtGrowFn = void(__fastcall*)(Words*, void*, int, std::uint32_t);

void CompareSetSize(const char* name, SetSizeFn original, Words* self, int count, int growBy) {
    Words scratch = CopyArray(*self);
    original(&scratch, nullptr, count, growBy);
    self->SetSize(count, growBy);
    ei::verify::Check(name, true, SameArray(scratch, *self), "count %d growBy %d: size %d/%d cap %d/%d", count, growBy,
                      scratch.size, self->size, scratch.capacity, self->capacity);
    FreeCopy(scratch);
}

void CompareSetAtGrow(const char* name, SetAtGrowFn original, Words* self, int index, std::uint32_t value) {
    Words scratch = CopyArray(*self);
    original(&scratch, nullptr, index, value);
    self->SetAtGrow(index, value);
    ei::verify::Check(name, true, SameArray(scratch, *self), "index %d: size %d/%d cap %d/%d", index, scratch.size,
                      self->size, scratch.capacity, self->capacity);
    FreeCopy(scratch);
}

// 006CD5C0 / 006CE890: MOV EAX,[ESP+8]; PUSH EBX; PUSH ESI (6 bytes). 006CD5C0 runs when a zone
// loads (006C7750), 006CE890 with the light countdown (006C8ED0, when lights change).
SetSizeFn s_setSizeD5C0;
void __fastcall SetSizeD5C0Verify(Words* self, void*, int count, int growBy) {
    CompareSetSize("AlignedArray<uint32>::SetSize (006CD5C0)", s_setSizeD5C0, self, count, growBy);
}
EI_VERIFY(0x006CD5C0, SetSizeD5C0Verify, 6, s_setSizeD5C0);

SetSizeFn s_setSizeE890;
void __fastcall SetSizeE890Verify(Words* self, void*, int count, int growBy) {
    CompareSetSize("AlignedArray<uint32>::SetSize (006CE890)", s_setSizeE890, self, count, growBy);
}
EI_VERIFY(0x006CE890, SetSizeE890Verify, 6, s_setSizeE890);

// 006CDD50 / 006CDE90: PUSH ECX; PUSH EBX; MOV EBX,ECX; PUSH EBP (5 bytes). The map
// loader's tables (006C5AA0, every zone load).
SetAtGrowFn s_setAtGrowDD50;
void __fastcall SetAtGrowDD50Verify(Words* self, void*, int index, std::uint32_t value) {
    CompareSetAtGrow("AlignedArray<uint32>::SetAtGrow (006CDD50)", s_setAtGrowDD50, self, index, value);
}
EI_VERIFY(0x006CDD50, SetAtGrowDD50Verify, 5, s_setAtGrowDD50);

SetAtGrowFn s_setAtGrowDE90;
void __fastcall SetAtGrowDE90Verify(Words* self, void*, int index, std::uint32_t value) {
    CompareSetAtGrow("AlignedArray<uint32>::SetAtGrow (006CDE90)", s_setAtGrowDE90, self, index, value);
}
EI_VERIFY(0x006CDE90, SetAtGrowDE90Verify, 5, s_setAtGrowDE90);

} // namespace
