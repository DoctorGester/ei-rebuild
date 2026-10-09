// Verify wrappers for the landscape object's members and its aligned arrays (terrain_landscape.cpp).
#include "verify.h"

#include "combat_verify_util.h"

#include "../../src/game/material.h"
#include "../../src/game/terrain.h"
#include "../../src/game/terrain_sector.h"

#include <cmath>
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
    // The originals leave grown elements uninitialised (no fill after the copy), so only the elements
    // kept from before are compared.
    int kept = self->size < count ? self->size : count;
    Words scratch = CopyArray(*self);
    original(&scratch, nullptr, count, growBy);
    self->SetSize(count, growBy);
    bool same = scratch.size == self->size && scratch.capacity == self->capacity && scratch.growBy == self->growBy &&
                !scratch.data == !self->data &&
                (kept <= 0 || std::memcmp(scratch.data, self->data, static_cast<std::size_t>(kept) * sizeof(std::uint32_t)) == 0);
    ei::verify::Check(name, true, same, "count %d growBy %d: size %d/%d cap %d/%d", count, growBy,
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

// ---- Landscape Create / Destroy / Reset and the waves ----------------------------------------------
// Reached at every zone load: the client world's Setup constructs the landscape (Reset through its
// address) and calls Create; leaving a zone runs Destroy; Create and Destroy call Reset and the
// wave phases through their addresses. The zone load (00567AB0) sets the wave strength.

// A raw copy of the landscape in a static buffer, one per wrapper (Reset's original runs SetWaveStrength's
// wrapper on its copy; the originals below only write fields).
template <int Buffer>
Terrain* RawCopy(const Terrain* self) {
    alignas(Terrain) static unsigned char bytes[sizeof(Terrain)];
    std::memcpy(bytes, static_cast<const void*>(self), sizeof(Terrain));
    return reinterpret_cast<Terrain*>(bytes);
}

mfc::CString& AsString(const char*& text) { return reinterpret_cast<mfc::CString&>(text); }

// 006C5620: SUB ESP,0xC; PUSH EBX; PUSH ESI (5 bytes). The original on a raw copy whose arrays are
// empty and whose two texts are the empty string (so it frees and overwrites nothing of ours); ours
// on the landscape. The whole object compared except the two text pointers (their texts compared).
void(__fastcall* s_reset)(Terrain*, void*);

void __fastcall ResetVerify(Terrain* self, void*) {
    Terrain* scratch = RawCopy<0>(self);
    const char* empty = *reinterpret_cast<const char* const*>(0x00796E48);   // afxEmptyString
    scratch->pageTextures.data = nullptr;
    scratch->pageMaterials.data = nullptr;
    scratch->visibleColumnFirst.data = nullptr;
    scratch->visibleColumnLast.data = nullptr;
    scratch->textF8 = empty;
    scratch->textAE0 = empty;
    s_reset(scratch, nullptr);
    self->Reset();
    bool texts = std::strcmp(scratch->textF8, self->textF8) == 0 && std::strcmp(scratch->textAE0, self->textAE0) == 0;
    AsString(scratch->textF8).~CString();
    AsString(scratch->textAE0).~CString();
    scratch->textF8 = self->textF8;
    scratch->textAE0 = self->textAE0;
    int differs = -1;
    const auto* a = reinterpret_cast<const unsigned char*>(scratch);
    const auto* b = reinterpret_cast<const unsigned char*>(self);
    for (int i = 0; i < static_cast<int>(sizeof(Terrain)) && differs < 0; ++i)
        if (a[i] != b[i]) differs = i;
    ei::verify::Check("Terrain::Reset", -1, differs, "first differing byte (texts %d)", texts);
    ei::verify::Check("Terrain::Reset texts", true, texts, "\"%s\"", self->textF8);
}
EI_VERIFY(0x006C5620, ResetVerify, 5, s_reset);

// 006C57E0: SUB ESP,0xC; PUSH EBX; MOV EBX,ECX (6 bytes). The original on a raw copy with its own
// phase block, rand()'s state saved before and restored for ours; the phases, the array header and
// the state after both runs compared.
void(__fastcall* s_wavePhases)(Terrain*, void*);

// The first index where two float arrays differ by more than a few ULPs, or -1.
int FirstFar(const float* expected, const float* actual, int count) {
    for (int i = 0; i < count; ++i) {
        float scale = std::fabs(expected[i]) > 1.0f ? std::fabs(expected[i]) : 1.0f;
        if (!(std::fabs(expected[i] - actual[i]) <= scale * 1e-6f)) return i;
    }
    return -1;
}

void __fastcall WavePhasesVerify(Terrain* self, void*) {
    Terrain* scratch = RawCopy<1>(self);
    scratch->wavePhases = CopyArray(self->wavePhases);
    combatverify::RandomState before, afterOriginal, afterOurs;
    before.Save();
    s_wavePhases(scratch, nullptr);
    afterOriginal.Save();
    before.Restore();
    self->BuildWavePhases();
    afterOurs.Save();
    ei::verify::Check("Terrain::BuildWavePhases rand", true, afterOriginal.SameAs(afterOurs), "state after");
    bool header = scratch->wavePhases.size == self->wavePhases.size &&
                  scratch->wavePhases.capacity == self->wavePhases.capacity;
    ei::verify::Check("Terrain::BuildWavePhases", true, header, "size %d/%d", scratch->wavePhases.size,
                      self->wavePhases.size);
    int bad = header ? FirstFar(scratch->wavePhases.data, self->wavePhases.data, self->wavePhases.size) : -1;
    if (bad >= 0)
        ei::verify::CheckNear("Terrain::BuildWavePhases", scratch->wavePhases[bad], self->wavePhases[bad], "phase %d", bad);
    FreeCopy(scratch->wavePhases);
}
EI_VERIFY(0x006C57E0, WavePhasesVerify, 6, s_wavePhases);

// The wave fields after SetWaveStrength / SetWaves.
void CompareWaves(const char* name, const Terrain* expected, const Terrain* actual, float strength) {
    const float e[] = {expected->waveDirection.x, expected->waveDirection.y, expected->waveDirection.z,
                       expected->waveHeight, expected->waveGlow, expected->wavePhase, expected->waveSpeed};
    const float a[] = {actual->waveDirection.x, actual->waveDirection.y, actual->waveDirection.z,
                       actual->waveHeight, actual->waveGlow, actual->wavePhase, actual->waveSpeed};
    int bad = FirstFar(e, a, 7);
    if (bad >= 0) ei::verify::CheckNear(name, e[bad], a[bad], "strength %g field %d", strength, bad);
}

// 006C7D70 / 006C7EB0: PUSH EBP; MOV EBP,ESP; SUB ESP,8 (6 bytes). They only write the wave
// fields: the original on a raw copy, ours on the landscape.
void(__fastcall* s_waveStrength)(Terrain*, void*, float);

void __fastcall WaveStrengthVerify(Terrain* self, void*, float strength) {
    Terrain* scratch = RawCopy<2>(self);
    s_waveStrength(scratch, nullptr, strength);
    self->SetWaveStrength(strength);
    CompareWaves("Terrain::SetWaveStrength", scratch, self, strength);
}
EI_VERIFY(0x006C7D70, WaveStrengthVerify, 6, s_waveStrength);

void(__fastcall* s_waves)(Terrain*, void*, const Vector3*, float);

void __fastcall WavesVerify(Terrain* self, void*, const Vector3* direction, float strength) {
    Vector3 copy = *direction;   // the original's copy writes our direction when it is the landscape's
    Terrain* scratch = RawCopy<3>(self);
    s_waves(scratch, nullptr, &copy, strength);
    self->SetWaves(direction, strength);
    CompareWaves("Terrain::SetWaves", scratch, self, strength);
}
EI_VERIFY(0x006C7EB0, WavesVerify, 6, s_waves);

// 006C5430 / 006C54D0: ours only (they allocate and free; Reset and the wave phases are compared
// above). Create: the result, the sine table against sin(i * pi / 256), the frame and rotation.
// Destroy: the light table, the cache list and the map size emptied.
int(__fastcall* s_create)(Terrain*, void*);

int __fastcall CreateVerify(Terrain* self, void*) {
    int result = self->Terrain::Create();
    ei::verify::Check("Terrain::Create", 1, result, "result");
    float expected[512];
    for (int i = 0; i < 512; ++i) expected[i] = static_cast<float>(std::sin(i * 3.141592653589793 / 256));
    int bad = FirstFar(expected, self->sine, 512);
    if (bad >= 0) ei::verify::CheckNear("Terrain::Create sine", expected[bad], self->sine[bad], "entry %d", bad);
    ei::verify::Check("Terrain::Create", true,
                      self->frame == 1 && self->localRotation_.w == 1.0f && self->wavePhases.size == 33 * 33,
                      "frame %d", self->frame);
    return result;
}
EI_VERIFY(0x006C5430, CreateVerify, 0, s_create);

void(__fastcall* s_destroy)(Terrain*, void*);

void __fastcall DestroyVerify(Terrain* self, void*) {
    self->Terrain::Destroy();
    ei::verify::Check("Terrain::Destroy", true,
                      self->lights.Size() == 0 && self->lights.First() == nullptr && self->maxLightId == 0 &&
                          self->sectorCache.count == 0 && self->sectorCache.head == nullptr && self->sectorsX == -1,
                      "lights %u cache %d", static_cast<unsigned>(self->lights.Size()), self->sectorCache.count);
}
EI_VERIFY(0x006C54D0, DestroyVerify, 0, s_destroy);

// 006C5AA0: ours only (it makes, holds and loads textures and materials; zone load through the map
// loader 006C68B0). The result; with 1, both arrays as long as the pages asked for plus the extra
// ones found, every entry set, every material named "Wood".
int(__fastcall* s_setupPages)(Terrain*, void*, int);

int __fastcall SetupPagesVerify(Terrain* self, void*, int count) {
    int extra = self->extraPages;
    int result = self->SetupPages(count);
    if (result == 1) {
        int pages = count + self->extraPages - extra;
        bool filled = self->pageTextures.size == pages && self->pageMaterials.size == pages;
        for (int i = 0; filled && i < pages; ++i)
            filled = self->pageTextures[i] && self->pageMaterials[i] &&
                     std::strcmp(self->pageMaterials[i]->name, "Wood") == 0;
        ei::verify::Check("Terrain::SetupPages", true, filled, "%d pages, %d extra", count, self->extraPages - extra);
    }
    return result;
}
EI_VERIFY(0x006C5AA0, SetupPagesVerify, 0, s_setupPages);

// 006C7A80: SUB ESP,0x1C; PUSH EBX; MOV EBX,ECX (6 bytes). Create (startup and every new game). The
// original into a raw copy of the landscape with a private copy of the table; the headers and each
// entry's count and first `count` tiles compared (the original leaves the rest of an entry as its
// stack record held).
void(__fastcall* s_vertexTiles)(Terrain*);

void __fastcall VertexTilesVerify(Terrain* self) {
    alignas(Terrain) unsigned char storage[sizeof(Terrain)];
    std::memcpy(storage, self, sizeof(Terrain));
    auto* copy = reinterpret_cast<Terrain*>(storage);
    copy->vertexTiles = CopyArray(self->vertexTiles);
    s_vertexTiles(copy);
    self->BuildVertexTiles();
    const auto& e = copy->vertexTiles;
    const auto& a = self->vertexTiles;
    int bad = -1;
    if (e.size != a.size || e.capacity != a.capacity || e.growBy != a.growBy) bad = -2;
    for (int i = 0; bad == -1 && i < a.size; ++i) {
        bool same = e[i].count == a[i].count;
        for (int k = 0; same && k < a[i].count; ++k) same = e[i].tiles[k] == a[i].tiles[k];
        if (!same) bad = i;
    }
    ei::verify::Check("Terrain::BuildVertexTiles", -1, bad, "size %d / %d capacity %d / %d", e.size, a.size, e.capacity,
                      a.capacity);
    FreeCopy(copy->vertexTiles);
}
EI_VERIFY(0x006C7A80, VertexTilesVerify, 6, s_vertexTiles);

// 006C7750: ours only (it stops sounds, deletes sectors and releases textures; every zone load
// through the map loader, and Destroy). The map's state emptied afterwards.
void(__fastcall* s_clearMap)(Terrain*);

void __fastcall ClearMapVerify(Terrain* self) {
    self->ClearMap();
    bool empty = self->sectorCache.count == 0 && self->sectorCache.head == nullptr && self->sectorGrid == nullptr &&
                 self->pageTextures.size == 0 && self->pageMaterials.size == 0 && self->sectorsX == -1 &&
                 self->sectorsY == -1 && self->unknown_100 == nullptr && self->ambientClasses[0] == -1;
    for (int i = 0; empty && i < 4; ++i) empty = !self->uvTables[i] && !self->uvTablesLast[i];
    ei::verify::Check("Terrain::ClearMap", true, empty, "cache %d pages %d / %d", self->sectorCache.count,
                      self->pageTextures.size, self->pageMaterials.size);
}
EI_VERIFY(0x006C7750, ClearMapVerify, 0, s_clearMap);

} // namespace
