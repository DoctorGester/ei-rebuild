// The landscape object's own members (Terrain, terrain.h; 006C5430-006CCF20) and the aligned-array
// instantiations next to them (006CD5C0-006CE890). Sector loading and building are in
// terrain_sector.cpp, height queries in terrain.cpp.
#include "terrain.h"
#include "terrain_sector.h"
#include "material.h"
#include "sound.h"
#include "spell.h"
#include "texture.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "originals_render.h"
#include "originals_ui.h"
#include "originals_world.h"

#include <cstdint>
#include <cstring>
#include <initializer_list>

using terrain::SectorData;

// 006BF8A0: the sector's destructor body (not rewritten).
__declspec(naked) void SectorData::Destruct() EI_ORIGINAL(0x006BF8A0)

namespace {

// A sector bound: FISTP of the value stored as a float (the original stores it, then loads it back),
// divided by 32 towards zero, clamped to 0..count - 1 in this order (a map without sectors gives -1).
int SectorBound(float value, int count) {
    int sector = x87::fistp(value) / 32;
    if (sector < 0) return 0;
    if (sector > count - 1) return count - 1;
    return sector;
}

// How many frames an unused sector stays cached before a load may reuse or free it.
constexpr int kSectorAge = 240;

void DeleteSector(SectorData* sector) {
    sector->Destruct();
    gamecrt::operator_delete(sector);
}

} // namespace

TerrainSector* Terrain::SectorAt(int sx, int sy, std::uint32_t wanted, int create) {
    if (sx < 0 || sy < 0 || sx >= sectorsX || sy >= sectorsY) return nullptr;
    auto* fields = reinterpret_cast<terrain::TerrainFields*>(this);
    SectorCache::Node* node = sectorGrid[sx][sy];
    if (node) {
        SectorData* sector = node->data;
        if (sector->lastUsed < frame) {
            sector->lastUsed = frame;
            sectorCache.RemoveAt(node);
            sectorCache.AddHead(sector);
            sectorGrid[sx][sy] = sectorCache.head;
        }
        if (create) sector->Ensure(wanted, fields);
        return (sector->built & wanted) ? reinterpret_cast<TerrainSector*>(sector) : nullptr;
    }
    if (!create) return nullptr;

    // A new sector, or the least recently used one when it is old enough; then every further
    // sector at the cache's end that is old enough is freed.
    SectorData* sector;
    SectorData* last = sectorCache.count != 0 ? sectorCache.tail->data : nullptr;
    if (!last || frame - kSectorAge <= last->lastUsed) {
        auto* memory = static_cast<SectorData*>(gamecrt::operator_new(sizeof(SectorData)));
        sector = memory ? memory->Construct() : nullptr;
    } else {
        sector = last;
        sectorGrid[sector->sx][sector->sy] = nullptr;
        sectorCache.RemoveTail();
        while (sectorCache.count != 0 && sectorCache.tail->data->lastUsed < frame - kSectorAge) {
            SectorData* old = sectorCache.RemoveTail();
            sectorGrid[old->sx][old->sy] = nullptr;
            if (old) DeleteSector(old);
        }
    }
    if (sector->Load(&archive, sx, sy, pages, perPage, textF8, fields)) {
        sector->lastUsed = frame;
        sectorCache.AddHead(sector);
        sectorGrid[sx][sy] = sectorCache.head;
        sector->Ensure(wanted, fields);
        return (sector->built & wanted) ? reinterpret_cast<TerrainSector*>(sector) : nullptr;
    }
    if (sector) DeleteSector(sector);
    return nullptr;
}
EI_REPLACE_METHOD(0x006C8140, Terrain::SectorAt);

void Terrain::SetVisibleSectors(float x, float y, float radius) {
    int columnFirst = SectorBound(x - radius - 0.5f, sectorsX);
    int columnLast = SectorBound(x + radius - 0.5f, sectorsX);
    int rowFirst = SectorBound(y - radius - 0.5f, sectorsY);
    int rowLast = SectorBound(y + radius - 0.5f, sectorsY);
    int rows = rowLast - rowFirst + 1;
    if (rows < 0) rows = 0;
    visibleColumnFirst.SetSize(rows);
    visibleColumnLast.SetSize(rows);
    for (int row = 0; row < rows; ++row) {
        visibleColumnFirst[row] = columnFirst;
        visibleColumnLast[row] = columnLast;
    }
    visibleRowFirst = rowFirst;
}
EI_REPLACE_METHOD(0x006CA260, Terrain::SetVisibleSectors);

// ---- Create, Destroy, Reset and the waves --------------------------------------------------------

namespace {

constexpr int kSectorTiles = 16;          // tiles per sector row (2 x 2 cells each)

// Appends tile (x, y) of the sector when it exists.
void AddTile(terrain::VertexTiles& entry, int x, int y) {
    if (x >= 0 && y >= 0 && x < kSectorTiles && y < kSectorTiles) entry.tiles[entry.count++] = y * kSectorTiles + x;
}

} // namespace

void Terrain::BuildVertexTiles() {
    constexpr int kSide = 33;   // vertices per sector row
    vertexTiles.SetSize(kSide * kSide);
    // The original fills one stack record per vertex without clearing it: the tiles after `count`
    // keep earlier vertices' values (ours start from zero).
    terrain::VertexTiles entry{};
    terrain::VertexTiles* out = vertexTiles.data;
    for (int y = 0; y < kSide; ++y) {
        for (int x = 0; x < kSide; ++x) {
            entry.count = 0;
            int tx = x / 2;
            int ty = y / 2;
            bool xEdge = (x & 1) == 0;
            bool yEdge = (y & 1) == 0;
            if (xEdge && yEdge) {
                AddTile(entry, tx - 1, ty - 1);
                AddTile(entry, tx - 1, ty);
                AddTile(entry, tx, ty - 1);
            } else if (xEdge) {
                AddTile(entry, tx - 1, ty);
            } else if (yEdge) {
                AddTile(entry, tx, ty - 1);
            }
            AddTile(entry, tx, ty);
            *out++ = entry;
        }
    }
}
EI_REPLACE_METHOD(0x006C7A80, Terrain::BuildVertexTiles);

void Terrain::ClearMap() {
    for (int slot = 0; slot < 3; ++slot) {
        ambientSounds[slot] = sound::Manager()->StopSound(ambientSounds[slot]);
        ambientClasses[slot] = -1;
    }
    while (sectorCache.count != 0) {
        SectorData* sector = sectorCache.RemoveHead();
        if (sector) DeleteSector(sector);
    }
    for (int i = 0; i < pageTextures.size; ++i) pageTextures[i]->Release();
    pageTextures.SetSize(0, -1);
    for (int i = 0; i < pageMaterials.size; ++i) pageMaterials[i]->Release();
    pageMaterials.SetSize(0, -1);
    archive.CloseResource();
    if (sectorGrid) {
        for (int i = 0; i < sectorsX; ++i) gamecrt::operator_delete(sectorGrid[i]);
        gamecrt::operator_delete(sectorGrid);
        sectorGrid = nullptr;
    }
    sectorsX = -1;
    sectorsY = -1;
    unknown_100 = nullptr;
    for (float*& table : uvTables) {
        if (table) gamecrt::operator_delete(table);
        table = nullptr;
    }
    for (float*& table : uvTablesLast) {
        if (table) gamecrt::operator_delete(table);
        table = nullptr;
    }
}
EI_REPLACE_METHOD(0x006C7750, Terrain::ClearMap);

namespace {

constexpr int kRow = 33;                  // vertices per sector row
constexpr int kVertices = kRow * kRow;
constexpr float kPi = 3.14159274f;        // 007441C8
constexpr double kPiDouble = 3.141592653589793;   // 007441C0
constexpr double kSineStep = 1.0 / 256;   // 007441F0
constexpr float kWaveStepTurn = 0.500250161f;     // 007441F8
constexpr int kRipples = 40;

// Static initialisers (006C52D0, 006C52F0, 006C5360..006C53A0): the zero position and identity
// rotation Create gives the landscape, and the waves' scales (computed from other constants at
// startup, read from memory).
const Vector3& ZeroPosition() { return *reinterpret_cast<const Vector3*>(0x007C8988); }
const Quaternion& IdentityRotation() { return *reinterpret_cast<const Quaternion*>(0x007C8978); }
float WaveHeightScale() { return *reinterpret_cast<const float*>(0x007C8970); }
float WaveSpeedScale() { return *reinterpret_cast<const float*>(0x007C8968); }
float WaveDirectionScale() { return *reinterpret_cast<const float*>(0x007C896C); }

// The CString members are declared as their data pointers (the client world's Setup stores the
// empty string's there); assignments go through MFC's operator=.
mfc::CString& AsString(const char*& text) { return reinterpret_cast<mfc::CString&>(text); }

// Reset (LandscapeResetOriginal) and the wave phases are called through their addresses, so that
// their verify wrappers run.
__declspec(naked) void __fastcall BuildWavePhasesAt(Terrain*) EI_ORIGINAL(0x006C57E0)
// The same for the vertex tile table and the map's freeing.
__declspec(naked) void __fastcall BuildVertexTilesAt(Terrain*) EI_ORIGINAL(0x006C7A80)
__declspec(naked) void __fastcall ClearMapAt(Terrain*) EI_ORIGINAL(0x006C7750)

} // namespace

int Terrain::Create() {
    LandscapeResetOriginal(this);
    BuildVertexTilesAt(this);
    BuildWavePhasesAt(this);
    for (int i = 0; i < 512; ++i) sine[i] = static_cast<float>(x87::fsin(i * kPiDouble * kSineStep));
    localPosition_ = ZeroPosition();
    localRotation_ = IdentityRotation();
    return 1;
}

void Terrain::Destroy() {
    ClearMapAt(this);
    for (LightTable::Node* node = lights.First(); node; node = lights.After(node)) node->value.light->Release();
    maxLightId = 0;
    lights.Clear();
    sectorCache.RemoveAll();
    LandscapeResetOriginal(this);
}

void Terrain::Reset() {
    extraPages = 0;
    landscapeFlags = 0;
    pageTextures.RemoveAll();
    pageMaterials.RemoveAll();
    std::memset(&worldMatrix, 0, sizeof(worldMatrix));
    unknown_100 = nullptr;
    AsString(textF8) = "none";
    sectorsX = -1;
    sectorsY = -1;
    unknown_118 = -1;
    unknown_120 = -1;
    perPage = -1;
    frame = 1;
    lightInterval = -1;
    lightCountdown = -1;
    for (int i = 0; i < 4; ++i) {
        uvTables[i] = nullptr;
        uvTablesLast[i] = nullptr;
    }
    waveDirection = {0.0f, 0.0f, 1.0f};
    sectorGrid = nullptr;
    ambient.x = ambient.y = ambient.z = 0.0f;
    sunSlopeX = sunSlopeY = sunRunX = sunRunY = 0.0f;
    sunStepX = sunStepY = -1;
    sun = nullptr;
    SetWaveStrength(0.0f);
    minDetail = 0;
    visibleRowFirst = 0;
    wavePhase = kPi + kPi;
    visibleColumnFirst.SetSize(0, -1);
    visibleColumnLast.SetSize(0, -1);
    for (int i = 0; i < 3; ++i) {
        ambientSounds[i] = -1;
        ambientClasses[i] = -1;
    }
    AsString(textAE0) = "none";
}

void Terrain::BuildWavePhases() {
    wavePhases.SetSize(kVertices);
    float* phases = wavePhases.data;
    std::memset(phases, 0, kVertices * sizeof(float));
    for (int ripple = 0; ripple < kRipples; ++ripple) {
        float centreX = static_cast<float>(gamecrt::rand() % kRow);
        float centreY = static_cast<float>(gamecrt::rand() % kRow);
        int radius = gamecrt::rand() % 5 + 1;
        double radiusSquared = static_cast<double>(radius) * radius;
        float* phase = phases;
        for (int row = 0; row < kRow; ++row) {
            double dy = row - centreY;
            double dySquared = dy * dy;
            for (int column = 0; column < kRow; ++column, ++phase) {
                double dx = column - centreX;
                double distanceSquared = dx * dx + dySquared;
                if (distanceSquared < radiusSquared)
                    *phase = static_cast<float>((1.0 - distanceSquared / radiusSquared) * kPi * 0.5f + *phase);
            }
        }
    }
    // The first and last rows, then the first and last columns, averaged pairwise; the four corners
    // get the average of all four.
    constexpr int kLastRow = kRow * (kRow - 1);
    for (int i = 1; i < kRow - 1; ++i) {
        float average = (phases[kLastRow + i] + phases[i]) * 0.5f;
        phases[i] = average;
        phases[kLastRow + i] = average;
    }
    for (int i = kRow; i < kLastRow; i += kRow) {
        float average = (phases[i + kRow - 1] + phases[i]) * 0.5f;
        phases[i] = average;
        phases[i + kRow - 1] = average;
    }
    float corners = (((phases[kLastRow] + phases[kVertices - 1]) + phases[kRow - 1]) + phases[0]) * 0.25f;
    phases[0] = corners;
    phases[kRow - 1] = corners;
    phases[kVertices - 1] = corners;
    phases[kLastRow] = corners;
}

void Terrain::SetWaveStrength(float strength) {
    if (!(strength >= 0.0f)) strength = 0.0f;
    if (!(1.0f >= strength)) strength = 1.0f;
    waveGlow = strength;
    waveHeight = WaveHeightScale() * strength;
    waveSpeed = WaveSpeedScale() * strength;
    double step = Asin(1.0f - strength) + kPi * kWaveStepTurn;
    float length = LengthXYZ(waveDirection);
    if (length > 0.0f) waveDirection *= 1.0f / length;
    waveDirection.x = static_cast<float>(WaveDirectionScale() * waveDirection.x * step);
    waveDirection.y = static_cast<float>(WaveDirectionScale() * waveDirection.y * step);
}

void Terrain::SetWaves(const Vector3* direction, float strength) {
    waveDirection = *direction;
    SetWaveStrength(strength);
}

namespace {

// The texture manager (007AF744) and the material manager (007AF740).
CTextureManager* Textures() { return *reinterpret_cast<CTextureManager* const*>(0x007AF744); }
void* Materials() { return *reinterpret_cast<void* const*>(0x007AF740); }

constexpr int kPageTexture = 0xB;     // texture kind of the terrain pages
constexpr int kPageMaterial = 0x12;   // material kind
constexpr int kExtraPages = 2;        // <map>L00, <map>L01

} // namespace

int Terrain::SetupPages(int count) {
    pageTextures.SetSize(count);
    pageMaterials.SetSize(count);
    for (int i = 0; i < count + kExtraPages; ++i) {
        mfc::CString name;
        if (i < count) {
            name.Format("%s%3.3i", textF8, i);
        } else {
            name.Format("%sL%2.2i", textF8, i - count);
            if (!Textures()->HasFile(name + ".mmp")) return 1;
            ++extraPages;
            pageTextures.SetAtGrow(pageTextures.size, nullptr);
            pageMaterials.SetAtGrow(pageMaterials.size, nullptr);
        }
        CTexture* texture = static_cast<CTexture*>(TextureNamedOriginal(Textures(), nullptr, kPageTexture, name));
        pageTextures[i] = texture;
        ++texture->refCount_;
        CMaterial* material = reinterpret_cast<CMaterial*>(MaterialCreateOriginal(Materials(), nullptr, kPageMaterial));
        pageMaterials[i] = material;
        ++material->refCount_;
        material->name = "Wood";
        material->colors.diffuse.x = material->colors.diffuse.y = material->colors.diffuse.z = 0.3f;
        material->colors.ambient.x = material->colors.ambient.y = material->colors.ambient.z = 0.7f;
        material->colors.specular.x = material->colors.specular.y = material->colors.specular.z = 0.2f;
        material->colors.emissive.x = material->colors.emissive.y = material->colors.emissive.z = 0.0f;
        material->colors.diffuse.w = 1.0f;
        material->colors.power = 5.0f;
        if (!TextureLoadOriginal(Textures(), nullptr, pageTextures[i])) return 0;
    }
    return 1;
}
EI_REPLACE_METHOD(0x006C5AA0, Terrain::SetupPages);

// ---- The world matrix, the light interval, water levels, tile texture coordinates -------------------

void Terrain::CombineRotation(const Quaternion& parent) {
    CTransformObject::CombineRotation(parent);
    WorldMatrix(worldMatrix.m);
}

void Terrain::SetLightInterval(int frames) {
    lightInterval = frames;
    if (frames < 0) lightCountdown = -1;
    if (lightCountdown < frames) lightCountdown = frames;
}
EI_REPLACE_METHOD(0x006C9290, Terrain::SetLightInterval);

void Terrain::SetWaterLevel(int material, float level) {
    if (material < 0 || material >= materials.size) return;
    float change = RoundToFloat(level - materials[material].level);
    materials[material].level = change + materials[material].level;
    for (int sector = 0; sector < sectorsY * sectorsX; ++sector) {
        float& sectorLevel = waterLevels[sector][material];
        sectorLevel = change + sectorLevel;
    }
}
EI_REPLACE_METHOD(0x006CC7F0, Terrain::SetWaterLevel);

void Terrain::BuildTileUv(float* uv, int rotation, int column, int row, float size, float margin, char wholePage) {
    constexpr int kSide = 3;                  // a tile's vertices per row
    constexpr int kTileVertices = kSide * kSide;
    float* u = uv;
    float* v = uv + kTileVertices;
    // u runs along each row, v down the rows: the margin, the middle, the far side less the margin.
    const float vSize = wholePage ? 1.0f : size;
    for (int i = 0; i < kSide; ++i) {
        u[i * kSide] = margin;
        u[i * kSide + 1] = size * 0.5f;
        u[i * kSide + 2] = size - margin;
    }
    for (int i = 0; i < kSide; ++i) {
        v[i] = margin;
        v[kSide + i] = vSize * 0.5f;
        v[2 * kSide + i] = vSize - margin;
    }
    // A quarter turn of both grids: the corners 0 <- 2 <- 8 <- 6 and the edges 1 <- 5 <- 7 <- 3.
    for (; rotation > 0; --rotation) {
        for (float* grid : {u, v}) {
            float corner = grid[0];
            grid[0] = grid[2];
            grid[2] = grid[8];
            grid[8] = grid[6];
            grid[6] = corner;
            float edge = grid[1];
            grid[1] = grid[5];
            grid[5] = grid[7];
            grid[7] = grid[3];
            grid[3] = edge;
        }
    }
    const float uBase = static_cast<float>(column) * size;
    const float vBase = wholePage ? 0.0f : static_cast<float>(row) * size;
    for (int i = 0; i < kTileVertices; ++i) {
        u[i] = uBase + u[i];
        v[i] = vBase + v[i];
    }
}
EI_REPLACE_METHOD(0x006C7940, Terrain::BuildTileUv);

int __fastcall CreateHook(Terrain* self, void*) { return self->Terrain::Create(); }
void __fastcall DestroyHook(Terrain* self, void*) { self->Terrain::Destroy(); }
EI_REPLACE(0x006C5430, CreateHook);
EI_REPLACE(0x006C54D0, DestroyHook);
void __fastcall CombineRotationHook(Terrain* self, void*, const Quaternion* parent) {
    self->Terrain::CombineRotation(*parent);
}
EI_REPLACE(0x006C6070, CombineRotationHook);
EI_REPLACE_METHOD(0x006C5620, Terrain::Reset);
EI_REPLACE_METHOD(0x006C57E0, Terrain::BuildWavePhases);
EI_REPLACE_METHOD(0x006C7D70, Terrain::SetWaveStrength);
EI_REPLACE_METHOD(0x006C7EB0, Terrain::SetWaves);

// ---- Aligned arrays of 4-byte elements (lib::AlignedArray) ----------------------------------------
// SetSize(count, growBy) (RET 8): 006CD5C0 (the client setup's array, from 006C7750), 006CE890 (the
// light countdown's sorted sectors, 006C8ED0). SetAtGrow(index, value) (RET 8): 006CDD50 /
// 006CDE90 (the map loader's tables, 006C5AA0).
EI_REPLACE_METHOD(0x006CD5C0, lib::AlignedArray<std::uint32_t>::SetSize);
EI_REPLACE_METHOD(0x006CE890, lib::AlignedArray<std::uint32_t>::SetSize);
EI_REPLACE_METHOD(0x006CDD50, lib::AlignedArray<std::uint32_t>::SetAtGrow);
EI_REPLACE_METHOD(0x006CDE90, lib::AlignedArray<std::uint32_t>::SetAtGrow);
