// The landscape object's own members (Terrain, terrain.h) and the aligned-array instantiations next to
// them. Sector loading and building are in terrain_sector.cpp, height queries in terrain.cpp.
#include "terrain.h"
#include "terrain_sector.h"
#include "console.h"
#include "material.h"
#include "sound.h"
#include "spell.h"
#include "texture.h"

#include "ei/x87.h"
#include "lib/crt.h"
#include "game_singletons.h"
#include "lib/str.h"
#include "lib/text_case.h"

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

using terrain::SectorData;

namespace {

// A sector bound: FISTP of the value stored as a float (stored, then loaded back), divided by 32 towards
// zero, clamped to 0..count - 1 in this order (a map without sectors gives -1).
int SectorBound(float value, int count) {
    int sector = x87::fistp(value) / 32;
    if (sector < 0) return 0;
    if (sector > count - 1) return count - 1;
    return sector;
}

// How many frames an unused sector stays cached before a load may reuse or free it.
constexpr int kSectorAge = 240;

constexpr int kSectorTiles = 16;          // tiles per sector row (2 x 2 cells each)

// Appends tile (x, y) of the sector when it exists.
void AddTile(terrain::VertexTiles& entry, int x, int y) {
    if (x >= 0 && y >= 0 && x < kSectorTiles && y < kSectorTiles) entry.tiles[entry.count++] = y * kSectorTiles + x;
}

constexpr int kRow = 33;                  // vertices per sector row
constexpr int kVertices = kRow * kRow;
constexpr float kPi = 3.14159274f;
constexpr double kPiDouble = 3.141592653589793;
constexpr double kSineStep = 1.0 / 256;
constexpr float kWaveStepTurn = 0.500250161f;
constexpr int kRipples = 40;

// Static initialisers: the zero position and identity rotation Create gives the landscape, and the waves'
// scales (computed from other constants at startup, read from memory).
constexpr Vector3 kZeroPosition = /*007C8988*/ {0.0f, 0.0f, 0.0f};
constexpr Quaternion kIdentityRotation = /*007C8978*/ {1.0f, 0.0f, 0.0f, 0.0f};
constexpr float kWaveHeightScale = /*007C8970*/ 0.3f;
constexpr float kWaveDirectionScale = /*007C896C*/ 0.44879898f;   // pi / 7
constexpr float kWaveSpeedScale = /*007C8968*/ -0.14959967f;      // that * -1/3

constexpr int kPageTexture = 0xB;     // texture kind of the terrain pages
constexpr int kPageMaterial = 0x12;   // material kind
constexpr int kExtraPages = 2;        // <map>L00, <map>L01

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
constexpr std::uint32_t kMaterialDwords = sizeof(MapMaterial) / 4;
constexpr int kSectorCells = 32;
constexpr int kExtraTextures = 16;      // the two extra pages' textures, after the map's own
constexpr int kExtraClass = 0xF;
constexpr int kUvFloats = 18;           // per texture: 9 u, 9 v
constexpr float kMarginPixels = 8.0f;
constexpr double kByteScale = 255.0;
constexpr std::uint32_t kLowFlags = 0xFFFF;

// The map file's extension.
constexpr const char* kMapDataExtension = /*007441D4*/ ".MP";

// 1 is kept when FCOMP finds 1 below the value, or the two unordered.
float CappedAtOne(float value) { return value <= 1.0f ? value : 1.0f; }

} // namespace

SectorData* Terrain::SectorAt(int sx, int sy, std::uint32_t wanted, int create) {
    if (sx < 0 || sy < 0 || sx >= sectorsX || sy >= sectorsY) return nullptr;
    std::list<terrain::SectorData*>::iterator node = sectorGrid[sx][sy];
    if (node != std::list<terrain::SectorData*>::iterator{}) {
        SectorData* sector = *node;
        if (sector->lastUsed < frame) {
            sector->lastUsed = frame;
            sectorCache.erase(node);
            sectorCache.push_front(sector);
            sectorGrid[sx][sy] = sectorCache.begin();
        }
        if (create) sector->Ensure(wanted, this);
        return (sector->built & wanted) ? sector : nullptr;
    }
    if (!create) return nullptr;

    // A new sector, or the least recently used one when it is old enough; then every further
    // sector at the cache's end that is old enough is freed.
    SectorData* sector;
    SectorData* last = !sectorCache.empty() ? sectorCache.back() : nullptr;
    if (!last || frame - kSectorAge <= last->lastUsed) {
        sector = new SectorData();
    } else {
        sector = last;
        sectorGrid[sector->sx][sector->sy] = {};
        sectorCache.pop_back();
        while (!sectorCache.empty() && sectorCache.back()->lastUsed < frame - kSectorAge) {
            SectorData* old = sectorCache.back();
            sectorCache.pop_back();
            sectorGrid[old->sx][old->sy] = {};
            delete old;
        }
    }
    if (sector->Load(&archive, sx, sy, pages, perPage, textF8.c_str(), this)) {
        sector->lastUsed = frame;
        sectorCache.push_front(sector);
        sectorGrid[sx][sy] = sectorCache.begin();
        sector->Ensure(wanted, this);
        return (sector->built & wanted) ? sector : nullptr;
    }
    delete sector;
    return nullptr;
}

void Terrain::SetVisibleSectors(float x, float y, float radius) {
    int columnFirst = SectorBound(x - radius - 0.5f, sectorsX);
    int columnLast = SectorBound(x + radius - 0.5f, sectorsX);
    int rowFirst = SectorBound(y - radius - 0.5f, sectorsY);
    int rowLast = SectorBound(y + radius - 0.5f, sectorsY);
    int rows = rowLast - rowFirst + 1;
    if (rows < 0) rows = 0;
    visibleColumnFirst.resize(rows);
    visibleColumnLast.resize(rows);
    for (int row = 0; row < rows; ++row) {
        visibleColumnFirst[row] = columnFirst;
        visibleColumnLast[row] = columnLast;
    }
    visibleRowFirst = rowFirst;
}

// ---- Create, Destroy, Reset and the waves --------------------------------------------------------

void Terrain::BuildVertexTiles() {
    constexpr int kSide = 33;   // vertices per sector row
    vertexTiles.resize(kSide * kSide);
    // One stack record per vertex is filled without clearing it: the tiles after `count` keep earlier
    // vertices' values (here they start from zero).
    terrain::VertexTiles entry{};
    terrain::VertexTiles* out = vertexTiles.data();
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

void Terrain::ClearMap() {
    for (int slot = 0; slot < 3; ++slot) {
        ambientSounds[slot] = TheSoundManager()->StopSound(ambientSounds[slot]);
        ambientClasses[slot] = -1;
    }
    while (!sectorCache.empty()) {
        SectorData* sector = sectorCache.front();
        sectorCache.pop_front();
        delete sector;
    }
    for (int i = 0; i < std::ssize(pageTextures); ++i) pageTextures[i]->Release();
    pageTextures.clear();
    for (int i = 0; i < std::ssize(pageMaterials); ++i) pageMaterials[i]->Release();
    pageMaterials.clear();
    archive.CloseResource();
    if (sectorGrid) {
        for (int i = 0; i < sectorsX; ++i) delete[] sectorGrid[i];
        delete[] sectorGrid;
        sectorGrid = nullptr;
    }
    sectorsX = -1;
    sectorsY = -1;
    mapLoaded = 0;
    for (float*& table : uvTables) {
        if (table) delete[] table;
        table = nullptr;
    }
    for (float*& table : uvTablesLast) {
        if (table) delete[] table;
        table = nullptr;
    }
}

Terrain::Terrain() {
    ClearFields();
    // Identity rotations (w first); the positions are not initialised.
    localRotation_ = Quaternion{1.0f, 0.0f, 0.0f, 0.0f};
    worldRotation_ = Quaternion{1.0f, 0.0f, 0.0f, 0.0f};
    // archive: a C++ member, constructed before this body (CMMIOResource())
    maxLightId = 0;
    Reset();
}

// The members last to first (the texts, members, go after the others).
Terrain::~Terrain() {
    Terrain::Destroy();
    sectorCache.clear();
    // archive: destroyed after this body (~CMMIOResource())
}

int Terrain::Create() {
    Reset();
    BuildVertexTiles();
    BuildWavePhases();
    for (int i = 0; i < 512; ++i) sine[i] = static_cast<float>(x87::fsin(i * kPiDouble * kSineStep));
    localPosition_ = kZeroPosition;
    localRotation_ = kIdentityRotation;
    return 1;
}

void Terrain::Destroy() {
    ClearMap();
    for (auto& [id, entry] : lights) entry.light->Release();
    maxLightId = 0;
    lights.clear();
    sectorCache.clear();
    Reset();
}

void Terrain::Reset() {
    extraPages = 0;
    landscapeFlags = 0;
    pageTextures.clear();
    pageMaterials.clear();
    std::memset(&worldMatrix, 0, sizeof(worldMatrix));
    mapLoaded = 0;
    textF8 = "none";
    sectorsX = -1;
    sectorsY = -1;
    pageSize = -1;
    tileSize = -1;
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
    visibleColumnFirst.clear();
    visibleColumnLast.clear();
    for (int i = 0; i < 3; ++i) {
        ambientSounds[i] = -1;
        ambientClasses[i] = -1;
    }
    textAE0 = "none";
}

void Terrain::BuildWavePhases() {
    wavePhases.resize(kVertices);
    float* phases = wavePhases.data();
    std::memset(phases, 0, kVertices * sizeof(float));
    for (int ripple = 0; ripple < kRipples; ++ripple) {
        float centreX = static_cast<float>(libcrt::Rand() % kRow);
        float centreY = static_cast<float>(libcrt::Rand() % kRow);
        int radius = libcrt::Rand() % 5 + 1;
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
    pageMaterials.resize(count);
    for (int i = 0; i < count + kExtraPages; ++i) {
        std::string name;
        if (i < count) {
            name = str::Format("%s%3.3i", textF8.c_str(), i);
        } else {
            name = str::Format("%sL%2.2i", textF8.c_str(), i - count);
            if (!TheTextureManager()->HasFile((name + ".mmp"))) return 1;
            ++extraPages;
            pageTextures.push_back(nullptr);
            pageMaterials.push_back(nullptr);
        }
        CTexture* texture = TheTextureManager()->FindOrCreate(kPageTexture, name.c_str());
        pageTextures[i] = texture;
        ++texture->refCount_;
        CMaterial* material = TheMaterialManager()->NewMaterial(kPageMaterial);
        pageMaterials[i] = material;
        ++material->refCount_;
        material->name = "Wood";
        material->colors.diffuse.x = material->colors.diffuse.y = material->colors.diffuse.z = 0.3f;
        material->colors.ambient.x = material->colors.ambient.y = material->colors.ambient.z = 0.7f;
        material->colors.specular.x = material->colors.specular.y = material->colors.specular.z = 0.2f;
        material->colors.emissive.x = material->colors.emissive.y = material->colors.emissive.z = 0.0f;
        material->colors.diffuse.w = 1.0f;
        material->colors.power = 5.0f;
        if (!TheTextureManager()->LoadTexture(pageTextures[i])) return 0;
    }
    return 1;
}

// --- The map loader ----------------------------------------------------------------------------

// Float notes (rule 7): the game works on the FPU stack at the main thread's 24-bit precision, so every
// quotient and product below is rounded to float where it is made.
int Terrain::LoadMap(const char* path, const char* zone, char dungeon) {
    ClearMap();
    std::string name = path;
    if (!archive.OpenResource(name, 0)) return 0;
    std::vector<std::string> files;
    archive.ListFiles(&files, "*.mp");
    int dot = str::ReverseFind(name, '.');
    if (dot > 0) textFC = str::Left(name, dot);
    else textFC = name;
    // (An archive without a map file is not tested for either.)
    const std::string& first = files[0];
    textF8 = str::Left(first, str::Find(first, '.'));

    // The messages are passed to FatalError as its format string.
    std::uint32_t size = 0;
    const auto* file = static_cast<const std::uint8_t*>(archive.GetFile((textF8 + kMapDataExtension), &size));
    if (!file) {
        ClearMap();
        return game::FatalError(("Unable to open file \"" + name + "\"").c_str());
    }
    if (size < sizeof(MapHeader)) {
        ClearMap();
        return game::FatalError(("Wrong file format: " + name).c_str());
    }
    MapHeader header;
    std::memcpy(&header, file, sizeof(header));
    const std::uint32_t dwords =
        header.animated + header.materials * kMaterialDwords + static_cast<std::uint32_t>(header.textures);
    if (header.magic != kMapMagic || size != dwords * 4 + sizeof(MapHeader)) {
        ClearMap();
        return game::FatalError(("Wrong file format: " + name).c_str());
    }

    if (sectorGrid) {
        for (int i = 0; i < sectorsX; ++i) delete[] sectorGrid[i];
        delete[] sectorGrid;
        sectorGrid = nullptr;
    }
    sectorsX = header.sectorsX;
    sectorsY = header.sectorsY;
    sectorGrid = new std::list<terrain::SectorData*>::iterator*[static_cast<std::size_t>(sectorsX)];
    for (int i = 0; i < sectorsX; ++i) sectorGrid[i] = new std::list<terrain::SectorData*>::iterator[static_cast<std::size_t>(sectorsY)]();
    width = static_cast<float>(sectorsX * kSectorCells);
    height = static_cast<float>(sectorsY * kSectorCells);
    heightRange = header.heightRange;
    pageSize = header.pageSize;
    pages = header.pages;
    tileSize = header.tileSize;
    tilesPerRow = header.pageSize / header.tileSize;
    perPage = tilesPerRow * tilesPerRow;

    const std::uint8_t* cursor = file + sizeof(MapHeader);
    const int materialCount = header.materials;
    materials.resize(materialCount);
    for (int i = 0; i < materialCount; ++i, cursor += sizeof(MapMaterial)) {
        MapMaterial source;
        std::memcpy(&source, cursor, sizeof(source));
        terrain::TerrainMaterial& material = materials[i];
        material.kind = source.kind;
        material.waveColour = source.colour;
        material.opacity = source.opacity;
        material.absorption = static_cast<float>(1.0 / static_cast<float>(1.0 - source.opacity));
        material.opacityByte = static_cast<std::uint8_t>(libcrt::Ftol(static_cast<float>(material.opacity * kByteScale)));
        material.colour.x = CappedAtOne(static_cast<float>(static_cast<double>(source.light) * material.waveColour.x));
        material.colour.y = CappedAtOne(static_cast<float>(static_cast<double>(material.waveColour.y) * source.light));
        material.colour.z = CappedAtOne(static_cast<float>(static_cast<double>(source.light) * material.waveColour.z));
        material.priority = source.priority;
        material.warpSpeed = source.warpSpeed;
        material.level = 0.0f;
    }

    const int textures = header.textures;
    const std::size_t classBytes = static_cast<std::size_t>(textures * 4);
    textureClass.resize(textures + kExtraTextures);
    std::memcpy(textureClass.data(), cursor, classBytes);
    for (int i = 0; i < kExtraTextures; ++i) textureClass[textures + i] = kExtraClass;
    textureFrames.resize(textures);
    std::memset(textureFrames.data(), 0, classBytes);
    const std::uint8_t* animated = cursor + classBytes;
    for (int i = 0; i < static_cast<int>(header.animated); ++i, animated += sizeof(MapAnimatedTexture)) {
        MapAnimatedTexture entry;
        std::memcpy(&entry, animated, sizeof(entry));
        textureFrames[entry.texture] = entry.frames;
    }

    if (!SetupPages(pages)) {
        ClearMap();
        return 0;
    }

    // A tile's side as a part of the page, and 8 pixels of the page inside each of its edges.
    const float tileSide = static_cast<float>(1.0 / tilesPerRow);
    const float margin = static_cast<float>(static_cast<float>(static_cast<double>(tileSide) / tileSize) * kMarginPixels);
    const std::size_t tableBytes = static_cast<std::size_t>(perPage * kUvFloats * static_cast<int>(sizeof(float)));
    for (int rotation = 0; rotation < 4; ++rotation) {
        uvTables[rotation] = new float[tableBytes / sizeof(float)];
        int index = 0;
        for (int row = 0; row < tilesPerRow; ++row)
            for (int column = 0; column < tilesPerRow; ++column, ++index)
                BuildTileUv(uvTables[rotation] + index * kUvFloats, rotation, column, row, tileSide, margin, 0);
    }
    for (int rotation = 0; rotation < 4; ++rotation) {
        uvTablesLast[rotation] = new float[tableBytes / sizeof(float)];
        for (int column = 0; column < tilesPerRow; ++column)
            BuildTileUv(uvTablesLast[rotation] + column * kUvFloats, rotation, column, 0, tileSide, margin, 1);
    }
    mapLoaded = 1;

    const char* set = "Gipat";
    if (dungeon) set = "Dungeon";
    else if (text::CompareIgnoringCase(zone, "Ingos") == 0) set = "Ingos";
    else if (text::CompareIgnoringCase(zone, "Suslanger") == 0) set = "Suslanger";
    textAE0 = set;
    terrain::CountAmbientVariants(textAE0.c_str());

    const int sectors = sectorsY * sectorsX;
    landscapeFlags |= kLowFlags;
    waterLevels.resize(sectors);
    for (int i = 0; i < sectorsY * sectorsX; ++i) {
        waterLevels[i].assign(materials.size(), 0.0f);
    }
    return 1;
}

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

void Terrain::SetWaterLevel(int material, float level) {
    if (material < 0 || material >= std::ssize(materials)) return;
    float change = static_cast<float>(level - materials[material].level);
    materials[material].level = change + materials[material].level;
    for (int sector = 0; sector < sectorsY * sectorsX; ++sector) {
        float& sectorLevel = waterLevels[sector][material];
        sectorLevel = change + sectorLevel;
    }
}

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
