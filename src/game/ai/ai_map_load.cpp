// AIMap::Load (Rebirth patches it): the navigation grid of a map from its archive. See docs/notes/ai.md, "The
// map archive".
#include "game/ai/ai_map.h"
#include "game/console/console.h"
#include "game/app/singletons.h"
#include "game/data/map_file.h"
#include "game/data/registry.h"
#include "game/data/resource.h"
#include "game/ai/sector_map.h"
#include "game/ai/unit_ai.h"

#include "lib/bytes.h"
#include "lib/maths.h"
#include "lib/rounding.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <string>
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
    std::uint32_t magic = 0;            // kSectorMagic
    std::uint8_t layers = 0;            // bit 0: water vertices and materials follow; bits 0-1: water tiles
};
struct SectorFileVertex {
    std::int8_t offsetX = 0, offsetY = 0;
    std::uint16_t altitude = 0;         // in kAltitudeUnit of the map's highest point
    std::uint32_t normal = 0;
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

// The .mp file `name`.MP of `archive` into `map`. Returns 1; a missing file or one that is not a map (its size must
// be the header's counts) ends the game.
int ReadDescription(MappedResource& archive, const std::string& path, const std::string& name, MapDescription& map) {
    std::uint32_t size;
    void* file = archive.GetFile(name + kMapExtension, &size);
    if (!file) game::FatalExit(("Unable to open file \"" + path) + "\"");
    if (size < sizeof(MapHeader)) game::FatalExit("Wrong file format: " + path);
    std::memcpy(&map.header, file, sizeof(MapHeader));
    const MapHeader& header = map.header;
    const std::uint32_t words = header.animated + header.materials * (sizeof(MapMaterial) / 4) + header.textures;
    if (header.magic != kMapMagic || size != words * 4 + sizeof(MapHeader)) game::FatalExit("Wrong file format: " + path);

    const auto* materials = static_cast<const std::uint8_t*>(file) + sizeof(MapHeader);
    const std::size_t materialBytes = header.materials * sizeof(MapMaterial);
    map.materials.resize(header.materials);
    std::memcpy(map.materials.data(), materials, materialBytes);
    map.tileTypes.resize(header.textures);
    std::memcpy(map.tileTypes.data(), materials + materialBytes, header.textures * sizeof(std::uint32_t));
    archive.CloseFile(file);
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
constexpr float kHalfTurn = 180.0f;
constexpr float kPerDegree = 1.0f / 180.0f;

// The cost of a height step between neighbouring cells, by its slope: (sin^2 + 1)^2 times the flat
// cost uphill and on the level, the flat cost over sin^2 + 1 downhill. The larger size classes take
// no step steeper than 40 degrees; size class 0 takes any up to 60 at the flat cost.
void BuildClimbTables(AIMap& map) {
    map.climbLarge.resize(kClimbSteps);
    map.climbSmall.resize(kClimbSteps);
    for (int i = 0; i < kClimbSteps; ++i) {
        const float rise = std::fabs(static_cast<float>(i - kLevelStep) / map.heightScale);
        const float slope = std::fabs(lib::Atan2(rise, kCellSize) * (kHalfTurn / kPi));
        const float sine = lib::Sin((kPerDegree * kPi) * slope);
        float effort = (sine * sine) + 1.0f;
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
// then every section of the registry into the type its name numbers. Returns 1; an archive, file or registry that
// cannot be read ends the game.
int ReadTerrainTypes(AIMap& map) {
    MappedResource archive;
    archive.OpenResource(g_textFolder + "aiinfo.res");
    const RecordFile file = archive.FindRecord("tileDesc.reg");
    if (!file.Any()) game::FatalExit("CAIMap::Load.\n Can't open file \"tileDesc.reg\"");
    FileRegistry registry;
    if (!registry.Load(file)) game::FatalExit("CAIMap::Load.\n Can't open aiReg");
    std::vector<std::string> sections = registry.SectionNames();

    map.materials.resize(kTerrainTypes);
    for (int i = 0; i < std::ssize(map.materials); ++i) {
        AITerrainType& type = map.materials[i];
        type.name.clear();
        type.cost = 1;
        type.speed = -1;
        type.look = 1.0f;
        type.smell = 1.0f;
        type.noise = 1.0f;
    }
    for (const std::string& section : sections) {
        const int index = str::ToInt(section);
        if (index < 0 || index >= std::ssize(map.materials)) continue;   // the original wrote past the table
        AITerrainType& type = map.materials[index];
        type.name = registry.GetString(section, "Name", std::string(""));
        type.cost = registry.GetInt(section, "CostMul", -1);
        type.speed = registry.GetInt(section, "Speed", -1);
        type.look = static_cast<float>(registry.GetInt(section, "StepLook", 100)) * kHundredth;
        type.smell = static_cast<float>(registry.GetInt(section, "StepSmell", 100)) * kHundredth;
        type.noise = static_cast<float>(registry.GetInt(section, "StepSound", 100)) * kHundredth;
    }
    registry.Close();
    archive.CloseResource();
    return 1;
}

// ---- The sectors -------------------------------------------------------------------------------------

// A vertex's altitude as a fraction of the map's highest point. The game's constant (which the landscape's
// sector loader uses too) is 1 / 65534 as a float, 2^-16 + 2^-31; not 1 / 65535 (with that the ground levels
// of a few dozen cells per map were one unit off). Written as the exact fraction.
constexpr float kAltitudeUnit = 32769.0f / 2147483648.0f;

constexpr float kOuterCornerWeight = 0.25f;   // the weight of each of the two other corners beside a cell's corner

// The heights in float maths, value by value.

// A vertex's height in world units: the altitude times the map's highest point times the altitude unit, in that
// order.
inline float VertexHeight(const SectorFileVertex& vertex, float maxZ) {
    return static_cast<float>(vertex.altitude) * maxZ * kAltitudeUnit;
}

// A vertex layer of a sector file, its vertices read by value (the layers start at byte 5, unaligned).
struct SectorVertices {
    const std::uint8_t* bytes = nullptr;
    SectorFileVertex operator[](int index) const {
        SectorFileVertex vertex = lib::Load<SectorFileVertex>(bytes + static_cast<std::size_t>(index) * sizeof(SectorFileVertex));
        return vertex;
    }
};

// Four floats of a terrain square, one per cell: [0] at its first vertex, [1] towards the next
// vertex of the row, [2] towards the next row, [3] opposite.
struct SquareHeights {
    float cell[4] = {};
};

// The ground's height at the four cells of a terrain square: half of the cell's own corner and a quarter of
// each of the two corners beside it. The original stores the heights of the three other corners and keeps the
// first one, and the quarter of the first and the opposite corner, on the stack.
SquareHeights LandHeights(SectorVertices layer, int vertex, float maxZ) {
    const float next = VertexHeight(layer[vertex + 1], maxZ);
    const float below = VertexHeight(layer[vertex + kVertexRow], maxZ);
    const float opposite = VertexHeight(layer[vertex + kVertexRow + 1], maxZ);
    const float first = VertexHeight(layer[vertex], maxZ);
    const float sides = (below + next) * kOuterCornerWeight;
    SquareHeights heights;
    heights.cell[0] = first * 0.5f + sides;
    const float diagonal = (first + opposite) * kOuterCornerWeight;
    heights.cell[1] = next * 0.5f + diagonal;
    heights.cell[2] = below * 0.5f + diagonal;
    heights.cell[3] = opposite * 0.5f + sides;
    return heights;
}

// The water's depth over the four cells of a terrain square: its surface, weighted like the ground, less the
// ground `land`. The original keeps the third cell's surface on the stack here as well and subtracts the
// ground from it there.
SquareHeights WaterDepths(SectorVertices layer, int vertex, float maxZ, const SquareHeights& land) {
    const float next = VertexHeight(layer[vertex + 1], maxZ);
    const float below = VertexHeight(layer[vertex + kVertexRow], maxZ);
    const float opposite = VertexHeight(layer[vertex + kVertexRow + 1], maxZ);
    const float first = VertexHeight(layer[vertex], maxZ);
    const float sides = (below + next) * kOuterCornerWeight;
    const float surface0 = first * 0.5f + sides;
    const float diagonal = (first + opposite) * kOuterCornerWeight;
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
    SectorHeader header = lib::Load<SectorHeader>(file);
    if (header.magic != kSectorMagic) return false;
    std::size_t expected = sizeof(SectorHeader) + kVertexBytes + kTileBytes;
    if (header.layers & 1) expected += kVertexBytes + kTileBytes;
    if (header.layers & 3) expected += kTileBytes;
    return size == expected;
}

// A sector file into the cells from (x0, y0): the ground levels; with water the depth of the water
// over each cell (0 .. 63); the tiles' terrain types; the water flag of the cells with depth under a
// tile of a water material; and the water tile's terrain type on the cells under water.
void StampSector(AIMap& map, const MapDescription& description, const void* file, int x0, int y0) {
    SectorHeader header = lib::Load<SectorHeader>(file);
    const bool hasWater = (header.layers & 1) != 0;
    const bool hasWaterTiles = (header.layers & 3) != 0;
    const SectorVertices ground{static_cast<const std::uint8_t*>(file) + sizeof(SectorHeader)};
    const SectorVertices water{ground.bytes + kVertexBytes};
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
            const SquareHeights land = LandHeights(ground, vertex, maxZ);
            for (int i = 0; i < 4; ++i) cells[i]->groundLevel = static_cast<std::uint16_t>(HeightUnits(land.cell[i], scale));
            if (!hasWater) continue;
            const SquareHeights depths = WaterDepths(water, vertex, maxZ, land);
            for (int i = 0; i < 4; ++i) {
                int depth = HeightUnits(depths.cell[i], scale);
                depth = depth >= kMaxDepth ? kMaxDepth : depth < 0 ? 0 : depth;
                cells[i]->flags = static_cast<std::uint8_t>((cells[i]->flags & ~kCellDepthMask) | depth);
            }
        }
    }

    // The tile words after the vertex layers: the tiles, the water tiles, the water materials (unaligned: the header
    // is 5 bytes).
    const std::uint8_t* tileWords = ground.bytes + (hasWater ? 2 : 1) * kVertexBytes;
    auto tileWord = [tileWords](int layer, int tx, int ty) {
        std::uint16_t word = lib::Load<std::uint16_t>(tileWords + 2 * ((layer * kTiles + ty) * kTiles + tx));
        return word;
    };
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
            const std::uint32_t type = TileType(description, tileWord(0, tx, ty));
            forTileCells(tx, ty, [type](AIMapCell&, std::uint32_t& occluder) { occluder = occluder::WithType(occluder, type); });
        }
    if (hasWater)
        for (int ty = 0; ty < kTiles; ++ty)
            for (int tx = 0; tx < kTiles; ++tx) {
                const std::uint16_t material = tileWord(2, tx, ty);
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
                const std::uint16_t tile = tileWord(1, tx, ty);
                if (tile == kNoTile) continue;
                const std::uint32_t type = TileType(description, tile);
                forTileCells(tx, ty, [type](AIMapCell& cell, std::uint32_t& occluder) {
                    if (cell.InWater() && cell.WaterDepth() != 0) occluder = occluder::WithType(occluder, type);
                });
            }
}

} // namespace

int AIMap::LoadTerrain(const std::string& full) {
    MappedResource archive;
    // (The original also builds the path without its extension here and never uses it.)
    archive.OpenResource(full);
    std::vector<std::string> maps;
    archive.ListFiles(&maps, "*.mp");
    if (maps.empty()) return 0;   // no map in the archive (the original read the first of none)
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
            const std::string file = std::format("{}{:03}{:03}{}", name, sectorX, sectorY, kSectorExtension);
            std::uint32_t size;
            void* sector = archive.GetFile(file, &size);
            if (!sector) game::FatalExit("Unable to locate resource: " + file);
            if (!IsSector(sector, size)) game::FatalExit("Wrong format resource: " + file);
            StampSector(*this, description, sector, sectorX * kSectorCells, sectorY * kSectorCells);
            archive.CloseFile(sector);
        }
    }
    return 1;
}

void AIMap::AttachTerrain() {
    loaded = true;
    refreshUnits = false;
    BuildOpenMarks();
    SearchGrid<std::int8_t>& water = waterDepth;
    water.Resize(cells.width, cells.height);
    water.data.assign(water.data.size(), 0);
    forwardSearch.Attach(this, false);
    backwardSearch.Attach(this, true);
    regionMap.Attach(this);
    events.Setup(this, static_cast<float>(cells.width) * kCellSize, static_cast<float>(cells.height) * kCellSize);
    sectorMap.Init(cells.width, cells.height, this);
    QueueRefresh({0, 0}, {cells.width - 1, cells.height - 1});
}

int AIMap::Load(const std::string& path) {
    if (const int result = LoadTerrain(path); result != 1) return result;
    AttachTerrain();
    return 1;
}
