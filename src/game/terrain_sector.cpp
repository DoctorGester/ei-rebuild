// Terrain sectors: loading, building, water levels and projection. See terrain_sector.h for the layout and
// the file format.
// Floating point: values kept on the FPU stack are `double` here; every store to a float is a
// static_cast<float>.
#include "terrain_sector.h"

#include "model.h"
#include "random.h"
#include "render/render.h"
#include "terrain.h"

#include "lib/bytes.h"
#include "lib/crt.h"
#include "lib/nan_audit.h"

#include "ei/x87.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace terrain {
namespace {

// The sector files' extension.
constexpr const char* kSectorExtension = /*00744164*/ ".SEC";

constexpr double kOffsetScale = 0.007936508394777775;    // 1/126 as a float: vertex displacement per unit
constexpr double kHeightUnit = 0.000015259254723787308;  // 1/65534 as a float
constexpr double kNormalScale = 0.0010000000474974513;   // 0.001 as a float
constexpr double kNormalBias = 1000.0;
// A packed normal: z in the top 10 bits, then x and y in 11 bits each, biased by kNormalBias.
constexpr int kNormalZShift = 22;
constexpr int kNormalXShift = 11;
constexpr std::uint32_t kNormalMask = 0x7FF;

constexpr std::uint32_t kSectorMagic = 0xCF4BF774;
constexpr int kTextureClasses = 16;
constexpr int kFallbackClass = 0xF;

// A tile's entries (3 x 3 vertices).
constexpr int kTileEntries = 9;

// Tile edge flags (SectorData::tileFlags): the tile on the sector's first / last row, first / last column.
constexpr std::uint8_t kFirstRow = 1;
constexpr std::uint8_t kLastRow = 2;
constexpr std::uint8_t kFirstColumn = 4;
constexpr std::uint8_t kLastColumn = 8;

// Where a tile texture is: its page and its index in the page.
struct PageSlot {
    int page, index;
};
PageSlot SlotOf(int texture, int perPage, int pages) {
    if (texture / perPage < pages) return {texture / perPage, texture % perPage};
    const int extra = texture - perPage * pages;
    return {extra / kExtraPageTextures + pages, extra % kExtraPageTextures};
}

// The quarter (8 x 8 tiles) a tile is in: 0, 1 on the first rows, 2, 3 after them.
int QuarterOf(int column, int row) { return column / (kSectorTiles / 2) + row / (kSectorTiles / 2) * 2; }

// The animation speed of a texture class.
constexpr int kAnimationSpeeds[kTextureClasses] = /*00796960*/ {1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000,
                                                   1000, 1000, 1000, 1000, 1000, 1000, 5,    9};

int ClassOf(const Terrain* owner, int texture) {
    int textureClass = owner->textureClass[texture];
    if (textureClass >= owner->perPage * owner->pages) textureClass = kFallbackClass;
    return textureClass;
}

bool IsAnimated(const Terrain* owner, int texture) {
    return texture < owner->perPage * owner->pages && owner->textureFrames[texture] != 0;
}

// The water material a water vertex's record keeps: the first among its tiles' (-1 none).
int FirstWaterMaterial(const SectorData& s, int vertex) {
    const VertexTiles& around = s.owner->vertexTiles[vertex];
    int material = s.waterTiles[around.tiles[0]];
    for (int i = 1; material == -1 && i < around.count; ++i) material = s.waterTiles[around.tiles[i]];
    return material;
}

// The water material that places a water vertex: the lowest priority among its tiles' (-1 none).
int LowestPriorityWaterMaterial(const SectorData& s, int vertex) {
    const std::vector<TerrainMaterial>& materials = s.owner->materials;
    const VertexTiles& around = s.owner->vertexTiles[vertex];
    int material = s.waterTiles[around.tiles[0]];
    for (int i = 1; i < around.count; ++i) {
        int other = s.waterTiles[around.tiles[i]];
        if (other == -1) continue;
        if (material != -1) EI_NAN_AUDIT(materials[other].priority, materials[material].priority);
        if (material == -1 || materials[other].priority < materials[material].priority) material = other;
    }
    return material;
}

// Vertex records: water material (water layer), normals, height range. Returns the tile words.
const std::uint8_t* BuildRecords(SectorData& s) {
    const int layers = s.hasWater ? 2 : 1;
    const std::uint8_t* source = s.data;
    s.minZ = 1e30f;
    s.maxZ = 0.0f;
    for (int i = 0; i < layers * kSectorVertices; ++i, source += kVertexBytes) {
        SectorVertex& record = s.records[i];
        const SectorPosition& vertex = s.vertices[i];
        // The minimum keeps its value when it is below z, the maximum when z is below it (both when z is NaN).
        float z = vertex.z;
        if (z <= s.minZ) s.minZ = z;
        if (z >= s.maxZ) s.maxZ = z;

        record.materialByte = 0xFF;
        record.materialColour = {0.0f, 0.0f, 0.0f};
        record.waterMaterial = SectorVertex::kNoWaterMaterial;
        if (i >= kSectorVertices) {
            const int ground = i - kSectorVertices;
            int material = FirstWaterMaterial(s, ground);
            record.waterMaterial = static_cast<std::uint16_t>(material);
            if (material != -1) {
                record.materialByte = s.owner->materials[material].opacityByte;
                record.materialColour = s.owner->materials[material].colour;
            }
            record.position = vertex;
            s.records[ground].depth = static_cast<float>(static_cast<double>(vertex.z) - s.vertices[ground].z);
        }
        std::uint32_t normal;
        lib::LoadBytes(normal, source + 4);
        record.normal.z = static_cast<float>(x87::FromUnsigned(normal >> kNormalZShift) * kNormalScale);
        record.normal.x = static_cast<float>(
            (static_cast<double>(static_cast<int>((normal >> kNormalXShift) & kNormalMask)) - kNormalBias) * kNormalScale);
        record.normal.y =
            static_cast<float>((static_cast<double>(static_cast<int>(normal & kNormalMask)) - kNormalBias) * kNormalScale);
    }
    return s.data + layers * kLayerBytes;
}

// Tiles per quarter and page (ground pages, then the water tiles' pages after them).
using TileCounts = int[4][kMaxPages];

// Counts the tiles per quarter and texture page, stores the tile textures (a water tile layer's -1 where the
// tile has no water) and the ground rotations and the texture class counts. Returns the number of animated
// tiles.
int CountTiles(SectorData& s, const std::uint8_t* words, TileCounts& counts) {
    const Terrain* owner = s.owner;
    int animated = 0;
    s.classCounts.assign(kTextureClasses, 0);
    s.tileTextures.resize(s.hasWaterTiles ? 2 * kLayerTiles : kLayerTiles);

    const std::uint8_t* source = words;
    int* out = s.tileTextures.data();
    for (int layer = 0; layer < (s.hasWaterTiles ? 2 : 1); ++layer) {
        for (int row = 0; row < kSectorTiles; ++row) {
            for (int column = 0; column < kSectorTiles; ++column, source += 2, ++out) {
                const int tile = row * kSectorTiles + column;
                if (layer == 1 && s.waterTiles[tile] == -1) {
                    *out = -1;
                    continue;
                }
                std::uint16_t word;
                lib::LoadBytes(word, source);
                if (layer == 0) s.tileRotation[tile] = static_cast<std::uint8_t>(word >> kTileRotationShift);
                int texture = word & kTileTextureMask;
                int page = SlotOf(texture, s.perPage, s.pages).page + layer * s.LayerPages();
                *out = texture;
                if (IsAnimated(owner, texture)) ++animated;
                ++counts[QuarterOf(column, row)][page];
                ++s.classCounts[ClassOf(owner, texture)];
            }
        }
    }
    return animated;
}

// Fills the tile lists: 9 entries per tile (3x3 vertices with texture coordinates), its edge
// flags, and the animated tiles (random start frame).
void FillTiles(SectorData& s, const std::uint8_t* words, TileCounts& counts) {
    Terrain* owner = s.owner;
    const std::uint8_t* source = words;
    int animatedIndex = 0;
    for (int layer = 0; layer < (s.hasWaterTiles ? 2 : 1); ++layer) {
        for (int row = 0; row < kSectorTiles; ++row) {
            for (int column = 0; column < kSectorTiles; ++column, source += 2) {
                if (layer == 1 && s.waterTiles[row * kSectorTiles + column] == -1) continue;
                std::uint16_t word;
                lib::LoadBytes(word, source);
                if (word == kNoTile) continue;
                int texture = word & kTileTextureMask;
                int rotation = word >> kTileRotationShift;
                PageSlot slot = SlotOf(texture, s.perPage, s.pages);
                int page = slot.page + layer * s.LayerPages();
                int quarter = QuarterOf(column, row);
                const float* uv = owner->TileUv(rotation, slot.index, page);

                int& count = counts[quarter][page];
                TileVertex* entry = s.tiles[quarter][page].data() + count * kTileEntries;
                SectorVertex* record = s.records.data() + layer * kSectorVertices + 2 * (column + kSectorRow * row);
                if (texture < owner->perPage * owner->pages) {
                    int frames = owner->textureFrames[texture];
                    if (frames != 0) {
                        AnimatedTile& tile = s.animated[animatedIndex++];
                        tile.frameBase = slot.index;
                        tile.vertices = entry;
                        tile.rotation = rotation;
                        std::uint32_t draw = g_clientRandom.Next();
                        tile.frames = frames;
                        tile.frame = static_cast<int>(draw % static_cast<std::uint32_t>(frames));
                        tile.speed = kAnimationSpeeds[ClassOf(owner, texture)];
                        uv = owner->TileUv(rotation, tile.frame + slot.index, 0);
                    }
                }
                for (int entryRow = 0; entryRow < 3; ++entryRow, record += kSectorRow) {
                    for (int entryColumn = 0; entryColumn < 3; ++entryColumn, ++entry) {
                        entry->u = uv[entryRow * 3 + entryColumn];
                        entry->vertex = record + entryColumn;
                        entry->v = static_cast<float>(1.0 - uv[entryRow * 3 + entryColumn + kTileEntries]);
                    }
                }
                std::uint8_t& flags = s.tileFlags[quarter][page][count];
                flags = row == 0 ? kFirstRow : (row == kSectorTiles - 1 ? kLastRow : 0);
                if (column == 0) flags |= kFirstColumn;
                else if (column == kSectorTiles - 1) flags |= kLastColumn;
                ++count;
            }
        }
    }
}

// Bounding boxes of the sector and of its four quarters.
void ComputeBounds(SectorData& s) {
    double size = kSectorCells;
    float x0 = static_cast<float>(s.sx * size);
    float y0 = static_cast<float>(s.sy * size);
    double half = size * 0.5;
    s.maxZ = static_cast<float>(1.0 + s.maxZ);
    double low = static_cast<double>(s.minZ) - 1.0;
    s.minZ = static_cast<float>(low);
    double cz = (static_cast<double>(s.maxZ) - low) * 0.5 + low;
    float centreZ = static_cast<float>(cz);
    s.box.center.x = static_cast<float>(half + x0);
    s.box.center.y = static_cast<float>(half + y0);
    s.box.center.z = centreZ;
    s.box.boxMin.x = static_cast<float>(-half);
    s.box.boxMin.y = static_cast<float>(-half);
    s.box.boxMin.z = static_cast<float>(s.minZ - cz);
    s.box.boxMax.x = static_cast<float>(half);
    s.box.boxMax.y = static_cast<float>(half);
    s.box.boxMax.z = static_cast<float>(s.maxZ - cz);
    s.box.flags |= BoundsFlag::Box;
    double quarterHalf = half * 0.5;
    SectorBox* box = s.quarters;
    for (int i = 0; i < 2; ++i) {
        float by = static_cast<float>((i + 0.5) * half + y0);
        for (int j = 0; j < 2; ++j, ++box) {
            box->center.x = static_cast<float>((j + 0.5) * half + x0);
            box->center.y = by;
            box->center.z = centreZ;
            box->boxMin.x = static_cast<float>(-quarterHalf);
            box->boxMin.y = static_cast<float>(-quarterHalf);
            box->boxMin.z = static_cast<float>(s.minZ - cz);
            box->boxMax.x = static_cast<float>(quarterHalf);
            box->boxMax.y = static_cast<float>(quarterHalf);
            box->boxMax.z = static_cast<float>(s.maxZ - cz);
            box->flags |= BoundsFlag::Box;
        }
    }
}

} // namespace

// Loading

int SectorData::Load(CMappedResource* archive, int sectorX, int sectorY, int pageCount, int texturesPerPage,
                     const char* prefix, Terrain* terrain) {
    Clear();
    owner = terrain;
    sx = sectorX;
    pages = pageCount;
    sy = sectorY;
    perPage = texturesPerPage;

    char name[0x100];
    std::snprintf(name, sizeof(name), "%s%3.3i%3.3i%s", prefix, sectorX, sectorY, kSectorExtension);
    std::uint32_t size = 0;
    const std::uint8_t* file = static_cast<const std::uint8_t*>(archive->GetFile(name, &size));
    data = file;

    // The message is passed to FatalError as its format string.
    auto fail = [&](const char* text) { return game::FatalError(text + std::string(name)); };
    if (!file) return fail("Unable to locate resource: ");
    if (size < kSectorHeaderBytes) return fail("Wrong format resource: ");

    std::uint32_t magic;
    lib::LoadBytes(magic, file);
    std::uint8_t flags = file[4];
    data = file + kSectorHeaderBytes;
    if (magic != kSectorMagic) return fail("Wrong format resource: ");

    hasWaterTiles = (flags & 3) != 0;
    hasWater = (flags & 1) != 0;
    const int layers = hasWater ? 2 : 1;
    const int tileLayers = hasWaterTiles ? 2 : 1;
    const int waterTileBytes = hasWater ? kTileLayerBytes : 0;
    const std::uint32_t expected = kSectorHeaderBytes + layers * kLayerBytes + tileLayers * kTileLayerBytes + waterTileBytes;
    if (size != expected) return fail("Wrong format resource: ");

    // Water material per tile.
    if (hasWater) {
        waterTiles.resize(kLayerTiles);
        const std::uint8_t* source = data + layers * kLayerBytes + tileLayers * kTileLayerBytes;
        for (int i = 0; i < kLayerTiles; ++i) {
            std::uint16_t material;
            lib::LoadBytes(material, source + i * 2);
            waterTiles[i] = material == 0xFFFF ? -1 : material;
        }
    } else {
        waterTiles.clear();
    }

    // Vertex positions: one or two layers of 33x33 (resized, zeroing new ones).
    vertices.resize(layers * kSectorVertices);

    // Each vertex is 8 bytes: x and y displacement (signed bytes), height (u16), packed normal.
    const std::uint8_t* source = data;
    SectorPosition* out = vertices.data();
    for (int layer = 0; layer < layers; ++layer) {
        double x0 = static_cast<double>(sx) * kSectorCells;
        double y = static_cast<double>(sy) * kSectorCells;
        int vertex = 0;
        for (int row = 0; row < kSectorRow; ++row) {
            double x = x0;
            for (int column = 0; column < kSectorRow; ++column, ++vertex, source += kVertexBytes, ++out) {
                std::uint16_t height;
                lib::LoadBytes(height, source + 2);
                out->z = static_cast<float>(static_cast<double>(height) * owner->heightRange * kHeightUnit);
                if (layer == 0) {
                    out->x = static_cast<float>(0.5 * static_cast<std::int8_t>(source[0]) * kOffsetScale + x);
                    out->y = static_cast<float>(0.5 * static_cast<std::int8_t>(source[1]) * kOffsetScale + y);
                } else {
                    int material = LowestPriorityWaterMaterial(*this, vertex);
                    if (material == -1) {
                        out->x = static_cast<float>(x);
                        out->z = 0.0f;
                        out->y = static_cast<float>(y);
                    } else if (owner->materials[material].kind != TerrainMaterial::kKeepsGroundXY) {
                        out->x = static_cast<float>(x);
                        out->y = static_cast<float>(y);
                    } else {
                        out->x = vertices[vertex].x;
                        out->y = vertices[vertex].y;
                    }
                }
                x += 1.0;
            }
            y += 1.0;
        }
    }
    built |= SectorStage::Loaded;
    return 1;
}

// Building

void SectorData::Build() {
    records.resize(hasWater ? 2 * kSectorVertices : kSectorVertices);
    const std::uint8_t* tileWords = BuildRecords(*this);

    const int pageCount = 2 * LayerPages();
    TileCounts counts = {};
    int animatedCount = CountTiles(*this, tileWords, counts);
    for (int quarter = 0; quarter < 4; ++quarter) {
        tileFlags[quarter].resize(pageCount);
        tiles[quarter].resize(pageCount);
        for (int page = 0; page < pageCount; ++page) {
            int count = counts[quarter][page];
            tileFlags[quarter][page].resize(count);
            tiles[quarter][page].resize(count * kTileEntries);
        }
    }
    animated.resize(animatedCount);

    TileCounts filled = {};
    FillTiles(*this, tileWords, filled);
    ComputeBounds(*this);
    built |= SectorStage::Built;

    if (hasWater) {
        // The water materials present in the sector, then their levels.
        for (int i = 0; i < kSectorVertices; ++i) {
            unsigned id = records[kSectorVertices + i].waterMaterial;
            if (id == SectorVertex::kNoWaterMaterial) continue;
            if (std::find(waterMaterials.begin(), waterMaterials.end(), id) == waterMaterials.end())
                waterMaterials.push_back(static_cast<std::uint8_t>(id));
        }
        std::vector<float>& levels = owner->waterLevels[owner->sectorsX * sy + sx];
        for (std::uint8_t id : waterMaterials) levels[id] = owner->materials[id].level;
        ApplyWaterLevels();
    }
    {
        const x87::ScopedRounding chop(x87::kRoundChop);
        for (int i = 0; i < std::ssize(records); ++i) {
            if (i >= kSectorVertices && records[i].waterMaterial == SectorVertex::kNoWaterMaterial) continue;
            records[i].specular = PackArgb(owner->EdgeFade(vertices[i]), 0, 0, 0);
        }
    }
    render::TerrainChanged(this, render::TerrainChange::Rebuilt);   // the pages kept on the GPU
}

void SectorData::ApplyWaterLevels() {
    std::vector<float>& levels = owner->waterLevels[owner->sectorsX * sy + sx];
    for (std::uint8_t id : waterMaterials) {
        const double level = levels[id];
        EI_NAN_AUDIT(level);
        if (level == 0.0) continue;
        for (int i = kSectorVertices; i < 2 * kSectorVertices; ++i) {
            SectorVertex& record = records[i];
            if (record.waterMaterial != id) continue;
            record.position.z = static_cast<float>(level + record.position.z);
            vertices[i].z = static_cast<float>(level + vertices[i].z);
        }
        levels[id] = 0.0f;
        render::TerrainChanged(this, render::TerrainChange::WaterMoved);
    }
}

void SectorData::Ensure(SectorStage wanted) {
    if (lib::Has(wanted, built)) return;
    if (wanted >= SectorStage::Built && !lib::Has(built, SectorStage::Built)) Build();
}

SectorData::~SectorData() { Clear(); }

} // namespace terrain
