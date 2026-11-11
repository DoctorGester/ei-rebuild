// The landscape object's own members (Terrain, terrain.h): the sector cache, the map loader, the waves'
// setup and the tile texture coordinates. Sector loading and building are in terrain_sector.cpp, height
// queries in terrain.cpp.
#include "game/console/console.h"
#include "game/app/singletons.h"
#include "engine/scene/light.h"
#include "engine/data/map_file.h"
#include "engine/core/random.h"
#include "game/sound/sound.h"
#include "game/spells/spell.h"
#include "game/terrain/terrain.h"
#include "game/terrain/terrain_sector.h"
#include "engine/models/texture.h"
#include "game/world/world.h"

#include "lib/bytes.h"
#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/maths.h"
#include "lib/rounding.h"
#include "lib/str.h"
#include "lib/text_case.h"
#include "lib/vecmath.h"
#include "render/render.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <format>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace terrain {
namespace {

// How many frames an unused sector stays cached before a load may reuse or free it.
constexpr int kSectorAge = 240;

// Appends tile (x, y) of the sector when it exists.
void AddTile(VertexTiles& entry, int x, int y) {
    if (x >= 0 && y >= 0 && x < kSectorTiles && y < kSectorTiles) entry.tiles[entry.count++] = y * kSectorTiles + x;
}

constexpr float kSineStep = 1.0f / 256;
constexpr float kWaveStepTurn = 0.500250161f;
constexpr int kRipples = 40;

// The waves' scales.
constexpr float kWaveHeightScale = 0.3f;
constexpr float kWaveDirectionScale = 0.44879898f;   // pi / 7
constexpr float kWaveSpeedScale = -0.14959967f;      // that * -1/3

constexpr int kPageTexture = static_cast<int>(TextureKind::TerrainPage);     // texture kind of the terrain pages

constexpr int kExtraTextures = kExtraPages * kExtraPageTextures;   // after the map's own
constexpr int kExtraClass = 0xF;
constexpr float kMarginPixels = 8.0f;
constexpr float kByteScale = 255.0f;

// The map file's extension.
constexpr const char* kMapDataExtension = ".MP";

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
    MapHeader header = lib::Load<MapHeader>(file);
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
    SectorData*& cell = sectorGrid[GridIndex(sx, sy)];
    SectorData* sector = cell;
    if (!sector) {
        if (!create) return nullptr;
        // A new sector, or the least recently used one when it is old enough; then every further
        // sector at the cache's end that is old enough is freed.
        std::unique_ptr<SectorData> loaded;
        if (sectorCache.empty() || frame - kSectorAge <= sectorCache.back()->lastUsed) {
            loaded = std::make_unique<SectorData>();
        } else {
            loaded = std::move(sectorCache.back());
            sectorCache.pop_back();
            sectorGrid[GridIndex(loaded->sx, loaded->sy)] = nullptr;
            while (!sectorCache.empty() && sectorCache.back()->lastUsed < frame - kSectorAge) {
                sectorGrid[GridIndex(sectorCache.back()->sx, sectorCache.back()->sy)] = nullptr;
                sectorCache.pop_back();
            }
        }
        if (!loaded->Load(&archive, sx, sy, mapName, this)) return nullptr;
        loaded->lastUsed = frame;
        sector = loaded.get();
        sectorCache.insert(sectorCache.begin(), std::move(loaded));
        cell = sector;
    } else if (sector->lastUsed < frame) {
        sector->lastUsed = frame;
        // To the front, the others keeping their order.
        const auto used = std::ranges::find(sectorCache, sector, &std::unique_ptr<SectorData>::get);
        std::rotate(sectorCache.begin(), used, used + 1);
    }
    if (create) sector->Ensure(wanted);
    return lib::Has(sector->built, wanted) ? sector : nullptr;
}

void Terrain::ShowAllSectors() {
    visible = SectorRect{0, 0, sectorsX - 1, sectorsY - 1};
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
    sectorCache.clear();
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
    // Identity rotations (w first).
    localRotation = Quaternion{1.0f, 0.0f, 0.0f, 0.0f};
    worldRotation = Quaternion{1.0f, 0.0f, 0.0f, 0.0f};
}

Terrain::~Terrain() { Terrain::Destroy(); }

void Terrain::Create() {
    Reset();
    BuildVertexTiles();
    BuildWavePhases();
    for (int i = 0; i < 512; ++i) sine[i] = lib::Sin(static_cast<float>(i) * kPi * kSineStep);
    localPosition = {0.0f, 0.0f, 0.0f};
    localRotation = {1.0f, 0.0f, 0.0f, 0.0f};
}

void Terrain::Destroy() {
    ClearMap();
    lights.clear();
}

void Terrain::Reset() {
    landscapeFlags = LandscapeFlag::None;
    mapName = "none";
    perPage = -1;
    frame = 1;
    tileSteps = 0;
    waveDirection = {0.0f, 0.0f, 1.0f};
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
        float radiusSquared = static_cast<float>(radius * radius);
        float* phase = phases;
        for (int row = 0; row < kSectorRow; ++row) {
            float dy = static_cast<float>(row) - centreY;
            for (int column = 0; column < kSectorRow; ++column, ++phase) {
                float dx = static_cast<float>(column) - centreX;
                float distanceSquared = LengthSquared2D(dx, dy);
                if (distanceSquared < radiusSquared) *phase += (1.0f - distanceSquared / radiusSquared) * kPi * 0.5f;
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
    const float corners = (phases[kLastRow] + phases[kSectorVertices - 1] + phases[kSectorRow - 1] + phases[0]) * 0.25f;
    phases[0] = corners;
    phases[kSectorRow - 1] = corners;
    phases[kSectorVertices - 1] = corners;
    phases[kLastRow] = corners;
}

void Terrain::SetWaveStrength(float strength) {
    strength = MaxKeepB(strength, 0.0f);
    strength = MinKeepA(1.0f, strength);
    waveGlow = strength;
    waveHeight = kWaveHeightScale * strength;
    waveSpeed = kWaveSpeedScale * strength;
    const float step = lib::Asin(1.0f - strength) + kPi * kWaveStepTurn;
    float length = Length(waveDirection);
    if (length > 0.0f) waveDirection *= 1.0f / length;
    waveDirection = WithZ(kWaveDirectionScale * XY(waveDirection) * step, waveDirection.z);
}

void Terrain::SetWaves(const Vector3& direction, float strength) {
    waveDirection = direction;
    SetWaveStrength(strength);
}

bool Terrain::SetupPages(int count) {
    TextureManager& textures = *g_clientWorld.textureManager;
    const auto pageName = [&](int i) {
        return i < count ? std::format("{}{:03}", mapName, i) : std::format("{}L{:02}", mapName, i - count);
    };
    // The pages made from PNGs (a converted install's) are made together first: one at a time they cost most of the
    // load (docs/notes/mods.md, "Textures as PNG", "Loading cost").
    std::vector<std::string> files;
    for (int i = 0; i < count + kExtraPages; ++i)
        if (!textures.IsLoaded(kPageTexture, pageName(i))) files.push_back(pageName(i) + ".mmp");
    textures.MakePngs(files);
    for (int i = 0; i < count + kExtraPages; ++i) {
        const std::string name = pageName(i);
        if (i >= count && !textures.HasFile(name + ".mmp")) return true;
        Texture* texture = pageTextures.emplace_back(textures.FindOrCreate(kPageTexture, name));
        if (!textures.LoadTexture(texture)) return false;
    }
    return true;
}

// --- The map loader ----------------------------------------------------------------------------

bool Terrain::LoadMap(std::string_view path, std::string_view zone, bool dungeon) {
    ClearMap();
    const std::string name(path);
    if (!archive.OpenResource(name)) return false;
    std::vector<std::string> files;
    archive.ListFiles(&files, "*.mp");
    // An archive without a map file fails as a missing map file (the original read past the empty list).
    std::uint32_t size = 0;
    const std::uint8_t* file = nullptr;
    if (!files.empty()) {
        mapName = str::Left(files[0], str::Find(files[0], '.'));
        file = static_cast<const std::uint8_t*>(archive.GetFile(mapName + kMapDataExtension, &size));
    }
    if (!file) game::FatalExit("Unable to open file \"" + name + "\"");
    const std::optional<MapHeader> header = ReadMapHeader(file, size);
    if (!header) game::FatalExit("Wrong file format: " + name);
    ReadMap(*header, file);
    if (!SetupPages(pages)) {
        ClearMap();
        return false;
    }
    BuildUvTables(header->tileSize);
    mapLoaded = true;

    ambientSet = AmbientSetOf(zone, dungeon);
    CountAmbientVariants();
    landscapeFlags |= LandscapeFlag::All;
    waterLevels.assign(static_cast<std::size_t>(sectorsY * sectorsX) * materials.size(), 0.0f);
    render::LandscapeLoaded();
    return true;
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
        MapMaterial source = lib::Load<MapMaterial>(cursor);
        TerrainMaterial& material = materials[i];
        material.kind = source.kind;
        material.waveColour = source.colour;
        material.absorption = 1.0f / (1.0f - source.opacity);
        // Truncated through 64 bits (a NaN opacity from the file gives 0, as the game CRT's _ftol).
        material.opacityByte = static_cast<std::uint8_t>(static_cast<std::int64_t>(source.opacity * kByteScale));
        material.colour.x = MinKeepA(1.0f, source.light * material.waveColour.x);
        material.colour.y = MinKeepA(1.0f, source.light * material.waveColour.y);
        material.colour.z = MinKeepA(1.0f, source.light * material.waveColour.z);
        material.priority = source.priority;
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
        MapAnimatedTexture entry = lib::Load<MapAnimatedTexture>(animated);
        textureFrames[entry.texture] = entry.frames;
    }
}

void Terrain::BuildUvTables(int tileSize) {
    // A tile's side as a part of the page, and 8 pixels of the page inside each of its edges.
    const float side = 1.0f / tilesPerRow;
    const float margin = (side / tileSize) * kMarginPixels;
    tileSide = side;
    uvLocal.clear();
    for (int rotation = 0; rotation < 4; ++rotation) uvLocal.push_back(BuildTileUv(rotation, 0, 0, side, margin, false));
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

void Terrain::AddLight(Light* light) {
    if (!mapLoaded) return;
    lights[light->id] = light;
}

void Terrain::RemoveLight(Light* light) {
    lights.erase(light->id);
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
    const float change = level - materials[material].level;
    materials[material].level = change + materials[material].level;
    for (std::size_t i = material; i < waterLevels.size(); i += materials.size()) waterLevels[i] = change + waterLevels[i];
}

} // namespace terrain
