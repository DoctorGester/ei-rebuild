// Verify wrappers for the terrain sector code (terrain_sector.cpp).
#include "verify.h"

#include "../../src/game/random.h"
#include "../../src/game/terrain_sector.h"
#include "../../src/lib/crt.h"

#include <cstdio>
#include <cstring>
#include <vector>

using namespace terrain;

namespace {

constexpr int kVertices = 33 * 33;

IsaacRandom& Rng() { return *reinterpret_cast<IsaacRandom*>(0x007A9D40); }

void FreeAligned(void* p) {
    if (p) gamecrt::operator_delete(static_cast<void**>(p)[-1]);
}

__declspec(naked) void __stdcall OriginalDestroyRecords(SectorVertex*, int) EI_ORIGINAL(0x006C4EE0)

template <typename T>
T* CopyBlock(const T* src, int count, int capacity) {
    if (!src) return nullptr;
    T* p = static_cast<T*>(gamecrt::operator_new(capacity * sizeof(T) + 1));
    std::memcpy(p, src, count * sizeof(T));
    return p;
}

void Report(const char* name, bool ok, const char* what, const SectorData* s) {
    char detail[160];
    std::snprintf(detail, sizeof(detail), "%s (sector %d,%d)", what, s->sx, s->sy);
    ei::verify::Report(name, ok, ok ? "" : detail);
}

MfcObArray<float>* WaterRow(const SectorData* s) {
    if (!s->hasWater) return nullptr;
    return &s->owner->waterLevels[s->owner->sectorsX * s->sy + s->sx];
}

std::vector<float> RowCopy(const MfcObArray<float>* row) {
    if (!row || !row->data) return {};
    return std::vector<float>(row->data, row->data + row->size);
}

void RowRestore(MfcObArray<float>* row, const std::vector<float>& v) {
    if (row && row->data && !v.empty()) std::memcpy(row->data, v.data(), v.size() * 4);
}

// Locates a tile entry pointer as quarter / page / index, for comparing pointers into two sectors.
long long EntryKey(const SectorData* s, const TileVertex* p) {
    for (int q = 0; q < 4; ++q)
        for (int k = 0; k < s->tiles[q].size; ++k) {
            const InlineArray<TileVertex>& t = s->tiles[q].data[k];
            if (t.data && p >= t.data && p < t.data + t.size) return (q * 1000LL + k) * 100000LL + (p - t.data);
        }
    return -1;
}

const char* CompareBuild(const SectorData* a, const SectorData* b, bool lists) {
    // a = ours (real), b = the original's (scratch)
    if (std::memcmp(reinterpret_cast<const char*>(a) + 4, reinterpret_cast<const char*>(b) + 4, 0x15C - 4)) return "scalars / boxes";
    if (a->unknown_170 != b->unknown_170 || a->unknown_174 != b->unknown_174 || a->unknown_178 != b->unknown_178) return "+0x170";
    if (a->waterMaterials.size != b->waterMaterials.size ||
        (a->waterMaterials.size && std::memcmp(a->waterMaterials.data, b->waterMaterials.data, a->waterMaterials.size)))
        return "water materials";
    if (a->vertices.size != b->vertices.size || std::memcmp(a->vertices.data, b->vertices.data, a->vertices.size * 12)) return "vertices";
    if (a->records.size != b->records.size) return "record count";
    for (int i = 0; i < a->records.size; ++i) {
        const SectorVertex& ra = a->records.data[i];
        const SectorVertex& rb = b->records.data[i];
        const char* pa = reinterpret_cast<const char*>(&ra);
        const char* pb = reinterpret_cast<const char*>(&rb);
        if (std::memcmp(pa + 0x19, pb + 0x19, 0x37 - 0x19) || std::memcmp(pa + 0x43, pb + 0x43, 0x50 - 0x43)) return "record";
        if (lists) {
            const LightRef* la = ra.lights;
            const LightRef* lb = rb.lights;
            for (; la && lb; la = la->next, lb = lb->next)
                if (la->light != lb->light) return "record lights";
            if (la || lb) return "record light count";
        }
    }
    if (a->tileTextures.size != b->tileTextures.size || std::memcmp(a->tileTextures.data, b->tileTextures.data, a->tileTextures.size * 4))
        return "tile textures";
    if (std::memcmp(a->tileRotation, b->tileRotation, sizeof(a->tileRotation))) return "tile rotation";
    if (a->classCounts.size != b->classCounts.size || std::memcmp(a->classCounts.data, b->classCounts.data, a->classCounts.size * 4))
        return "class counts";
    for (int q = 0; q < 4; ++q) {
        if (a->tileFlags[q].size != b->tileFlags[q].size || a->tiles[q].size != b->tiles[q].size) return "page count";
        for (int k = 0; k < a->tileFlags[q].size; ++k) {
            const MfcObArray<std::uint8_t>& fa = a->tileFlags[q].data[k];
            const MfcObArray<std::uint8_t>& fb = b->tileFlags[q].data[k];
            if (fa.size != fb.size || (fa.size && std::memcmp(fa.data, fb.data, fa.size))) return "tile flags";
        }
        for (int k = 0; k < a->tiles[q].size; ++k) {
            const InlineArray<TileVertex>& ta = a->tiles[q].data[k];
            const InlineArray<TileVertex>& tb = b->tiles[q].data[k];
            if (ta.size != tb.size) return "tile list size";
            for (int i = 0; i < ta.size; ++i) {
                if (ta.data[i].vertex - a->records.data != tb.data[i].vertex - b->records.data) return "tile vertex";
                if (std::memcmp(&ta.data[i].u, &tb.data[i].u, 8)) return "tile uv";
            }
        }
    }
    if (a->animated.size != b->animated.size) return "animated count";
    for (int i = 0; i < a->animated.size; ++i) {
        const AnimatedTile& x = a->animated.data[i];
        const AnimatedTile& y = b->animated.data[i];
        if (EntryKey(a, x.vertices) != EntryKey(b, y.vertices)) return "animated tile";
        if (std::memcmp(&x.rotation, &y.rotation, 0x14)) return "animated fields";
    }
    if (std::memcmp(&a->lastUsed, &b->lastUsed, 0x394 - 0x384)) return "+0x384";
    return nullptr;
}

} // namespace

// 006C1C00 SectorData::Build. The original runs first on a scratch copy of the sector whose
// rebuilt arrays start empty and whose vertices / water material list are private copies; the
// random generator and the owner's water level row are put back before ours runs on the real
// sector. Everything the build writes is compared (pointers as indices), then the scratch is
// destroyed (its light references go back to the pool through the original's record destructor).
static void(__fastcall* s_build)(SectorData*, void*);
static void __fastcall BuildVerify(SectorData* s, void*) {
    bool freshRecords = s->records.data == nullptr;
    auto* scratch = static_cast<SectorData*>(gamecrt::operator_new(sizeof(SectorData)));
    std::memcpy(scratch, s, sizeof(SectorData));
    scratch->vertices.data = CopyBlock(s->vertices.data, s->vertices.size, s->vertices.capacity);
    scratch->waterMaterials.data = CopyBlock(s->waterMaterials.data, s->waterMaterials.size, s->waterMaterials.capacity);
    scratch->records.data = nullptr;
    scratch->records.size = scratch->records.capacity = 0;
    for (InlineArray<int>* a : {&scratch->tileTextures, &scratch->classCounts}) {
        a->data = nullptr;
        a->size = a->capacity = 0;
    }
    scratch->animated.data = nullptr;
    scratch->animated.size = scratch->animated.capacity = 0;
    for (int q = 0; q < 4; ++q) {
        scratch->tileFlags[q].data = nullptr;
        scratch->tileFlags[q].size = scratch->tileFlags[q].capacity = 0;
        scratch->tiles[q].data = nullptr;
        scratch->tiles[q].size = scratch->tiles[q].capacity = 0;
    }

    MfcObArray<float>* row = WaterRow(s);
    std::vector<float> rowBefore = RowCopy(row);
    std::vector<std::uint8_t> rngBefore(sizeof(IsaacRandom));
    std::memcpy(rngBefore.data(), &Rng(), sizeof(IsaacRandom));

    s_build(scratch, nullptr);

    std::vector<float> rowOriginal = RowCopy(row);
    std::vector<std::uint8_t> rngOriginal(sizeof(IsaacRandom));
    std::memcpy(rngOriginal.data(), &Rng(), sizeof(IsaacRandom));
    std::memcpy(&Rng(), rngBefore.data(), sizeof(IsaacRandom));
    RowRestore(row, rowBefore);

    s->Build();

    const char* bad = CompareBuild(s, scratch, freshRecords);
    if (!bad && std::memcmp(rngOriginal.data(), &Rng(), sizeof(IsaacRandom))) bad = "random generator";
    if (!bad && RowCopy(row) != rowOriginal) bad = "water level row";
    Report("SectorData::Build", bad == nullptr, bad ? bad : "", s);

    // Destroy the scratch.
    if (scratch->records.data) {
        OriginalDestroyRecords(scratch->records.data, scratch->records.size);
        gamecrt::operator_delete(scratch->records.data);
    }
    FreeAligned(scratch->tileTextures.data);
    FreeAligned(scratch->classCounts.data);
    FreeAligned(scratch->animated.data);
    using Destructor = void(__thiscall*)(void*, int);
    for (int q = 0; q < 4; ++q) {
        for (int k = 0; k < scratch->tileFlags[q].size; ++k) {
            void* e = &scratch->tileFlags[q].data[k];
            (*static_cast<Destructor* const*>(e))[1](e, 0);
        }
        if (scratch->tileFlags[q].data) gamecrt::operator_delete(scratch->tileFlags[q].data);
        for (int k = 0; k < scratch->tiles[q].size; ++k) FreeAligned(scratch->tiles[q].data[k].data);
        if (scratch->tiles[q].data) gamecrt::operator_delete(scratch->tiles[q].data);
    }
    if (scratch->vertices.data) gamecrt::operator_delete(scratch->vertices.data);
    if (scratch->waterMaterials.data) gamecrt::operator_delete(scratch->waterMaterials.data);
    gamecrt::operator_delete(scratch);
}
// 006c1c00 PUSH EBP; MOV EBP,ESP; MOV EAX,0x1068 (8 bytes)
EI_VERIFY(0x006C1C00, BuildVerify, 8, s_build);

// 006C13A0 SectorData::Load. Ours on the real sector, then the original on a scratch copy whose
// arrays are emptied (its first step, Clear, would free them); the archive reference the second
// GetFile takes is dropped again with CloseFile.
static int(__fastcall* s_load)(SectorData*, void*, void*, int, int, int, int, const char*, TerrainFields*);
static int __fastcall LoadVerify(SectorData* s, void*, void* archive, int sx, int sy, int pages, int perPage,
                                 const char* prefix, TerrainFields* owner) {
    int r = s->Load(archive, sx, sy, pages, perPage, prefix, owner);
    if (r != 1 || !(s->built & 1)) return r;
    auto* scratch = static_cast<SectorData*>(gamecrt::operator_new(sizeof(SectorData)));
    std::memcpy(scratch, s, sizeof(SectorData));
    scratch->records.data = nullptr;
    scratch->records.size = scratch->records.capacity = 0;
    for (InlineArray<int>* a : {&scratch->tileTextures, &scratch->classCounts, &scratch->waterTiles}) {
        a->data = nullptr;
        a->size = a->capacity = 0;
    }
    scratch->animated.data = nullptr;
    scratch->animated.size = scratch->animated.capacity = 0;
    scratch->vertices.data = nullptr;
    scratch->vertices.size = scratch->vertices.capacity = 0;
    for (int q = 0; q < 4; ++q) {
        scratch->tileFlags[q].data = nullptr;
        scratch->tileFlags[q].size = scratch->tileFlags[q].capacity = 0;
        scratch->tiles[q].data = nullptr;
        scratch->tiles[q].size = scratch->tiles[q].capacity = 0;
    }
    int expected = s_load(scratch, nullptr, archive, sx, sy, pages, perPage, prefix, owner);
    using CloseFileFn = bool(__thiscall*)(void*, void*);
    if (scratch->data) (*static_cast<CloseFileFn* const*>(archive))[6](archive, const_cast<std::uint8_t*>(scratch->data - 5));

    const char* bad = nullptr;
    if (expected != r) bad = "result";
    else if (std::memcmp(reinterpret_cast<char*>(s) + 4, reinterpret_cast<char*>(scratch) + 4, 0x24)) bad = "header fields";
    else if (s->vertices.size != scratch->vertices.size ||
             std::memcmp(s->vertices.data, scratch->vertices.data, s->vertices.size * 12)) bad = "vertices";
    else if (s->waterTiles.size != scratch->waterTiles.size ||
             (s->waterTiles.size && std::memcmp(s->waterTiles.data, scratch->waterTiles.data, s->waterTiles.size * 4))) bad = "water tiles";
    Report("SectorData::Load", bad == nullptr, bad ? bad : "", s);

    if (scratch->vertices.data) gamecrt::operator_delete(scratch->vertices.data);
    FreeAligned(scratch->waterTiles.data);
    gamecrt::operator_delete(scratch);
    return r;
}
// 006c13a0 MOV EAX,FS:[0x0] (6 bytes)
EI_VERIFY(0x006C13A0, LoadVerify, 6, s_load);

// 006C30E0 SectorData::ApplyWaterLevels: snapshot (water records, vertices, the owner's row),
// original, record, restore, ours, compare.
static void(__fastcall* s_applyWater)(SectorData*, void*);
static void __fastcall ApplyWaterVerify(SectorData* s, void*) {
    MfcObArray<float>* row = &s->owner->waterLevels[s->owner->sectorsX * s->sy + s->sx];
    std::vector<float> rowBefore = RowCopy(row);
    std::vector<std::uint8_t> recBefore(reinterpret_cast<std::uint8_t*>(s->records.data),
                                        reinterpret_cast<std::uint8_t*>(s->records.data + s->records.size));
    std::vector<float> vBefore(s->vertices.data, s->vertices.data + s->vertices.size * 3);
    s_applyWater(s, nullptr);
    std::vector<float> rowOriginal = RowCopy(row);
    std::vector<std::uint8_t> recOriginal(reinterpret_cast<std::uint8_t*>(s->records.data),
                                          reinterpret_cast<std::uint8_t*>(s->records.data + s->records.size));
    std::vector<float> vOriginal(s->vertices.data, s->vertices.data + s->vertices.size * 3);
    RowRestore(row, rowBefore);
    std::memcpy(s->records.data, recBefore.data(), recBefore.size());
    std::memcpy(s->vertices.data, vBefore.data(), vBefore.size() * 4);
    s->ApplyWaterLevels();
    bool ok = RowCopy(row) == rowOriginal &&
              std::memcmp(s->records.data, recOriginal.data(), recOriginal.size()) == 0 &&
              std::memcmp(s->vertices.data, vOriginal.data(), vOriginal.size() * 4) == 0;
    Report("SectorData::ApplyWaterLevels", ok, "levels", s);
}
// 006c30e0 PUSH ECX; PUSH EBX; PUSH EBP; PUSH ESI; MOV ESI,ECX (6 bytes)
EI_VERIFY(0x006C30E0, ApplyWaterVerify, 6, s_applyWater);

// 006C3DD0 SectorData::Ensure: ours only (it builds); the result must be wanted & built, and
// what was asked for must be built.
static std::uint32_t(__fastcall* s_unusedEnsure)(SectorData*, void*, std::uint32_t, TerrainFields*);
static std::uint32_t __fastcall EnsureVerify(SectorData* s, void*, std::uint32_t wanted, TerrainFields* owner) {
    bool already = (wanted & s->built) != 0;
    std::uint32_t r = s->Ensure(wanted, owner);
    bool ok = already ? r == 1
                      : (r == (wanted & s->built) && (wanted < 2 || (s->built & 2)) && (wanted < 4 || (s->built & 4)));
    Report("SectorData::Ensure", ok, "result", s);
    return r;
}
EI_VERIFY(0x006C3DD0, EnsureVerify, 0, s_unusedEnsure);

// 006C53C0 TerrainFields::TileUv: pure.
static const float*(__fastcall* s_tileUv)(TerrainFields*, void*, int, int, int);
static const float* __fastcall TileUvVerify(TerrainFields* self, void*, int rotation, int index, int page) {
    const float* expected = s_tileUv(self, nullptr, rotation, index, page);
    ei::verify::Check("TerrainFields::TileUv", expected, self->TileUv(rotation, index, page), "%d %d %d", rotation, index, page);
    return expected;
}
// 006c53c0 MOV EAX,[ESP+0xc]; PUSH ESI (5 bytes)
EI_VERIFY(0x006C53C0, TileUvVerify, 5, s_tileUv);

// 006C8D40 TerrainFields::SurfaceClass: side by side (the sector lookup only refreshes the cache).
static int(__fastcall* s_surfaceClass)(TerrainFields*, void*, float, float);
static int __fastcall SurfaceClassVerify(TerrainFields* self, void*, float x, float y) {
    int expected = s_surfaceClass(self, nullptr, x, y);
    ei::verify::Check("TerrainFields::SurfaceClass", expected, self->SurfaceClass(x, y), "x=%g y=%g", x, y);
    return expected;
}
// 006c8d40 PUSH EBP; MOV EBP,ESP; SUB ESP,0xc (6 bytes)
EI_VERIFY(0x006C8D40, SurfaceClassVerify, 6, s_surfaceClass);

// 006C8930 Terrain::GroundAndWaterDepth: outputs only; ours into scratch first.
static void(__fastcall* s_groundDepth)(Terrain*, void*, float, float, float*, float*);
static void __fastcall GroundDepthVerify(Terrain* self, void*, float x, float y, float* ground, float* depth) {
    float g = 0, d = 0;
    self->GroundAndWaterDepth(x, y, &g, &d);
    s_groundDepth(self, nullptr, x, y, ground, depth);
    ei::verify::Check("Terrain::GroundAndWaterDepth", *ground, g, "ground x=%g y=%g", x, y);
    ei::verify::Check("Terrain::GroundAndWaterDepth", *depth, d, "depth x=%g y=%g", x, y);
}
// 006c8930 PUSH EBX; MOV EBX,[ESP+0x8] (5 bytes)
EI_VERIFY(0x006C8930, GroundDepthVerify, 5, s_groundDepth);
