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
#include "random.h"

#include "ei/x87.h"
#include "lib/bytes.h"
#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/str.h"
#include "lib/text_case.h"
#include "lib/vecmath.h"

#include <cstdint>
#include <cstring>
#include <format>
#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

namespace terrain {

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
void AddTile(VertexTiles& entry, int x, int y) {
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

// The map file's material and animated texture entries.
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
constexpr int kExtraTextures = kExtraPages * kExtraPageTextures;   // after the map's own
constexpr int kExtraClass = 0xF;
constexpr float kMarginPixels = 8.0f;
constexpr double kByteScale = 255.0;

// The map file's extension.
constexpr const char* kMapDataExtension = /*007441D4*/ ".MP";

// The tile at (column, row) of a page with tiles `size` wide and `margin` pixels inside their edges, turned
// `rotation` quarter turns; with `wholePage` the v run covers the page's whole height (the extra pages hold
// one row).
TileUv BuildTileUv(int rotation, int column, int row, float size, float margin, bool wholePage) {
    constexpr int kSide = 3;                  // a tile's vertices per row
    TileUv uv;
    float* u = uv.u;
    float* v = uv.v;
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
    for (int i = 0; i < TileUv::kVertices; ++i) {
        u[i] = uBase + u[i];
        v[i] = vBase + v[i];
    }
    return uv;
}

// The map file's header when the file is a map file: the magic and the size the header's counts give (wrapping
// at 32 bits, as the counts are not checked).
std::optional<MapHeader> ReadMapHeader(const std::uint8_t* file, std::uint32_t size) {
    if (size < sizeof(MapHeader)) return std::nullopt;
    MapHeader header;
    lib::LoadBytes(header, file);
    const std::uint32_t expected = sizeof(MapHeader) + header.materials * sizeof(MapMaterial) +
                                   static_cast<std::uint32_t>(header.textures) * sizeof(std::int32_t) +
                                   header.animated * sizeof(MapAnimatedTexture);
    if (header.magic != kMapMagic || size != expected) return std::nullopt;
    return header;
}

// The ambient sound set of a zone.
const char* AmbientSetOf(std::string_view zone, bool dungeon) {
    if (dungeon) return "Dungeon";
    if (text::CompareIgnoringCase(zone, "Ingos") == 0) return "Ingos";
    if (text::CompareIgnoringCase(zone, "Suslanger") == 0) return "Suslanger";
    return "Gipat";
}

} // namespace

int Terrain::LayerPages() const { return pages + kExtraPages; }

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
        if (!sector->Load(&archive, sx, sy, mapName.c_str(), this)) {
            delete sector;
            return nullptr;
        }
        sector->lastUsed = frame;
        sectorCache.insert(sectorCache.begin(), sector);
        cell = sector;
    } else if (sector->lastUsed < frame) {
        sector->lastUsed = frame;
        std::erase(sectorCache, sector);
        sectorCache.insert(sectorCache.begin(), sector);
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
    VertexTiles* out = vertexTiles.data();
    for (int y = 0; y < kSectorRow; ++y) {
        for (int x = 0; x < kSectorRow; ++x) {
            VertexTiles entry{};
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
    for (SectorData* sector : sectorCache) delete sector;
    sectorCache.clear();
    for (CTexture* texture : pageTextures) texture->Release();
    pageTextures.clear();
    archive.CloseResource();
    sectorGrid.clear();
    sectorsX = -1;
    sectorsY = -1;
    mapLoaded = false;
    for (std::vector<TileUv>& table : uvTables) table.clear();
    for (std::vector<TileUv>& table : uvTablesLast) table.clear();
}

Terrain::Terrain() {
    // Identity rotations (w first); the positions are not initialised.
    localRotation = Quaternion{1.0f, 0.0f, 0.0f, 0.0f};
    worldRotation = Quaternion{1.0f, 0.0f, 0.0f, 0.0f};
    Reset();
}

Terrain::~Terrain() { Terrain::Destroy(); }

void Terrain::Create() {
    Reset();
    BuildVertexTiles();
    BuildWavePhases();
    for (int i = 0; i < 512; ++i) sine[i] = static_cast<float>(x87::fsin(i * kPiDouble * kSineStep));
    localPosition = kZeroPosition;
    localRotation = kIdentityRotation;
}

void Terrain::Destroy() {
    ClearMap();
    for (auto& [objectId, light] : lights) light->Release();
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
    for (int i = 0; i < 4; ++i) {
        uvTables[i].clear();
        uvTablesLast[i].clear();
    }
    waveDirection = {0.0f, 0.0f, 1.0f};
    sectorGrid.clear();
    ambient.x = ambient.y = ambient.z = 0.0f;
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
        float centreX = static_cast<float>(ClientRand() % kSectorRow);
        float centreY = static_cast<float>(ClientRand() % kSectorRow);
        int radius = ClientRand() % 5 + 1;
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

void Terrain::SetWaves(const Vector3& direction, float strength) {
    waveDirection = direction;
    SetWaveStrength(strength);
}

int Terrain::SetupPages(int count) {
    pageTextures.resize(count);
    for (int i = 0; i < count + kExtraPages; ++i) {
        std::string name;
        if (i < count) {
            name = std::format("{}{:03}", mapName, i);
        } else {
            name = std::format("{}L{:02}", mapName, i - count);
            if (!g_clientWorld.textureManager->HasFile((name + ".mmp"))) return 1;
            pageTextures.push_back(nullptr);
        }
        CTexture* texture = g_clientWorld.textureManager->FindOrCreate(kPageTexture, name);
        pageTextures[i] = texture;
        texture->AddRef();
        if (!g_clientWorld.textureManager->LoadTexture(pageTextures[i])) return 0;
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
    const std::optional<MapHeader> header = ReadMapHeader(file, size);
    if (!header) {
        ClearMap();
        return game::FatalError("Wrong file format: " + name);
    }
    ReadMap(*header, file);
    if (!SetupPages(pages)) {
        ClearMap();
        return 0;
    }
    BuildUvTables(header->tileSize);
    mapLoaded = true;

    ambientSet = AmbientSetOf(zone, dungeon);
    CountAmbientVariants();
    landscapeFlags |= LandscapeFlag::All;
    waterLevels.assign(static_cast<std::size_t>(sectorsY * sectorsX) * materials.size(), 0.0f);
    return 1;
}

void Terrain::ReadMap(const MapHeader& header, const std::uint8_t* file) {
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
        TerrainMaterial& material = materials[i];
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
}

void Terrain::BuildUvTables(int tileSize) {
    // A tile's side as a part of the page, and 8 pixels of the page inside each of its edges.
    const float tileSide = static_cast<float>(1.0 / tilesPerRow);
    const float margin = static_cast<float>(static_cast<float>(static_cast<double>(tileSide) / tileSize) * kMarginPixels);
    for (int rotation = 0; rotation < 4; ++rotation) {
        uvTables[rotation].assign(static_cast<std::size_t>(perPage), TileUv{});
        int index = 0;
        for (int row = 0; row < tilesPerRow; ++row)
            for (int column = 0; column < tilesPerRow; ++column, ++index)
                uvTables[rotation][index] = BuildTileUv(rotation, column, row, tileSide, margin, false);
    }
    for (int rotation = 0; rotation < 4; ++rotation) {
        uvTablesLast[rotation].assign(static_cast<std::size_t>(perPage), TileUv{});
        for (int column = 0; column < tilesPerRow; ++column)
            uvTablesLast[rotation][column] = BuildTileUv(rotation, column, 0, tileSide, margin, true);
    }
}

// ---- The point lights, water levels, tile texture coordinates -----------------------------------------

void Terrain::AddLight(CLight* light) {
    if (!mapLoaded) return;
    light->AddRef();
    lights[light->id] = light;
}

void Terrain::RemoveLight(CLight* light) {
    if (!mapLoaded) return;
    lights.erase(light->id);
    light->Release();
}

const TileUv& Terrain::TileUvAt(int rotation, int index, int page) const {
    if (page % LayerPages() >= pages) return uvTablesLast[rotation][index];
    return uvTables[rotation][index];
}

std::span<float> Terrain::SectorWaterLevels(int sx, int sy) {
    const std::size_t count = materials.size();
    return std::span<float>(waterLevels).subspan(static_cast<std::size_t>(sy * sectorsX + sx) * count, count);
}

void Terrain::SetWaterLevel(int material, float level) {
    if (material < 0 || material >= std::ssize(materials)) return;
    float change = static_cast<float>(level - materials[material].level);
    materials[material].level = change + materials[material].level;
    for (std::size_t i = material; i < waterLevels.size(); i += materials.size()) waterLevels[i] = change + waterLevels[i];
}

} // namespace terrain
