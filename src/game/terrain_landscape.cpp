// The landscape object's own members (Terrain, terrain.h): the sector cache, the map loader, the waves'
// setup and the tile texture coordinates. Sector loading and building are in terrain_sector.cpp, height
// queries in terrain.cpp.
#include "terrain.h"
#include "terrain_sector.h"
#include "console.h"
#include "sound.h"
#include "spell.h"
#include "texture.h"
#include "game_singletons.h"
#include "world.h"

#include "ei/x87.h"
#include "lib/bytes.h"
#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/str.h"
#include "lib/text_case.h"
#include "lib/vecmath.h"

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

using terrain::SectorData;
using terrain::kSectorCells;
using terrain::kSectorRow;
using terrain::kSectorTiles;
using terrain::kSectorVertices;

namespace {

// A sector bound: the value rounded to nearest, divided by 32 towards zero, clamped to 0..count - 1 in this
// order (a map without sectors gives -1).
int SectorBound(float value, int count) {
    int sector = x87::fistp(value) / kSectorCells;
    if (sector < 0) return 0;
    if (sector > count - 1) return count - 1;
    return sector;
}

// How many frames an unused sector stays cached before a load may reuse or free it.
constexpr int kSectorAge = 240;

// Appends tile (x, y) of the sector when it exists.
void AddTile(terrain::VertexTiles& entry, int x, int y) {
    if (x >= 0 && y >= 0 && x < kSectorTiles && y < kSectorTiles) entry.tiles[entry.count++] = y * kSectorTiles + x;
}

constexpr double kSineStep = 1.0 / 256;
constexpr float kWaveStepTurn = 0.500250161f;
constexpr int kRipples = 40;

// The position and rotation Create gives the landscape, and the waves' scales.
constexpr Vector3 kZeroPosition = /*007C8988*/ {0.0f, 0.0f, 0.0f};
constexpr Quaternion kIdentityRotation = /*007C8978*/ {1.0f, 0.0f, 0.0f, 0.0f};
constexpr float kWaveHeightScale = /*007C8970*/ 0.3f;
constexpr float kWaveDirectionScale = /*007C896C*/ 0.44879898f;   // pi / 7
constexpr float kWaveSpeedScale = /*007C8968*/ -0.14959967f;      // that * -1/3

constexpr int kPageTexture = static_cast<int>(TextureKind::TerrainPage);     // texture kind of the terrain pages

// The map file (<map>.MP in the map's archive): this header, the materials, a class per tile
// texture, then the animated textures.
#pragma pack(push, 1)
struct MapHeader {
    std::uint32_t magic;
    float heightRange;
    std::int32_t sectorsX;
    std::int32_t sectorsY;
    std::int32_t pages;
    std::int32_t pageSize;
    std::int32_t textures;
    std::int32_t tileSize;
    std::uint16_t materials;
    std::uint32_t animated;
};
#pragma pack(pop)
static_assert(sizeof(MapHeader) == 0x26);

struct MapMaterial {
    int kind;
    Vector3 colour;
    float opacity;
    float light;                    // scales the colour
    float priority;
    float warpSpeed;                // (the name modding notes give it); copied, nothing reads it
    std::uint8_t unused_20[0xC];    // not read
};
static_assert(sizeof(MapMaterial) == 0x2C);

struct MapAnimatedTexture {
    std::uint16_t texture;
    std::uint16_t frames;
};

constexpr std::uint32_t kMapMagic = 0xCE4AF672;
constexpr int kExtraTextures = terrain::kExtraPages * terrain::kExtraPageTextures;   // after the map's own
constexpr int kExtraClass = 0xF;
constexpr int kUvFloats = 18;           // per texture: 9 u, 9 v
constexpr float kMarginPixels = 8.0f;
constexpr double kByteScale = 255.0;

// The map file's extension.
constexpr const char* kMapDataExtension = /*007441D4*/ ".MP";

// 9 u then 9 v (rows of three) of the tile at (column, row) of a page with tiles `size` wide and `margin`
// pixels inside their edges, turned `rotation` quarter turns; with `wholePage` the v run covers the page's
// whole height (the extra pages hold one row).
void BuildTileUv(float* uv, int rotation, int column, int row, float size, float margin, bool wholePage) {
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

} // namespace

int Terrain::LayerPages() const { return pages + terrain::kExtraPages; }

SectorData* Terrain::SectorAt(int sx, int sy, SectorStage wanted, bool create) {
    if (sx < 0 || sy < 0 || sx >= sectorsX || sy >= sectorsY) return nullptr;
    SectorData*& cell = sectorGrid[sx * sectorsY + sy];
    SectorData* sector = cell;
    if (!sector) {
        if (!create) return nullptr;
        // A new sector, or the least recently used one when it is old enough; then every further
        // sector at the cache's end that is old enough is freed.
        SectorData* last = !sectorCache.empty() ? sectorCache.back() : nullptr;
        if (!last || frame - kSectorAge <= last->lastUsed) {
            sector = new SectorData();
        } else {
            sector = last;
            sectorGrid[sector->sx * sectorsY + sector->sy] = nullptr;
            sectorCache.pop_back();
            while (!sectorCache.empty() && sectorCache.back()->lastUsed < frame - kSectorAge) {
                SectorData* old = sectorCache.back();
                sectorCache.pop_back();
                sectorGrid[old->sx * sectorsY + old->sy] = nullptr;
                delete old;
            }
        }
        if (!sector->Load(&archive, sx, sy, pages, perPage, mapName.c_str(), this)) {
            delete sector;
            return nullptr;
        }
        sector->lastUsed = frame;
        sectorCache.push_front(sector);
        cell = sector;
    } else if (sector->lastUsed < frame) {
        sector->lastUsed = frame;
        sectorCache.remove(sector);
        sectorCache.push_front(sector);
    }
    if (create) sector->Ensure(wanted);
    return lib::Has(sector->built, wanted) ? sector : nullptr;
}

void Terrain::SetVisibleSectors(float x, float y, float radius) {
    visible.firstX = SectorBound(x - radius - 0.5f, sectorsX);
    visible.lastX = SectorBound(x + radius - 0.5f, sectorsX);
    visible.firstY = SectorBound(y - radius - 0.5f, sectorsY);
    visible.lastY = SectorBound(y + radius - 0.5f, sectorsY);
}

// ---- Vertex tiles, the map's end, construction, the waves --------------------------------------------

void Terrain::BuildVertexTiles() {
    vertexTiles.resize(kSectorVertices);
    terrain::VertexTiles* out = vertexTiles.data();
    for (int y = 0; y < kSectorRow; ++y) {
        for (int x = 0; x < kSectorRow; ++x) {
            terrain::VertexTiles entry{};
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

void Terrain::ClearMap() {
    StopAmbientLoops();
    while (!sectorCache.empty()) {
        SectorData* sector = sectorCache.front();
        sectorCache.pop_front();
        delete sector;
    }
    for (CTexture* texture : pageTextures) texture->Release();
    pageTextures.clear();
    archive.CloseResource();
    sectorGrid.clear();
    sectorsX = -1;
    sectorsY = -1;
    mapLoaded = false;
    for (std::vector<float>& table : uvTables) table.clear();
    for (std::vector<float>& table : uvTablesLast) table.clear();
}

Terrain::Terrain() {
    // Identity rotations (w first); the positions are not initialised.
    localRotation_ = Quaternion{1.0f, 0.0f, 0.0f, 0.0f};
    worldRotation_ = Quaternion{1.0f, 0.0f, 0.0f, 0.0f};
    Reset();
}

Terrain::~Terrain() { Terrain::Destroy(); }

void Terrain::Create() {
    Reset();
    BuildVertexTiles();
    BuildWavePhases();
    for (int i = 0; i < 512; ++i) sine[i] = static_cast<float>(x87::fsin(i * kPiDouble * kSineStep));
    localPosition_ = kZeroPosition;
    localRotation_ = kIdentityRotation;
}

void Terrain::Destroy() {
    ClearMap();
    for (auto& [id, entry] : lights) entry.light->Release();
    lights.clear();
    Reset();
}

void Terrain::Reset() {
    landscapeFlags = LandscapeFlag::None;
    pageTextures.clear();
    mapLoaded = false;
    mapName = "none";
    sectorsX = -1;
    sectorsY = -1;
    perPage = -1;
    frame = 1;
    lightInterval = -1;
    lightCountdown = -1;
    for (int i = 0; i < 4; ++i) {
        uvTables[i].clear();
        uvTablesLast[i].clear();
    }
    waveDirection = {0.0f, 0.0f, 1.0f};
    sectorGrid.clear();
    ambient.x = ambient.y = ambient.z = 0.0f;
    sunSlopeX = sunSlopeY = sunRunX = sunRunY = 0.0f;
    sunStepX = sunStepY = -1;
    sun = nullptr;
    SetWaveStrength(0.0f);
    wavePhase = kPi + kPi;
    visible = {};
    for (AmbientLoop& loop : ambientLoops) loop = {};
    ambientSet = "none";
}

void Terrain::BuildWavePhases() {
    wavePhases.assign(kSectorVertices, 0.0f);
    float* phases = wavePhases.data();
    for (int ripple = 0; ripple < kRipples; ++ripple) {
        float centreX = static_cast<float>(libcrt::Rand() % kSectorRow);
        float centreY = static_cast<float>(libcrt::Rand() % kSectorRow);
        int radius = libcrt::Rand() % 5 + 1;
        double radiusSquared = static_cast<double>(radius) * radius;
        float* phase = phases;
        for (int row = 0; row < kSectorRow; ++row) {
            double dy = row - centreY;
            double dySquared = dy * dy;
            for (int column = 0; column < kSectorRow; ++column, ++phase) {
                double dx = column - centreX;
                double distanceSquared = dx * dx + dySquared;
                if (distanceSquared < radiusSquared)
                    *phase = static_cast<float>((1.0 - distanceSquared / radiusSquared) * kPi * 0.5f + *phase);
            }
        }
    }
    // The first and last rows, then the first and last columns, averaged pairwise; the four corners
    // get the average of all four.
    constexpr int kLastRow = kSectorRow * (kSectorRow - 1);
    for (int i = 1; i < kSectorRow - 1; ++i) {
        float average = (phases[kLastRow + i] + phases[i]) * 0.5f;
        phases[i] = average;
        phases[kLastRow + i] = average;
    }
    for (int i = kSectorRow; i < kLastRow; i += kSectorRow) {
        float average = (phases[i + kSectorRow - 1] + phases[i]) * 0.5f;
        phases[i] = average;
        phases[i + kSectorRow - 1] = average;
    }
    float corners = (((phases[kLastRow] + phases[kSectorVertices - 1]) + phases[kSectorRow - 1]) + phases[0]) * 0.25f;
    phases[0] = corners;
    phases[kSectorRow - 1] = corners;
    phases[kSectorVertices - 1] = corners;
    phases[kLastRow] = corners;
}

void Terrain::SetWaveStrength(float strength) {
    strength = strength >= 0.0f ? strength : 0.0f;
    strength = 1.0f >= strength ? strength : 1.0f;
    waveGlow = strength;
    waveHeight = kWaveHeightScale * strength;
    waveSpeed = kWaveSpeedScale * strength;
    double step = libcrt::CiAsin(1.0f - strength) + kPi * kWaveStepTurn;
    float length = LengthXYZ(waveDirection);
    if (length > 0.0f) waveDirection *= 1.0f / length;
    waveDirection.x = static_cast<float>(kWaveDirectionScale * waveDirection.x * step);
    waveDirection.y = static_cast<float>(kWaveDirectionScale * waveDirection.y * step);
}

void Terrain::SetWaves(const Vector3* direction, float strength) {
    waveDirection = *direction;
    SetWaveStrength(strength);
}

int Terrain::SetupPages(int count) {
    pageTextures.resize(count);
    for (int i = 0; i < count + terrain::kExtraPages; ++i) {
        std::string name;
        if (i < count) {
            name = str::Format("%s%3.3i", mapName, i);
        } else {
            name = str::Format("%sL%2.2i", mapName, i - count);
            if (!g_clientWorld.textureManager_->HasFile((name + ".mmp"))) return 1;
            pageTextures.push_back(nullptr);
        }
        CTexture* texture = g_clientWorld.textureManager_->FindOrCreate(kPageTexture, name);
        pageTextures[i] = texture;
        texture->AddRef();
        if (!g_clientWorld.textureManager_->LoadTexture(pageTextures[i])) return 0;
    }
    return 1;
}

// --- The map loader ----------------------------------------------------------------------------

int Terrain::LoadMap(std::string_view path, std::string_view zone, bool dungeon) {
    ClearMap();
    const std::string name(path);
    if (!archive.OpenResource(name, 0)) return 0;
    std::vector<std::string> files;
    archive.ListFiles(&files, "*.mp");
    // (An archive without a map file is not tested for either.)
    const std::string& first = files[0];
    mapName = str::Left(first, str::Find(first, '.'));

    // The messages are passed to FatalError as its format string.
    std::uint32_t size = 0;
    const auto* file = static_cast<const std::uint8_t*>(archive.GetFile((mapName + kMapDataExtension), &size));
    if (!file) {
        ClearMap();
        return game::FatalError("Unable to open file \"" + name + "\"");
    }
    if (size < sizeof(MapHeader)) {
        ClearMap();
        return game::FatalError("Wrong file format: " + name);
    }
    MapHeader header;
    lib::LoadBytes(header, file);
    // The sizes wrap at 32 bits, as the header's counts are not checked.
    const std::uint32_t expected = sizeof(MapHeader) + header.materials * sizeof(MapMaterial) +
                                   static_cast<std::uint32_t>(header.textures) * sizeof(std::int32_t) +
                                   header.animated * sizeof(MapAnimatedTexture);
    if (header.magic != kMapMagic || size != expected) {
        ClearMap();
        return game::FatalError("Wrong file format: " + name);
    }

    sectorsX = header.sectorsX;
    sectorsY = header.sectorsY;
    sectorGrid.assign(static_cast<std::size_t>(sectorsX * sectorsY), nullptr);
    width = static_cast<float>(sectorsX * kSectorCells);
    height = static_cast<float>(sectorsY * kSectorCells);
    heightRange = header.heightRange;
    pages = header.pages;
    tilesPerRow = header.pageSize / header.tileSize;
    perPage = tilesPerRow * tilesPerRow;

    const std::uint8_t* cursor = file + sizeof(MapHeader);
    const int materialCount = header.materials;
    materials.resize(materialCount);
    for (int i = 0; i < materialCount; ++i, cursor += sizeof(MapMaterial)) {
        MapMaterial source;
        lib::LoadBytes(source, cursor);
        terrain::TerrainMaterial& material = materials[i];
        material.kind = source.kind;
        material.waveColour = source.colour;
        material.opacity = source.opacity;
        material.absorption = static_cast<float>(1.0 / static_cast<float>(1.0 - source.opacity));
        material.opacityByte = static_cast<std::uint8_t>(libcrt::Ftol(static_cast<float>(material.opacity * kByteScale)));
        material.colour.x = MinKeepA(1.0f, static_cast<float>(static_cast<double>(source.light) * material.waveColour.x));
        material.colour.y = MinKeepA(1.0f, static_cast<float>(static_cast<double>(source.light) * material.waveColour.y));
        material.colour.z = MinKeepA(1.0f, static_cast<float>(static_cast<double>(source.light) * material.waveColour.z));
        material.priority = source.priority;
        material.warpSpeed = source.warpSpeed;
        material.level = 0.0f;
    }

    const int textures = header.textures;
    const std::size_t classBytes = static_cast<std::size_t>(textures * 4);
    textureClass.resize(textures + kExtraTextures);
    std::memcpy(textureClass.data(), cursor, classBytes);
    for (int i = 0; i < kExtraTextures; ++i) textureClass[textures + i] = kExtraClass;
    textureFrames.assign(textures, 0);
    const std::uint8_t* animated = cursor + classBytes;
    for (int i = 0; i < static_cast<int>(header.animated); ++i, animated += sizeof(MapAnimatedTexture)) {
        MapAnimatedTexture entry;
        lib::LoadBytes(entry, animated);
        textureFrames[entry.texture] = entry.frames;
    }

    if (!SetupPages(pages)) {
        ClearMap();
        return 0;
    }

    // A tile's side as a part of the page, and 8 pixels of the page inside each of its edges.
    const float tileSide = static_cast<float>(1.0 / tilesPerRow);
    const float margin = static_cast<float>(static_cast<float>(static_cast<double>(tileSide) / header.tileSize) * kMarginPixels);
    for (int rotation = 0; rotation < 4; ++rotation) {
        uvTables[rotation].assign(static_cast<std::size_t>(perPage * kUvFloats), 0.0f);
        int index = 0;
        for (int row = 0; row < tilesPerRow; ++row)
            for (int column = 0; column < tilesPerRow; ++column, ++index)
                BuildTileUv(uvTables[rotation].data() + index * kUvFloats, rotation, column, row, tileSide, margin, false);
    }
    for (int rotation = 0; rotation < 4; ++rotation) {
        uvTablesLast[rotation].assign(static_cast<std::size_t>(perPage * kUvFloats), 0.0f);
        for (int column = 0; column < tilesPerRow; ++column)
            BuildTileUv(uvTablesLast[rotation].data() + column * kUvFloats, rotation, column, 0, tileSide, margin, true);
    }
    mapLoaded = true;

    const char* set = "Gipat";
    if (dungeon) set = "Dungeon";
    else if (text::CompareIgnoringCase(zone, "Ingos") == 0) set = "Ingos";
    else if (text::CompareIgnoringCase(zone, "Suslanger") == 0) set = "Suslanger";
    ambientSet = set;
    terrain::CountAmbientVariants(ambientSet.c_str());

    const int sectors = sectorsY * sectorsX;
    landscapeFlags |= LandscapeFlag::All;
    waterLevels.resize(sectors);
    for (std::vector<float>& levels : waterLevels) levels.assign(materials.size(), 0.0f);
    return 1;
}

// ---- The light interval, water levels, tile texture coordinates ----------------------------------------

const float* Terrain::TileUv(int rotation, int index, int page) {
    if (page % LayerPages() >= pages) return uvTablesLast[rotation].data() + index * kUvFloats;
    return uvTables[rotation].data() + index * kUvFloats;
}

void Terrain::SetLightInterval(int frames) {
    lightInterval = frames;
    if (frames < 0) lightCountdown = -1;
    if (lightCountdown < frames) lightCountdown = frames;
}

void Terrain::SetWaterLevel(int material, float level) {
    if (material < 0 || material >= std::ssize(materials)) return;
    float change = static_cast<float>(level - materials[material].level);
    materials[material].level = change + materials[material].level;
    for (int sector = 0; sector < sectorsY * sectorsX; ++sector) {
        float& sectorLevel = waterLevels[sector][material];
        sectorLevel = change + sectorLevel;
    }
}
