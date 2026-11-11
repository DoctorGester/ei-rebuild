#pragma once

// Terrain sectors: the 32x32-cell blocks of the landscape, loaded on demand from the map's resource archive
// (<zone>XXXYYY.sec) and turned into per-vertex records and per-texture tile lists. A sector has 33x33
// vertices per layer (ground, and water when the sector has water) and 16x16 tiles of 2x2 cells. The tiles
// are grouped into four 8x8-tile quarters; each quarter keeps, per texture page, a list of tile entries
// (3x3 vertices each).

#include "engine/scene/pipeline.h"   // ClipBounds, TransformPipeline
#include "game/terrain/terrain.h"

#include "lib/vecmath.h"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>
class SceneRenderer;

namespace terrain {

constexpr int kSectorCells = 32;                       // cells per sector side, one unit each
constexpr int kSectorRow = kSectorCells + 1;           // vertices per sector row
constexpr int kLastVertex = kSectorCells;              // the last vertex of a row / column
constexpr int kSectorVertices = kSectorRow * kSectorRow;   // per layer
constexpr int kSectorTiles = 16;                       // tiles per sector side (2 x 2 cells each)
constexpr int kLayerTiles = kSectorTiles * kSectorTiles;

// The tiles (of the sector's 16 x 16) that share a vertex; only the first `count` are set.
struct VertexTiles {
    int count = 0;
    int tiles[4] = {};
};

// A tile's texture coordinates: its 3 x 3 vertices row by row, u then v.
struct TileUv {
    static constexpr int kVertices = 9;
    float u[kVertices] = {};
    float v[kVertices] = {};
};

// Texture pages: the map's `pages` pages hold `perPage` tile textures each, then come kExtraPages pages
// (<map>L00, <map>L01) of kExtraPageTextures each. The ground tiles use pages 0 .. LayerPages() - 1, the
// water tiles the same pages numbered after them.
constexpr int kExtraPages = 2;
constexpr int kExtraPageTextures = 8;
constexpr int kMaxPages = 256;                         // room for both layers' pages in the tile counts

// The sector file (<map>XXXYYY.sec): a header (magic, flags), the vertices of each layer (ground, and water
// with flag 1), the tile words of each tile layer (ground, and water tiles with flag 1 or 2), and with water
// the water material of each tile.
constexpr int kSectorHeaderBytes = 5;
constexpr int kVertexBytes = 8;                        // x, y displacement, height, packed normal
constexpr int kLayerBytes = kSectorVertices * kVertexBytes;
constexpr int kTileLayerBytes = kLayerTiles * 2;
// A tile word: the texture in the low bits, the quarter turns in the top two; kNoTile none.
constexpr std::uint16_t kTileTextureMask = 0x3FFF;
constexpr int kTileRotationShift = 14;
constexpr std::uint16_t kNoTile = 0xFFFF;

// Per-vertex record.
struct SectorVertex {
    std::uint32_t specular = 0;         // edge fade << 24 (the colour bytes 0: the GPU adds the water's crests)
    static constexpr std::uint16_t kNoWaterMaterial = 0xFFFF;
    std::uint16_t waterMaterial = 0;    // water layer: material index, or kNoWaterMaterial
    float depth = 0.0f;                 // ground layer: water z - ground z
    Vector3 normal;
    Vector3 materialColour;             // the water material's colour
    std::uint8_t materialByte = 0;      // the water material's opacityByte, 0xFF none
};

// A tile entry vertex: the record it uses and its texture coordinates.
struct TileVertex {
    SectorVertex* vertex = nullptr;
    float u = 0.0f;
    float v = 0.0f;
};

// Animated tiles step every kTileFrameEvery frames (Terrain::RunTick counts the steps in Terrain::tileSteps).
constexpr int kTileFrameEvery = 3;
// A tile speed that steps every time.
constexpr int kAlwaysSteps = 1000;

// An animated tile (texture with several frames). The GPU works its frame out from the steps made (terrain_stages.hlsli's
// tile_uv): a tile of speed kAlwaysSteps takes every step from a start frame hashed from its `seed`, a slower one rests
// at frame 0 and plays one cycle in some windows of steps, decided by a hash of its seed and the window. The renderer
// finds a tile's vertices by `vertices`.
struct AnimatedTile {
    TileVertex* vertices = nullptr;  // the tile's 9 entries
    int rotation = 0;
    int frameBase = 0;      // index of the texture within its page
    int frames = 0;
    int speed = 0;          // from the speed table by texture class, per kAlwaysSteps resting steps
    int seed = 0;           // the tile's place in the map (layer, row, column of the whole map's tiles): what the hashes start from
};

using SectorBox = ClipBounds;

// A tile or vertex without a water material (waterTiles, the material helpers); the sector records keep it as
// SectorVertex::kNoWaterMaterial.
constexpr int kNoMaterial = -1;

struct SectorPosition : Vector3 {};

// A colour word: alpha in the top byte, then red, green, blue (each value shifted into place, not masked).
constexpr std::uint32_t PackArgb(std::uint32_t a, std::uint32_t r, std::uint32_t g, std::uint32_t b) {
    return a << 24 | r << 16 | g << 8 | b;
}

struct SectorData {
    SectorStage built = SectorStage::None;
    int sx = -1, sy = -1;
    bool hasWater = false;
    bool hasWaterTiles = false;         // a second tile layer (water tiles)
    const std::uint8_t* data = nullptr;   // the .sec file after its header
    Terrain* owner = nullptr;
    SectorBox box{};
    TransformPipeline::ClipResult clipMode = TransformPipeline::ClipResult::Outside;   // the box's clip test verdict
    SectorBox quarters[4]{};
    TransformPipeline::ClipResult quarterMode[4] = {};   // the quarters' (ClipResult::Inside until tested)
    float minZ = 0.0f;
    float maxZ = 0.0f;
    std::vector<std::uint8_t> waterMaterials;
    std::vector<SectorPosition> vertices;
    std::vector<SectorVertex> records;   // ground, then water (zeroed when made)
    std::vector<int> tileTextures;      // per tile, ground then water tiles
    std::uint8_t tileRotation[kLayerTiles] = {};
    std::vector<int> waterTiles;        // water material per tile, kNoMaterial none
    std::vector<std::vector<TileVertex>> tiles[4];       // per quarter, per page (9 per tile)
    std::vector<AnimatedTile> animated;
    std::vector<int> classCounts;       // 16 texture classes
    int lastUsed = 0;                   // owner frame of the last use

    SectorData() = default;
    // Tells the renderer (TerrainChange::Dropped), as Clear does.
    ~SectorData();
    SectorData(const SectorData&) = delete;
    SectorData& operator=(const SectorData&) = delete;

    // Vertex positions and water tiles. True; a missing or malformed file ends the game.
    bool Load(MappedResource* archive, int sx, int sy, std::string_view prefix, Terrain* owner);

    // Builds the vertex records, tile lists (the animated tiles' start frames drawn from g_clientRandom),
    // bounding boxes and water materials; every vertex's specular word becomes its edge fade (the fog factor; the
    // GPU lights the colours and adds the water's wave crests), truncated as the game's lighting stored it, on the
    // water only where it has a material.
    void Build();

    // Raises the water layer by the owner's pending water level changes.
    void ApplyWaterLevels();

    // `wanted`: SectorStage::Built. Builds the sector when it is not yet.
    void Ensure(SectorStage wanted);

    // The records and tile lists emptied (the per-quarter arrays keep their sizes); no sector (-1, -1), no
    // owner, nothing built, culled, the rest of the scalar state 0. Build sets the boxes and
    // heights again.
    void Clear();

    // Whether the last clip test did not cull the sector.
    bool IsDrawn() const;
    // The box, and unless culled its quarters, with the renderer's pipeline.
    void ClipTest(SceneRenderer* renderer);
};

struct TerrainMaterial {
    static constexpr int kKeepsGroundXY = 4;   // the kind of water whose vertices keep the ground's x, y (no map's)
    int kind = 0;
    Vector3 waveColour;                 // specular of the wave crests (Terrain::WavesOf)
    float absorption = 0.0f;            // light lost per squared depth under water (lighting): 1 / (1 - opacity)
    std::uint8_t opacityByte = 0;       // opacity * 255
    Vector3 colour;                     // waveColour * the file's light factor, each capped at 1
    float priority = 0.0f;              // lowest wins where water materials meet; also scales the waves
    float level = 0.0f;                 // water level offset
};

} // namespace terrain
