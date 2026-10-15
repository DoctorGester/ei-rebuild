// Verify wrappers for the landscape object's members and its aligned arrays (terrain_landscape.cpp).
#include "verify.h"

#include "combat_verify_util.h"

#include "../../src/game/material.h"
#include "../../src/game/terrain.h"
#include "../../src/game/terrain_sector.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

using Words = lib::AlignedArray<std::uint32_t>;

// An aligned array's header and elements kept in our memory before ours runs; Copy makes a private
// copy (the header, and its own block: the original may grow or free it, so it must not share
// ours) after ours ran, so that ours' blocks land where the original's would without the check
// (the game reads the heap's address order, SV2f).
template <typename T>
struct KeptArray {
    lib::AlignedArray<T> header;
    std::vector<unsigned char> elements;
    explicit KeptArray(const lib::AlignedArray<T>& source) : header(source) {
        if (source.data && source.size > 0) {
            const auto* p = reinterpret_cast<const unsigned char*>(source.data);
            elements.assign(p, p + static_cast<std::size_t>(source.size) * sizeof(T));
        }
    }
    lib::AlignedArray<T> Copy() const {
        lib::AlignedArray<T> copy = header;
        if (header.data) {
            copy.data = lib::AlignedArray<T>::Allocate(header.capacity);
            if (!elements.empty()) std::memcpy(static_cast<void*>(copy.data), elements.data(), elements.size());
        }
        return copy;
    }
};

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
// scenarios). Ours runs on the real one, then the original on a raw copy of the object (taken
// before) whose two arrays have their own blocks (it reads only +0x104 / +0x108 and writes
// +0xAA4..+0xAC4); the first row and
// both arrays (header and elements) compared.
void(__fastcall* s_setVisible)(Terrain*, void*, float, float, float);

void __fastcall SetVisibleSectorsVerify(Terrain* self, void*, float x, float y, float radius) {
    alignas(Terrain) static unsigned char scratchBytes[sizeof(Terrain)];
    std::memcpy(scratchBytes, static_cast<void*>(self), sizeof(Terrain));
    auto* scratch = reinterpret_cast<Terrain*>(scratchBytes);
    KeptArray<int> first(self->visibleColumnFirst), last(self->visibleColumnLast);
    self->SetVisibleSectors(x, y, radius);   // ours first (SV2f)
    scratch->visibleColumnFirst = first.Copy();
    scratch->visibleColumnLast = last.Copy();
    s_setVisible(scratch, nullptr, x, y, radius);
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
terrain::SectorData*(__fastcall* s_sectorAt)(Terrain*, void*, int, int, std::uint32_t, int);

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

terrain::SectorData* __fastcall SectorAtVerify(Terrain* self, void*, int sx, int sy, std::uint32_t wanted, int create) {
    static unsigned calls;
    terrain::SectorData* result = self->SectorAt(sx, sy, wanted, create);
    if (result) {
        terrain::SectorData* sector = result;
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
    KeptArray<std::uint32_t> before(*self);
    self->SetSize(count, growBy);   // ours first (SV2f)
    Words scratch = before.Copy();
    original(&scratch, nullptr, count, growBy);
    bool same = scratch.size == self->size && scratch.capacity == self->capacity && scratch.growBy == self->growBy &&
                !scratch.data == !self->data &&
                (kept <= 0 || std::memcmp(scratch.data, self->data, static_cast<std::size_t>(kept) * sizeof(std::uint32_t)) == 0);
    ei::verify::Check(name, true, same, "count %d growBy %d: size %d/%d cap %d/%d", count, growBy,
                      scratch.size, self->size, scratch.capacity, self->capacity);
    FreeCopy(scratch);
}

void CompareSetAtGrow(const char* name, SetAtGrowFn original, Words* self, int index, std::uint32_t value) {
    KeptArray<std::uint32_t> before(*self);
    self->SetAtGrow(index, value);   // ours first (SV2f)
    Words scratch = before.Copy();
    original(&scratch, nullptr, index, value);
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
    self->Reset();   // ours first (SV2f)
    s_reset(scratch, nullptr);
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
    // Ours first; the copy's phase block is made after it and rand()'s state put back for the
    // original, then left as ours left it (SV2f).
    Terrain* scratch = RawCopy<1>(self);
    KeptArray<float> phases(self->wavePhases);
    combatverify::RandomState before, afterOriginal, afterOurs;
    before.Save();
    self->BuildWavePhases();
    afterOurs.Save();
    before.Restore();
    scratch->wavePhases = phases.Copy();
    s_wavePhases(scratch, nullptr);
    afterOriginal.Save();
    afterOurs.Restore();
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
    int result = self->Terrain::CreateImpl();
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
    self->Terrain::DestroyImpl();
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
    KeptArray<terrain::VertexTiles> tiles(self->vertexTiles);
    self->BuildVertexTiles();   // ours first (SV2f)
    copy->vertexTiles = tiles.Copy();
    s_vertexTiles(copy);
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
                 self->sectorsY == -1 && self->mapLoaded == 0 && self->ambientClasses[0] == -1;
    for (int i = 0; empty && i < 4; ++i) empty = !self->uvTables[i] && !self->uvTablesLast[i];
    ei::verify::Check("Terrain::ClearMap", true, empty, "cache %d pages %d / %d", self->sectorCache.count,
                      self->pageTextures.size, self->pageMaterials.size);
}
EI_VERIFY(0x006C7750, ClearMapVerify, 0, s_clearMap);

// ---- The world matrix, the light interval, water levels, tile texture coordinates (A-X6) -------------

// 006C6070 (slot 14): PUSH EBP; MOV EBP,ESP; SUB ESP,0x28 (6 bytes). From the base's UpdateWorld
// when the landscape is placed. It writes the world rotation and the world matrix: the original,
// both put back (the parent may be the landscape's own rotation), ours; the 20 floats compared.
void(__fastcall* s_combineRotation)(Terrain*, void*, const Quaternion*);

void __fastcall CombineRotationVerify(Terrain* self, void*, const Quaternion* parent) {
    struct Result {
        Quaternion rotation;
        Matrix4 matrix;
    };
    const Result before = {self->worldRotation_, self->worldMatrix};
    s_combineRotation(self, nullptr, parent);
    const Result expected = {self->worldRotation_, self->worldMatrix};
    self->worldRotation_ = before.rotation;
    self->worldMatrix = before.matrix;
    self->CombineRotationImpl(*parent);
    const Result actual = {self->worldRotation_, self->worldMatrix};
    const int bad = FirstFar(&expected.rotation.w, &actual.rotation.w, sizeof(Result) / sizeof(float));
    ei::verify::Check("Terrain::CombineRotation", -1, bad, "first differing float");
}
EI_VERIFY(0x006C6070, CombineRotationVerify, 6, s_combineRotation);

// 006C9290: MOV EAX,[ESP+4]; TEST EAX,EAX (6 bytes). From the zone load 00567AB0. The original,
// the two fields put back, ours; compared.
void(__fastcall* s_setLightInterval)(Terrain*, void*, int);

void __fastcall SetLightIntervalVerify(Terrain* self, void*, int frames) {
    const int interval = self->lightInterval;
    const int countdown = self->lightCountdown;
    s_setLightInterval(self, nullptr, frames);
    const int expectedInterval = self->lightInterval;
    const int expectedCountdown = self->lightCountdown;
    self->lightInterval = interval;
    self->lightCountdown = countdown;
    self->SetLightInterval(frames);
    ei::verify::Check("Terrain::SetLightInterval", true,
                      expectedInterval == self->lightInterval && expectedCountdown == self->lightCountdown,
                      "frames %d: interval %d/%d countdown %d/%d", frames, expectedInterval, self->lightInterval,
                      expectedCountdown, self->lightCountdown);
}
EI_VERIFY(0x006C9290, SetLightIntervalVerify, 6, s_setLightInterval);

// 006CC7F0: PUSH EDI; MOV EDI,[ESP+8] (5 bytes). From TweenArray::Load (a save loaded while a
// script's SetWaterLevel change is running: scenario terrain_water_load). It writes the material's
// level and its entry of every sector's level table: the original, those floats put back, ours;
// compared.
void(__fastcall* s_setWaterLevel)(Terrain*, void*, int, float);

void __fastcall SetWaterLevelVerify(Terrain* self, void*, int material, float level) {
    constexpr int kMost = 4096;   // sectors compared
    static float before[kMost + 1], expected[kMost + 1], actual[kMost + 1];
    const bool known = material >= 0 && material < self->materials.size;
    int sectors = known ? self->sectorsX * self->sectorsY : 0;
    if (sectors > kMost) sectors = kMost;
    if (sectors < 0) sectors = 0;
    auto read = [&](float* out) {
        if (!known) return;
        out[0] = self->materials[material].level;
        for (int i = 0; i < sectors; ++i) out[i + 1] = self->waterLevels[i][material];
    };
    read(before);
    s_setWaterLevel(self, nullptr, material, level);
    read(expected);
    if (known) {
        self->materials[material].level = before[0];
        for (int i = 0; i < sectors; ++i) self->waterLevels[i][material] = before[i + 1];
    }
    self->SetWaterLevel(material, level);
    read(actual);
    const int bad = known ? FirstFar(expected, actual, sectors + 1) : -1;
    ei::verify::Check("Terrain::SetWaterLevel", -1, bad, "material %d of %d level %g: first differing entry", material,
                      self->materials.size, level);
}
EI_VERIFY(0x006CC7F0, SetWaterLevelVerify, 5, s_setWaterLevel);

// 006C7940: PUSH EBX; MOV BL,[ESP+0x20] (5 bytes). From the map loader 006C68B0 (every zone load),
// per tile texture and rotation. The original into a scratch table, ours into the caller's; the 18
// floats compared.
void(__fastcall* s_tileUv)(Terrain*, void*, float*, int, int, int, float, float, char);

void __fastcall TileUvVerify(Terrain* self, void*, float* uv, int rotation, int column, int row, float size,
                             float margin, char wholePage) {
    float expected[18];
    s_tileUv(self, nullptr, expected, rotation, column, row, size, margin, wholePage);
    self->BuildTileUv(uv, rotation, column, row, size, margin, wholePage);
    ei::verify::Check("Terrain::BuildTileUv", -1, FirstFar(expected, uv, 18),
                      "rotation %d tile %d,%d size %g margin %g whole %d: first differing float", rotation, column, row,
                      size, margin, wholePage);
}
EI_VERIFY(0x006C7940, TileUvVerify, 5, s_tileUv);

} // namespace
