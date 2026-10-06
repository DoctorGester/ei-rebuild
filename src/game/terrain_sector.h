#pragma once

// Terrain sectors (game.exe 006BF5F0-006C5410): the 32x32-cell blocks of the landscape, loaded on
// demand from the map's resource archive (<zone>XXXYYY.sec) and turned into per-vertex records and
// per-texture tile lists. Names are provisional. The landscape object itself (`Terrain`, terrain.h)
// is viewed here through `TerrainFields`, which names only the fields this code uses.
//
// A sector has 33x33 vertices per layer (ground, and water when the sector has water) and 16x16
// tiles of 2x2 cells. The tiles are grouped into four 8x8-tile quarters; each quarter keeps, per
// texture page, a byte array of tile edge flags and a list of tile entries (3x3 vertices each).

#include <cstddef>
#include <cstdint>

#include "terrain.h"

#include "lib/containers.h"
#include "lib/mfc.h"

namespace terrain {

// One entry per lit vertex: the point lights that reach it (pooled 16-byte nodes, 006CAC10).
struct LightRef {
    LightRef* next;
    LightRef* prev;
    void* pool;
    void* light;
};

#pragma pack(push, 1)
// Per-vertex record, 0x54 bytes (odd offsets: the original packs it).
struct SectorVertex {
    std::uint8_t unknown_00[0x19];      // colours, written by the lighting stage (006C31C0)
    float position[3];                  // +0x19 water layer: the vertex position
    std::uint16_t waterMaterial;        // +0x25 water layer: material index, 0xFFFF none
    float depth;                        // +0x27 ground layer: water z - ground z
    float normal[3];                    // +0x2B
    std::uint8_t unknown_37[0x43 - 0x37];
    std::uint32_t materialColour[3];    // +0x43 water material +0x1C..+0x24
    std::uint8_t materialByte;          // +0x4F water material +0x18, 0xFF none
    LightRef* lights;                   // +0x50
};
#pragma pack(pop)
static_assert(sizeof(SectorVertex) == 0x54);

// A tile entry vertex: the record it uses and its texture coordinates.
struct TileVertex {
    SectorVertex* vertex;
    float u;
    float v;
};
static_assert(sizeof(TileVertex) == 0xC);

// An animated tile (texture with several frames).
struct AnimatedTile {
    TileVertex* vertices;   // the tile's 9 entries
    int rotation;
    int frameBase;          // index of the texture within its page
    int frame;              // random start frame
    int frames;
    int speed;              // from the table at 00796960 by texture class
};
static_assert(sizeof(AnimatedTile) == 0x18);

// A bounding box: flags (bit 1 valid), centre, then the low and high corner offsets.
struct SectorBox {
    std::uint32_t flags;
    float centre[3];
    std::uint32_t unknown_10;
    float low[3];
    float high[3];
    std::uint32_t unknown_2C[3];
};
static_assert(sizeof(SectorBox) == 0x38);

struct TerrainFields;

// A vertex position of a sector layer (x, y, z).
struct SectorPosition {
    float xyz[3];
};
static_assert(sizeof(SectorPosition) == 12);

// The construct / destroy helpers of the sector's CArray instantiations (mfc::CArray's Elements
// policy): the records (006C4F10 zeroes, 006C4EE0 releases their lights), the per-page tile flag
// CByteArrays (006C4F90 / 006C4F60) and the per-page tile lists (006C5050 / 006C5010).
struct RecordElements {
    static void Construct(SectorVertex* records, int count);
    static void Destruct(SectorVertex* records, int count);
};
using FlagArray = mfc::CArray<std::uint8_t>;      // CByteArray
struct FlagArrayElements {
    static void Construct(FlagArray* arrays, int count);
    static void Destruct(FlagArray* arrays, int count);
};
using TileList = lib::AlignedArray<TileVertex>;
struct TileListElements {
    static void Construct(TileList* lists, int count);
    static void Destruct(TileList* lists, int count);
};

// 0x394 bytes, constructed by 006BF5F0.
struct SectorData {
    std::uint8_t unknown_000;
    std::uint8_t pad_001[3];
    std::uint32_t built;                // +0x004 1 loaded, 2 geometry, 4 lit
    int sx, sy;                         // +0x008
    int hasWater;                       // +0x010
    int hasWaterTiles;                  // +0x014 a second tile layer (water tiles)
    const std::uint8_t* data;           // +0x018 the .sec file after its 5-byte header
    TerrainFields* owner;               // +0x01C
    int pages;                          // +0x020 owner +0x11C
    int perPage;                        // +0x024 owner +0x128
    SectorBox box;                      // +0x028
    std::uint32_t unknown_060;
    SectorBox quarters[4];              // +0x064
    int quarterMode[4];                 // +0x144
    float minZ;                         // +0x154
    float maxZ;                         // +0x158
    mfc::CArray<std::uint8_t> waterMaterials;  // +0x15C
    int unknown_170, unknown_174, unknown_178;
    mfc::CArray<SectorPosition> vertices;      // +0x17C
    mfc::CArray<SectorVertex, RecordElements> records;   // +0x190
    lib::AlignedArray<int> tileTextures;      // +0x1A4 per tile, ground then water tiles
    std::uint8_t tileRotation[0x100];   // +0x1B4
    lib::AlignedArray<int> waterTiles;        // +0x2B4 water material per tile, -1 none
    mfc::CArray<FlagArray, FlagArrayElements> tileFlags[4];  // +0x2C4 per quarter, per page
    mfc::CArray<TileList, TileListElements> tiles[4];        // +0x314 per quarter, per page (9 per tile)
    lib::AlignedArray<AnimatedTile> animated;  // +0x364
    lib::AlignedArray<int> classCounts;       // +0x374 16 texture classes
    int lastUsed;                       // +0x384 owner frame of the last use
    int unknown_388;
    std::uint8_t unknown_38C;
    std::uint8_t pad_38D[3];
    int unknown_390;

    // 006C13A0. Loads sector (sx, sy) from `archive` (the owner's CMappedResource): vertex
    // positions and water tiles. Returns 1, or FatalError's result for a missing or malformed file.
    int Load(void* archive, int sx, int sy, int pages, int perPage, const char* prefix, TerrainFields* owner);

    // 006C1C00. Builds the vertex records, tile lists, bounding boxes and water materials.
    void Build();

    // 006C30E0. Raises the water layer by the owner's pending water level changes.
    void ApplyWaterLevels();

    // 006C3DD0. Builds what `wanted` asks for (2 geometry, 4 lighting); returns wanted & built.
    std::uint32_t Ensure(std::uint32_t wanted, TerrainFields* owner);

    // Not rewritten (forwarders).
    void Clear();                       // 006C3C80
    void BuildAtOriginal();             // 006C1C00 through its address (lands on the installed hook)
    void Light(void* lights, TerrainFields* owner, int skipColours, int x0, int y0, int x1, int y1); // 006C3A00
};
static_assert(sizeof(SectorData) == 0x394);
static_assert(offsetof(SectorData, minZ) == 0x154);
static_assert(offsetof(SectorData, vertices) == 0x17C);
static_assert(offsetof(SectorData, tileFlags) == 0x2C4);
static_assert(offsetof(SectorData, tiles) == 0x314);
static_assert(offsetof(SectorData, animated) == 0x364);
static_assert(offsetof(SectorData, lastUsed) == 0x384);

// A terrain material, 0x38 bytes (owner +0x12C).
struct TerrainMaterial {
    int kind;                           // +0x00 4: water that keeps the ground's x, y
    std::uint8_t unknown_04[0x18 - 4];
    std::uint8_t byte18;                // +0x18
    std::uint8_t pad_19[3];
    std::uint32_t colour[3];            // +0x1C
    std::uint8_t unknown_28[4];
    float priority;                     // +0x2C lowest wins where water materials meet
    std::uint8_t unknown_30[4];
    float level;                        // +0x34 water level offset
};
static_assert(sizeof(TerrainMaterial) == 0x38);

// Vertex neighbourhood: the tiles that share a vertex (owner +0x1E8, 33x33 entries).
struct VertexTiles {
    int count;
    int tiles[4];
};

// The landscape object (vtable 0073F6A8), only the fields used here.
struct TerrainFields {
    std::uint8_t unknown_000[0x104];
    int sectorsX;                       // +0x104
    int sectorsY;                       // +0x108
    std::uint8_t unknown_10C[0x114 - 0x10C];
    float heightScale;                  // +0x114
    std::uint8_t unknown_118[0x11C - 0x118];
    int pages;                          // +0x11C
    std::uint8_t unknown_120[0x128 - 0x120];
    int perPage;                        // +0x128
    TerrainMaterial* materials;         // +0x12C
    std::uint8_t unknown_130[0x13C - 0x130];
    int* textureClass;                  // +0x13C per texture, 0..15
    std::uint8_t unknown_140[0x14C - 0x140];
    int* textureFrames;                 // +0x14C per texture, frames when animated
    std::uint8_t unknown_150[0x1C8 - 0x150];
    float* uvTables[4];                 // +0x1C8 per rotation, 0x48 bytes per texture of a page
    float* uvTablesLast[4];             // +0x1D8 the same for the last two pages
    VertexTiles* vertexTiles;           // +0x1E8
    std::uint8_t unknown_1EC[0xAEC - 0x1EC];
    mfc::CArray<float>* waterLevels;    // +0xAEC per sector, per material

    // 006C53C0. The 18 floats (9 u, then 9 v) of texture `index` of `page` under `rotation`.
    const float* TileUv(int rotation, int index, int page);
    // 006C8D40. Texture class (sound / prints material) at (x, y): the water tile's when the
    // point is under water, 8 outside the map.
    int SurfaceClass(float x, float y);
};
static_assert(offsetof(TerrainFields, materials) == 0x12C);
static_assert(offsetof(TerrainFields, vertexTiles) == 0x1E8);
static_assert(offsetof(TerrainFields, waterLevels) == 0xAEC);

} // namespace terrain
