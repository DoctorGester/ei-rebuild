// AIMap::Load (Rebirth patches it): the navigation grid of a map from its archive. See docs/notes/ai.md, "The
// map archive".
#include "ai_map.h"
#include "console.h"
#include "map_file.h"
#include "registry.h"
#include "resource.h"
#include "sector_map.h"
#include "unit_ai.h"
#include "world_data.h"

#include "lib/rounding.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <string>
#include "lib/in_place.h"
#include "lib/pc24.h"
#include "lib/str.h"
#include "lib/vecmath.h"

#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

// ---- The files -------------------------------------------------------------------------------------

constexpr const char* kMapExtension = /*0073FD1C*/ ".MP";
constexpr const char* kSectorExtension = /*0073FD20*/ ".SEC";
constexpr std::uint32_t kSectorMagic = 0xCF4BF774;

using terrain::kMapMagic;
using terrain::MapHeader;
using terrain::MapMaterial;   // the AI map only asks whether it is water

#pragma pack(push, 1)
// A sector file's header, and a vertex.
struct SectorHeader {
    std::uint32_t magic;                // kSectorMagic
    std::uint8_t layers;                // bit 0: water vertices and materials follow; bits 0-1: water tiles
};
struct SectorFileVertex {
    std::int8_t offsetX, offsetY;
    std::uint16_t altitude;             // in kAltitudeUnit of the map's highest point
    std::uint32_t normal;
};
#pragma pack(pop)
static_assert(sizeof(SectorHeader) == 5 && sizeof(SectorFileVertex) == 8);

constexpr int kSquares = 32;                        // terrain squares along a sector's side
constexpr int kVertexRow = kSquares + 1;            // vertices along it
constexpr int kTiles = 16;                          // tiles along it
constexpr int kSectorCells = 64;                    // AI cells along it
constexpr int kCellsPerTile = 4;
constexpr std::size_t kVertexBytes = kVertexRow * kVertexRow * sizeof(SectorFileVertex);
constexpr std::size_t kTileBytes = kTiles * kTiles * sizeof(std::uint16_t);
constexpr std::uint16_t kNoTile = 0xFFFF;
constexpr std::uint16_t kTileIndexMask = 0x3FFF;    // the rest of a tile word turns the texture
constexpr int kTilesPerTexture = 64;
constexpr std::uint8_t kUnknownTileType = 0xF;      // a tile outside the textures
constexpr int kWaterKindA = 2, kWaterKindB = 3;

constexpr int kMaxDepth = 0x3F;

// What the .mp file gives the sectors: its header and copies of the materials and the tile types
// (the file is closed before the sectors are read).
struct MapDescription {
    MapDescription() {
    }
    ~MapDescription() {
    }
    MapDescription(const MapDescription&) = delete;

    MapHeader header;
    std::vector<MapMaterial> materials;
    std::vector<std::uint32_t> tileTypes;
};

// A resource archive as a local, constructed in place.
struct Archive {
    Archive() { new (Get()) MappedResource(); }
    ~Archive() { Get()->MappedResource::Destroy(); }
    Archive(const Archive&) = delete;
    MappedResource* Get() { return storage.Get(); }
    bool Open(std::string_view path) { return Get()->MappedResource::OpenResource(std::string(path)) != 0; }
    void* File(const char* name, std::uint32_t* size) { return Get()->MappedResource::GetFile(name, size); }
    void Close(void* file) { Get()->MappedResource::CloseFile(file); }

    lib::InPlace<MappedResource> storage;
};

// The .mp file `name`.MP of `archive` into `map`. Returns 1, or the fatal error's result when the
// file is missing or not a map (its size must be the header's counts).
int ReadDescription(Archive& archive, const std::string& path, const std::string& name, MapDescription& map) {
    std::uint32_t size;
    void* file = archive.File((name + kMapExtension).c_str(), &size);
    if (!file) return game::FatalError(("Unable to open file \"" + path) + "\"");
    if (size < sizeof(MapHeader)) return game::FatalError("Wrong file format: " + path);
    std::memcpy(&map.header, file, sizeof(MapHeader));
    const MapHeader& header = map.header;
    const std::uint32_t words = header.animated + header.materials * (sizeof(MapMaterial) / 4) + header.textures;
    if (header.magic != kMapMagic || size != words * 4 + sizeof(MapHeader)) return game::FatalError("Wrong file format: " + path);

    const auto* materials = static_cast<const std::uint8_t*>(file) + sizeof(MapHeader);
    const std::size_t materialBytes = header.materials * sizeof(MapMaterial);
    map.materials.resize(header.materials);
    std::memcpy(map.materials.data(), materials, materialBytes);
    map.tileTypes.resize(header.textures);
    std::memcpy(map.tileTypes.data(), materials + materialBytes, header.textures * sizeof(std::uint32_t));
    archive.Close(file);
    return 1;
}

// ---- The climb tables --------------------------------------------------------------------------------

constexpr float kHeightLevels = 511.0f;     // AI height units up to the map's highest point
constexpr int kClimbSteps = 0x3FF;          // steps of -511 .. +511 height units
constexpr int kLevelStep = kClimbSteps / 2; // the table's middle: no step
constexpr float kCellSize = 0.5f;           // a cell's side in world units
constexpr float kFlatCost = 1024.0f;
constexpr int kFlatCostUnits = 0x400;
constexpr float kLargeLimit = 40.0f;        // degrees
constexpr float kSmallLimit = 60.0f;
// Doubles the original divides and multiplies on the FPU (with vecmath.h's kPiDouble), each operation rounded to a
// float (lib/pc24.h).
constexpr double kHalfTurn = 180.0;
constexpr double kPerDegree = 1.0 / 180.0;

// The cost of a height step between neighbouring cells, by its slope: (sin^2 + 1)^2 times the flat
// cost uphill and on the level, the flat cost over sin^2 + 1 downhill. The larger size classes take
// no step steeper than 40 degrees; size class 0 takes any up to 60 at the flat cost.
void BuildClimbTables(AIMap& map) {
    map.climbLarge.resize(kClimbSteps);
    map.climbSmall.resize(kClimbSteps);
    for (int i = 0; i < kClimbSteps; ++i) {
        const float rise = std::fabs(static_cast<float>(static_cast<float>(i - kLevelStep) / map.heightScale));
        const float slope = std::fabs(pc24::Mul(pc24::Atan2(rise, kCellSize), pc24::Div(kHalfTurn, kPiDouble)));
        const pc24::Unrounded sine = pc24::Sin(pc24::Mul(kPerDegree, kPiDouble) * slope);
        float effort = pc24::Mul(sine, sine) + 1.0f;
        effort *= effort;
        if (i < kLevelStep) effort = 1.0f / std::sqrt(effort);
        const int cost = lib::RoundEven(effort * kFlatCost);
        map.climbSmall[i] = slope > kSmallLimit ? -1 : kFlatCostUnits;
        map.climbLarge[i] = slope > kLargeLimit ? -1 : cost;
    }
}

// ---- The terrain types -------------------------------------------------------------------------------

constexpr int kTerrainTypes = 16;
constexpr float kHundredth = 0.01f;

// The terrain types from tileDesc.reg in the text resources' aiinfo.res: sixteen with the defaults,
// then every section of the registry into the type its name numbers. Returns 1, or the fatal
// error's result when the archive, the file or the registry cannot be read.
int ReadTerrainTypes(AIMap& map) {
    Archive archive;
    bool opened;
    {
        std::string folder = g_textFolder;
        opened = archive.Open((folder + "aiinfo.res"));
    }
    if (!opened) return game::FatalError("Unable to read file aiinfo.res");
    void* file = archive.File("tileDesc.reg", nullptr);
    if (!file) return game::FatalError("CAIMap::Load.\n Can't open file \"tileDesc.reg\"");
    FileRegistry registry;
    if (!registry.Attach(file)) return game::FatalError("CAIMap::Load.\n Can't open aiReg");
    std::vector<std::string> sections = registry.SectionNames();

    map.materials.resize(kTerrainTypes);
    for (int i = 0; i < std::ssize(map.materials); ++i) {
        AITerrainType& type = map.materials[i];
        type.name[0] = '\0';
        type.cost = 1;
        type.speed = -1;
        type.look = 1.0f;
        type.smell = 1.0f;
        type.noise = 1.0f;
    }
    for (const std::string& sectionName : sections) {
        const char* const section = sectionName.c_str();
        const int index = std::atoi(section);
        {
            std::string name = registry.GetString(section, "Name", std::string(""));
            const char* text = name.c_str();
            std::memcpy(map.materials[index].name, text, std::strlen(text) + 1);
        }
        AITerrainType& type = map.materials[index];
        type.cost = registry.GetInt(section, "CostMul", -1);
        type.speed = registry.GetInt(section, "Speed", -1);
        type.look = static_cast<float>(registry.GetInt(section, "StepLook", 100)) * kHundredth;
        type.smell = static_cast<float>(registry.GetInt(section, "StepSmell", 100)) * kHundredth;
        type.noise = static_cast<float>(registry.GetInt(section, "StepSound", 100)) * kHundredth;
    }
    registry.Close();
    archive.Close(file);
    archive.Get()->MappedResource::CloseResource();
    return 1;
}

// ---- The sectors -------------------------------------------------------------------------------------

// A vertex's altitude as a fraction of the map's highest point. The game's constant (which the landscape's
// sector loader uses too) is 1 / 65534 as a float, 2^-16 + 2^-31; not 1 / 65535 (with that the ground levels
// of a few dozen cells per map were one unit off). Written as the exact fraction.
constexpr float kAltitudeUnit = 32769.0f / 2147483648.0f;
static_assert(std::bit_cast<std::uint32_t>(kAltitudeUnit) == 0x37800100u);

constexpr float kQuarter = 0.25f;

// The heights follow the original's x87 sequence value by value, in float maths (its registers held floats at the
// main thread's 24-bit precision).

// A vertex's height in world units: the altitude times the map's highest point times the altitude unit, in that
// order.
__forceinline float VertexHeight(const SectorFileVertex& vertex, float maxZ) {
    return static_cast<float>(vertex.altitude) * maxZ * kAltitudeUnit;
}

// Four floats of a terrain square, one per cell: [0] at its first vertex, [1] towards the next
// vertex of the row, [2] towards the next row, [3] opposite.
struct SquareHeights {
    float cell[4];
};

// The ground's height at the four cells of a terrain square: half of the cell's own corner and a quarter of
// each of the two corners beside it. The original stores the heights of the three other corners and keeps the
// first one, and the quarter of the first and the opposite corner, on the stack.
SquareHeights LandHeights(const SectorFileVertex* vertex, float maxZ) {
    const float next = VertexHeight(vertex[1], maxZ);
    const float below = VertexHeight(vertex[kVertexRow], maxZ);
    const float opposite = VertexHeight(vertex[kVertexRow + 1], maxZ);
    const float first = VertexHeight(vertex[0], maxZ);
    const float sides = (below + next) * kQuarter;
    SquareHeights heights;
    heights.cell[0] = first * 0.5f + sides;
    const float diagonal = (first + opposite) * kQuarter;
    heights.cell[1] = next * 0.5f + diagonal;
    heights.cell[2] = below * 0.5f + diagonal;
    heights.cell[3] = opposite * 0.5f + sides;
    return heights;
}

// The water's depth over the four cells of a terrain square: its surface, weighted like the ground, less the
// ground `land`. The original keeps the third cell's surface on the stack here as well and subtracts the
// ground from it there.
SquareHeights WaterDepths(const SectorFileVertex* vertex, float maxZ, const SquareHeights& land) {
    const float next = VertexHeight(vertex[1], maxZ);
    const float below = VertexHeight(vertex[kVertexRow], maxZ);
    const float opposite = VertexHeight(vertex[kVertexRow + 1], maxZ);
    const float first = VertexHeight(vertex[0], maxZ);
    const float sides = (below + next) * kQuarter;
    const float surface0 = first * 0.5f + sides;
    const float diagonal = (first + opposite) * kQuarter;
    const float surface1 = next * 0.5f + diagonal;
    const float surface2 = below * 0.5f + diagonal;
    const float surface3 = opposite * 0.5f + sides;
    SquareHeights depths;
    depths.cell[0] = surface0 - land.cell[0];
    depths.cell[1] = surface1 - land.cell[1];
    depths.cell[2] = surface2 - land.cell[2];
    depths.cell[3] = surface3 - land.cell[3];
    return depths;
}

// A world height in the map's units (the float product rounded to nearest).
int HeightUnits(float height, float scale) { return lib::RoundEven(height * scale); }

// The terrain type of a tile word: the type word of its tile, 0xF outside the textures.
std::uint32_t TileType(const MapDescription& map, std::uint16_t tile) {
    const std::uint32_t index = tile & kTileIndexMask;
    const std::uint32_t tiles = static_cast<std::uint32_t>(map.header.pages) * kTilesPerTexture;
    const std::uint8_t type =
        index < tiles ? static_cast<std::uint8_t>(map.tileTypes[static_cast<int>(index)]) : kUnknownTileType;
    return occluder::Type(type);
}

// Whether a sector file of `size` bytes is one: the magic, and the size its layers give.
bool IsSector(const void* file, std::uint32_t size) {
    if (size < sizeof(SectorHeader)) return false;
    const auto* header = static_cast<const SectorHeader*>(file);
    if (header->magic != kSectorMagic) return false;
    std::size_t expected = sizeof(SectorHeader) + kVertexBytes + kTileBytes;
    if (header->layers & 1) expected += kVertexBytes + kTileBytes;
    if (header->layers & 3) expected += kTileBytes;
    return size == expected;
}

// A sector file into the cells from (x0, y0): the ground levels; with water the depth of the water
// over each cell (0 .. 63); the tiles' terrain types; the water flag of the cells with depth under a
// tile of a water material; and the water tile's terrain type on the cells under water.
void StampSector(AIMap& map, const MapDescription& description, const void* file, int x0, int y0) {
    const auto* header = static_cast<const SectorHeader*>(file);
    const bool hasWater = (header->layers & 1) != 0;
    const bool hasWaterTiles = (header->layers & 3) != 0;
    const auto* ground = reinterpret_cast<const SectorFileVertex*>(header + 1);
    const SectorFileVertex* water = ground + kVertexRow * kVertexRow;
    const float maxZ = description.header.heightRange;
    const float scale = map.heightScale;

    for (int row = 0; row < kSquares; ++row) {
        for (int column = 0; column < kSquares; ++column) {
            const int vertex = row * kVertexRow + column;
            AIMapCell* cells[4];
            cells[0] = &map.cells[y0 + row * 2][x0 + column * 2];
            cells[1] = cells[0] + 1;
            cells[2] = &map.cells[y0 + row * 2 + 1][x0 + column * 2];
            cells[3] = cells[2] + 1;
            const SquareHeights land = LandHeights(ground + vertex, maxZ);
            for (int i = 0; i < 4; ++i) cells[i]->groundLevel = static_cast<std::uint16_t>(HeightUnits(land.cell[i], scale));
            if (!hasWater) continue;
            const SquareHeights depths = WaterDepths(water + vertex, maxZ, land);
            for (int i = 0; i < 4; ++i) {
                int depth = HeightUnits(depths.cell[i], scale);
                depth = depth >= kMaxDepth ? kMaxDepth : depth < 0 ? 0 : depth;
                cells[i]->flags = static_cast<std::uint8_t>((cells[i]->flags & ~kCellDepthMask) | depth);
            }
        }
    }

    const auto* tiles = reinterpret_cast<const std::uint16_t*>(reinterpret_cast<const std::uint8_t*>(ground) +
                                                                (hasWater ? 2 : 1) * kVertexBytes);
    const std::uint16_t* waterTiles = tiles + kTiles * kTiles;
    const std::uint16_t* waterMaterials = waterTiles + kTiles * kTiles;
    // Calls `visit(cell, occluder word)` for the 4 x 4 cells of tile (tx, ty).
    auto forTileCells = [&](int tx, int ty, auto visit) {
        for (int j = 0; j < kCellsPerTile; ++j)
            for (int i = 0; i < kCellsPerTile; ++i) {
                const int x = x0 + tx * kCellsPerTile + i, y = y0 + ty * kCellsPerTile + j;
                visit(map.cells[y][x], map.occluders[y][x]);
            }
    };
    for (int ty = 0; ty < kTiles; ++ty)
        for (int tx = 0; tx < kTiles; ++tx) {
            const std::uint32_t type = TileType(description, tiles[ty * kTiles + tx]);
            forTileCells(tx, ty, [type](AIMapCell&, std::uint32_t& occluder) { occluder = occluder::WithType(occluder, type); });
        }
    if (hasWater)
        for (int ty = 0; ty < kTiles; ++ty)
            for (int tx = 0; tx < kTiles; ++tx) {
                const std::uint16_t material = waterMaterials[ty * kTiles + tx];
                bool liquid = false;
                if (material != kNoTile) {
                    const int kind = description.materials[material].kind;
                    liquid = kind == kWaterKindA || kind == kWaterKindB;
                }
                forTileCells(tx, ty, [liquid](AIMapCell& cell, std::uint32_t&) {
                    const bool under = liquid && cell.WaterDepth() != 0;
                    cell.flags = static_cast<std::uint8_t>((cell.flags & ~kCellWater) | (under ? kCellWater : 0));
                });
            }
    if (hasWaterTiles)
        for (int ty = 0; ty < kTiles; ++ty)
            for (int tx = 0; tx < kTiles; ++tx) {
                const std::uint16_t tile = waterTiles[ty * kTiles + tx];
                if (tile == kNoTile) continue;
                const std::uint32_t type = TileType(description, tile);
                forTileCells(tx, ty, [type](AIMapCell& cell, std::uint32_t& occluder) {
                    if (cell.InWater() && cell.WaterDepth() != 0) occluder = occluder::WithType(occluder, type);
                });
            }
}

} // namespace

int AIMap::LoadTerrain(const char* path) {
    Archive archive;
    std::string full = path;
    // (The original also builds the path without its extension here and never uses it.)
    if (!archive.Open(full)) return 0;
    std::vector<std::string> maps;
    archive.Get()->ListFiles(&maps, "*.mp");
    std::string name = str::Left(maps[0], str::Find(maps[0], '.'));

    MapDescription description;
    if (const int result = ReadDescription(archive, full, name, description); result != 1) return result;
    heightScale = kHeightLevels / description.header.heightRange;
    BuildClimbTables(*this);
    if (const int result = ReadTerrainTypes(*this); result != 1) return result;

    const int cellsX = description.header.sectorsX * kSectorCells;
    const int cellsY = description.header.sectorsY * kSectorCells;
    cells.Resize(cellsX, cellsY);
    occluders.Resize(cellsX, cellsY, occluder::kNone);
    marks.Resize(cellsX, cellsY);
    for (int sectorY = 0; sectorY < description.header.sectorsY; ++sectorY) {
        for (int sectorX = 0; sectorX < description.header.sectorsX; ++sectorX) {
            char file[432];
            std::snprintf(file, sizeof(file), "%s%3.3i%3.3i%s", name.c_str(), sectorX, sectorY, kSectorExtension);
            std::uint32_t size;
            void* sector = archive.File(file, &size);
            if (!sector) return game::FatalError("Unable to locate resource: " + std::string(file));
            if (!IsSector(sector, size)) return game::FatalError("Wrong format resource: " + std::string(file));
            StampSector(*this, description, sector, sectorX * kSectorCells, sectorY * kSectorCells);
            archive.Close(sector);
        }
    }
    return 1;
}

void AIMap::AttachTerrain() {
    loaded = true;
    refreshUnits = false;
    BuildOpenMarks();
    InitCostTables(this);
    SearchGrid<std::int8_t>& water = waterDepth;
    water.Resize(cells.width, cells.height);
    water.data.assign(water.data.size(), 0);
    forwardSearch.Attach(this, false);
    backwardSearch.Attach(this, true);
    regionMap.Attach(this);
    events.Setup(this, static_cast<float>(cells.width) * kCellSize, static_cast<float>(cells.height) * kCellSize);
    sectorMap.Init(cells.width, cells.height, this);
    QueueRefresh(0, cells.width - 1, 0, cells.height - 1);
}

int AIMap::Load(const char* path) {
    if (const int result = LoadTerrain(path); result != 1) return result;
    AttachTerrain();
    return 1;
}
