// Terrain sectors: loading, building, water levels and projection. See terrain_sector.h for the layout and
// the file format.
#include "game/terrain/terrain_sector.h"

#include "game/app/singletons.h"
#include "engine/scene/scene_renderer.h"
#include "engine/scene/pipeline.h"
#include "render/render.h"
#include "game/terrain/terrain.h"

#include "lib/bytes.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/float_select.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <string>
#include <vector>

namespace terrain {
namespace {

// The sector files' extension.
constexpr const char* kSectorExtension = ".SEC";

constexpr float kOffsetScale = 1.0f / 126;     // vertex displacement per unit
constexpr float kHeightUnit = 1.0f / 65534;
constexpr float kNormalScale = 0.001f;
constexpr float kNormalBias = 1000.0f;
// A packed normal: z in the top 10 bits, then x and y in 11 bits each, biased by kNormalBias.
constexpr int kNormalZShift = 22;
constexpr int kNormalXShift = 11;
constexpr std::uint32_t kNormalMask = 0x7FF;

constexpr std::uint32_t kSectorMagic = 0xCF4BF774;
// The sector file's flag byte: a water layer of vertices (with a water material per tile), a second tile layer
// (the water tiles; a water layer has one too).
constexpr std::uint8_t kFlagWater = 1;
constexpr std::uint8_t kFlagWaterTiles = 2;
constexpr int kTextureClasses = 16;
constexpr int kFallbackClass = 0xF;

// A tile's entries (3 x 3 vertices).
constexpr int kTileEntries = TileUv::kVertices;

// Where a tile texture is: its page and its index in the page.
struct PageSlot {
    int page = 0, index = 0;
};
PageSlot SlotOf(int texture, int perPage, int pages) {
    if (texture / perPage < pages) return {texture / perPage, texture % perPage};
    const int extra = texture - perPage * pages;
    return {extra / kExtraPageTextures + pages, extra % kExtraPageTextures};
}

// The quarter (8 x 8 tiles) a tile is in: 0, 1 on the first rows, 2, 3 after them.
int QuarterOf(int column, int row) { return column / (kSectorTiles / 2) + row / (kSectorTiles / 2) * 2; }

// The animation speed of a texture class.
constexpr int kAnimationSpeeds[kTextureClasses] = {1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000,
                                                   1000, 1000, 1000, 1000, 1000, 1000, 5,    9};

// The texture's class; kFallbackClass for a class outside the 16 (LostInAstral's maps have class 16: the original
// counted it and read its animation speed past its tables) or not below the page textures' count (the original's
// test).
int ClassOf(const Terrain* owner, int texture) {
    const int textureClass = owner->textureClass[texture];
    if (textureClass < 0 || textureClass >= kTextureClasses || textureClass >= owner->perPage * owner->pages)
        return kFallbackClass;
    return textureClass;
}

// The texture of a tile word, -1 for none: kNoTile, or a texture past the pages or the class table (no map has
// one; the original counted such a word into its tables past their end).
int TileTexture(const Terrain* owner, std::uint16_t word) {
    const int texture = word & kTileTextureMask;
    const int textures = std::min(owner->perPage * owner->pages + kExtraPages * kExtraPageTextures,
                                  static_cast<int>(owner->textureClass.size()));
    return word == kNoTile || texture >= textures ? -1 : texture;
}

bool IsAnimated(const Terrain* owner, int texture) {
    return texture < owner->perPage * owner->pages && texture < static_cast<int>(owner->textureFrames.size()) &&
           owner->textureFrames[texture] != 0;
}

// The water material a water vertex's record keeps: the first among its tiles' (kNoMaterial none).
int FirstWaterMaterial(const SectorData& s, int vertex) {
    const VertexTiles& around = s.owner->vertexTiles[vertex];
    int material = s.waterTiles[around.tiles[0]];
    for (int i = 1; material == kNoMaterial && i < around.count; ++i) material = s.waterTiles[around.tiles[i]];
    return material;
}

// The water material that places a water vertex: the lowest priority among its tiles' (kNoMaterial none).
int LowestPriorityWaterMaterial(const SectorData& s, int vertex) {
    const std::vector<TerrainMaterial>& materials = s.owner->materials;
    const VertexTiles& around = s.owner->vertexTiles[vertex];
    int material = s.waterTiles[around.tiles[0]];
    for (int i = 1; i < around.count; ++i) {
        int other = s.waterTiles[around.tiles[i]];
        if (other == kNoMaterial) continue;
        if (material == kNoMaterial || materials[other].priority < materials[material].priority) material = other;
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
        s.minZ = MinKeepA(s.minZ, z);
        s.maxZ = MaxKeepB(z, s.maxZ);

        record.materialByte = 0xFF;
        record.materialColour = {0.0f, 0.0f, 0.0f};
        record.waterMaterial = SectorVertex::kNoWaterMaterial;
        if (i >= kSectorVertices) {
            const int ground = i - kSectorVertices;
            int material = FirstWaterMaterial(s, ground);
            record.waterMaterial = static_cast<std::uint16_t>(material);
            if (material != kNoMaterial) {
                record.materialByte = s.owner->materials[material].opacityByte;
                record.materialColour = s.owner->materials[material].colour;
            }
            s.records[ground].depth = vertex.z - s.vertices[ground].z;
        }
        std::uint32_t normal = lib::Load<std::uint32_t>(source + 4);
        record.normal.z = static_cast<float>(normal >> kNormalZShift) * kNormalScale;
        record.normal.x = (static_cast<float>((normal >> kNormalXShift) & kNormalMask) - kNormalBias) * kNormalScale;
        record.normal.y = (static_cast<float>(normal & kNormalMask) - kNormalBias) * kNormalScale;
    }
    return s.data + layers * kLayerBytes;
}

// A tile word taken apart: its texture, quarter turns, the texture's slot, its page (a water tile's after the
// ground pages) and the quarter of the sector the tile at (column, row) is in.
struct DecodedTile {
    int texture = 0, rotation = 0;
    PageSlot slot;
    int page = 0, quarter = 0;
};
// A tile word decoded; texture -1 (TileTexture) for a tile without one, whose other fields are then unused.
DecodedTile DecodeTile(const Terrain* owner, std::uint16_t word, int layer, int column, int row) {
    DecodedTile tile;
    tile.texture = TileTexture(owner, word);
    tile.rotation = word >> kTileRotationShift;
    if (tile.texture < 0) return tile;
    tile.slot = SlotOf(tile.texture, owner->perPage, owner->pages);
    tile.page = tile.slot.page + layer * owner->LayerPages();
    tile.quarter = QuarterOf(column, row);
    return tile;
}

// Tiles per quarter and page (ground pages, then the water tiles' pages after them).
using TileCounts = int[4][kMaxPages];

// Counts the tiles per quarter and texture page, stores the tile textures (-1 where a tile has none: no water,
// or a word without a texture) and the ground rotations and the texture class counts. Returns the number of
// animated tiles.
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
                if (layer == 1 && s.waterTiles[tile] == kNoMaterial) {
                    *out = -1;
                    continue;
                }
                const DecodedTile decoded = DecodeTile(owner, lib::Load<std::uint16_t>(source), layer, column, row);
                if (layer == 0) s.tileRotation[tile] = static_cast<std::uint8_t>(decoded.rotation);
                *out = decoded.texture;
                if (decoded.texture < 0) continue;
                if (IsAnimated(owner, decoded.texture)) ++animated;
                ++counts[decoded.quarter][decoded.page];
                ++s.classCounts[ClassOf(owner, decoded.texture)];
            }
        }
    }
    return animated;
}

// Fills the tile lists: 9 entries per tile (3x3 vertices with texture coordinates) and the animated tiles (the entries
// hold the first frame's coordinates; the GPU works the frame out).
void FillTiles(SectorData& s, const std::uint8_t* words, TileCounts& counts) {
    Terrain* owner = s.owner;
    const std::uint8_t* source = words;
    int animatedIndex = 0;
    for (int layer = 0; layer < (s.hasWaterTiles ? 2 : 1); ++layer) {
        for (int row = 0; row < kSectorTiles; ++row) {
            for (int column = 0; column < kSectorTiles; ++column, source += 2) {
                if (layer == 1 && s.waterTiles[row * kSectorTiles + column] == kNoMaterial) continue;
                std::uint16_t word = lib::Load<std::uint16_t>(source);
                const DecodedTile decoded = DecodeTile(owner, word, layer, column, row);
                if (decoded.texture < 0) continue;
                const int texture = decoded.texture, rotation = decoded.rotation, page = decoded.page;
                const PageSlot slot = decoded.slot;
                const int quarter = decoded.quarter;
                const TileUv* uv = &owner->TileUvAt(rotation, slot.index, page);

                int& count = counts[quarter][page];
                TileVertex* entry = s.tiles[quarter][page].data() + count * kTileEntries;
                SectorVertex* record = s.records.data() + layer * kSectorVertices + 2 * (column + kSectorRow * row);
                if (IsAnimated(owner, texture)) {
                    const int frames = owner->textureFrames[texture];
                    AnimatedTile& tile = s.animated[animatedIndex++];
                    tile.frameBase = slot.index;
                    tile.vertices = entry;
                    tile.rotation = rotation;
                    tile.frames = frames;
                    tile.speed = kAnimationSpeeds[ClassOf(owner, texture)];
                    const int tilesX = owner->sectorsX * kSectorTiles, tilesY = owner->sectorsY * kSectorTiles;
                    tile.seed = (layer * tilesY + s.sy * kSectorTiles + row) * tilesX + s.sx * kSectorTiles + column;
                    uv = &owner->TileUvAt(rotation, slot.index, 0);
                }
                for (int entryRow = 0; entryRow < 3; ++entryRow, record += kSectorRow) {
                    for (int entryColumn = 0; entryColumn < 3; ++entryColumn, ++entry) {
                        entry->u = uv->u[entryRow * 3 + entryColumn];
                        entry->vertex = record + entryColumn;
                        entry->v = 1.0f - uv->v[entryRow * 3 + entryColumn];
                    }
                }
                ++count;
            }
        }
    }
}

// Bounding boxes of the sector and of its four quarters.
void ComputeBounds(SectorData& s) {
    constexpr float half = kSectorCells * 0.5f;
    constexpr float quarterHalf = half * 0.5f;
    const float x0 = static_cast<float>(s.sx * kSectorCells);
    const float y0 = static_cast<float>(s.sy * kSectorCells);
    s.maxZ = 1.0f + s.maxZ;
    s.minZ -= 1.0f;
    const float centreZ = (s.maxZ - s.minZ) * 0.5f + s.minZ;
    s.box.center = {half + x0, half + y0, centreZ};
    s.box.boxMin = {-half, -half, s.minZ - centreZ};
    s.box.boxMax = {half, half, s.maxZ - centreZ};
    s.box.flags |= BoundsFlag::Box;
    SectorBox* box = s.quarters;
    for (int i = 0; i < 2; ++i) {
        const float by = (i + 0.5f) * half + y0;
        for (int j = 0; j < 2; ++j, ++box) {
            box->center = {(j + 0.5f) * half + x0, by, centreZ};
            box->boxMin = {-quarterHalf, -quarterHalf, s.minZ - centreZ};
            box->boxMax = {quarterHalf, quarterHalf, s.maxZ - centreZ};
            box->flags |= BoundsFlag::Box;
        }
    }
}

} // namespace

// Loading

bool SectorData::Load(MappedResource* archive, int sectorX, int sectorY, std::string_view prefix, Terrain* terrain) {
    Clear();
    owner = terrain;
    sx = sectorX;
    sy = sectorY;

    const std::string name = std::format("{}{:03}{:03}{}", prefix, sectorX, sectorY, kSectorExtension);
    std::uint32_t size = 0;
    const std::uint8_t* file = static_cast<const std::uint8_t*>(archive->GetFile(name, &size));
    data = file;

    const std::string wrongFormat = "Wrong format resource: " + name;
    if (!file) game::FatalExit("Unable to locate resource: " + name);
    if (size < kSectorHeaderBytes) game::FatalExit(wrongFormat);

    std::uint32_t magic = lib::Load<std::uint32_t>(file);
    std::uint8_t flags = file[4];
    data = file + kSectorHeaderBytes;
    if (magic != kSectorMagic) game::FatalExit(wrongFormat);

    hasWaterTiles = (flags & (kFlagWater | kFlagWaterTiles)) != 0;
    hasWater = (flags & kFlagWater) != 0;
    const int layers = hasWater ? 2 : 1;
    const int tileLayers = hasWaterTiles ? 2 : 1;
    const int waterTileBytes = hasWater ? kTileLayerBytes : 0;
    const std::uint32_t expected = kSectorHeaderBytes + layers * kLayerBytes + tileLayers * kTileLayerBytes + waterTileBytes;
    if (size != expected) game::FatalExit(wrongFormat);

    // Water material per tile.
    if (hasWater) {
        waterTiles.resize(kLayerTiles);
        const std::uint8_t* source = data + layers * kLayerBytes + tileLayers * kTileLayerBytes;
        for (int i = 0; i < kLayerTiles; ++i) {
            std::uint16_t material = lib::Load<std::uint16_t>(source + i * 2);
            waterTiles[i] = material == SectorVertex::kNoWaterMaterial ? kNoMaterial : material;
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
        // The grid's corner, exact as a float below 2^19 sectors.
        const float x0 = static_cast<float>(sx * kSectorCells);
        float y = static_cast<float>(sy * kSectorCells);
        int vertex = 0;
        for (int row = 0; row < kSectorRow; ++row) {
            float x = x0;
            for (int column = 0; column < kSectorRow; ++column, ++vertex, source += kVertexBytes, ++out) {
                std::uint16_t height = lib::Load<std::uint16_t>(source + 2);
                out->z = static_cast<float>(height) * owner->heightRange * kHeightUnit;
                if (layer == 0) {
                    out->x = 0.5f * static_cast<std::int8_t>(source[0]) * kOffsetScale + x;
                    out->y = 0.5f * static_cast<std::int8_t>(source[1]) * kOffsetScale + y;
                } else {
                    int material = LowestPriorityWaterMaterial(*this, vertex);
                    if (material == kNoMaterial) {
                        out->x = x;
                        out->z = 0.0f;
                        out->y = y;
                    } else if (owner->materials[material].kind != TerrainMaterial::kKeepsGroundXY) {
                        out->x = x;
                        out->y = y;
                    } else {
                        out->x = vertices[vertex].x;
                        out->y = vertices[vertex].y;
                    }
                }
                x += 1.0f;
            }
            y += 1.0f;
        }
    }
    built |= SectorStage::Loaded;
    return true;
}

// Building

void SectorData::Build() {
    records.resize(hasWater ? 2 * kSectorVertices : kSectorVertices);
    const std::uint8_t* tileWords = BuildRecords(*this);

    const int pageCount = 2 * owner->LayerPages();
    TileCounts counts = {};
    int animatedCount = CountTiles(*this, tileWords, counts);
    for (int quarter = 0; quarter < 4; ++quarter) {
        tiles[quarter].resize(pageCount);
        for (int page = 0; page < pageCount; ++page) {
            int count = counts[quarter][page];
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
            if (!lib::Contains(waterMaterials, id)) waterMaterials.push_back(static_cast<std::uint8_t>(id));
        }
        const std::span<float> levels = owner->SectorWaterLevels(sx, sy);
        for (std::uint8_t id : waterMaterials) levels[id] = owner->materials[id].level;
        ApplyWaterLevels();
    }
    for (int i = 0; i < std::ssize(records); ++i) {
        if (i >= kSectorVertices && records[i].waterMaterial == SectorVertex::kNoWaterMaterial) continue;
        records[i].specular = PackArgb(owner->EdgeFade(vertices[i]), 0, 0, 0);
    }
    render::TerrainChanged(this, render::TerrainChange::Rebuilt);   // the zone's meshes
}

void SectorData::ApplyWaterLevels() {
    const std::span<float> levels = owner->SectorWaterLevels(sx, sy);
    for (std::uint8_t id : waterMaterials) {
        const float level = levels[id];
        if (level == 0.0f) continue;
        for (int i = kSectorVertices; i < 2 * kSectorVertices; ++i) {
            SectorVertex& record = records[i];
            if (record.waterMaterial != id) continue;
            vertices[i].z = level + vertices[i].z;
        }
        levels[id] = 0.0f;
        render::TerrainChanged(this, render::TerrainChange::WaterMoved);
    }
}

void SectorData::Ensure(SectorStage wanted) {
    if (lib::Has(wanted, built)) return;
    if (wanted >= SectorStage::Built && !lib::Has(built, SectorStage::Built)) Build();
}

SectorData::~SectorData() { render::TerrainChanged(this, render::TerrainChange::Dropped); }

// Clipping and drawing

bool SectorData::IsDrawn() const { return clipMode != TransformPipeline::ClipResult::Outside; }

void SectorData::ClipTest(SceneRenderer* renderer) {
    const auto verdict = [&](const SectorBox& bounds) { return renderer->pipeline->ClipTest(&bounds); };
    clipMode = verdict(box);
    if (clipMode == TransformPipeline::ClipResult::Outside) return;
    for (int q = 0; q < 4; ++q) quarterMode[q] = verdict(quarters[q]);
}

// Clearing

void SectorData::Clear() {
    records.clear();
    for (int quarter = 0; quarter < 4; ++quarter) {
        for (std::vector<TileVertex>& list : tiles[quarter]) list.clear();
    }
    sx = sy = -1;
    hasWater = false;
    hasWaterTiles = false;
    data = nullptr;
    owner = nullptr;
    built = SectorStage::None;
    lastUsed = 0;
    clipMode = TransformPipeline::ClipResult::Outside;
    render::TerrainChanged(this, render::TerrainChange::Dropped);
}

} // namespace terrain
