// CAIMap::Load (005B43C0, A-W15): the navigation grid of a map from its archive. Rebirth patches
// the function, so this is the vanilla body and it is installed in the vanilla launch only; the
// Rebirth-patched callees (the archive's open 00671730 and destructor 006714A0, FatalError
// 00480080, the AI events' Setup 005C9310) are called through their addresses.
//
// A map archive holds one .mp file (MapHeader, the materials, a type word per tile texture, the
// animated tiles) and a .sec file per sector of 32 x 32 terrain squares (SectorHeader, 33 x 33
// vertices, the same again for the water when the sector has any, then 16 x 16 tile words, the
// water's tile words and the water's material indices). The AI map has 2 x 2 cells per terrain
// square and 4 x 4 per tile: 64 x 64 cells per sector.
//
// The original is one function; it is split here into LoadTerrain (everything read from the files)
// and AttachTerrain (the rest), and its locals (archives, registry, strings) are destroyed at the
// end of the step that uses them instead of at the very end. Nothing reads them in between.
#include "ai_grids.h"
#include "ai_map.h"
#include "console.h"
#include "originals_engine.h"    // game::FatalError, MappedResourceOpenOriginal, MappedResourceDestroyOriginal
#include "originals_library.h"   // CStringFindCharOriginal
#include "registry.h"
#include "resource.h"
#include "sector_map.h"
#include "unit_ai.h"
#include "world_data.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "lib/vecmath.h"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {

// AIEventTable::Setup at its address (RET 0xC): Rebirth patches it.
__declspec(naked) int __fastcall AIEventsSetupAt(AIEventTable*, void*, CAIMap*, float /*sizeX*/, float /*sizeY*/)
    EI_ORIGINAL(0x005C9310)

// ---- The files -------------------------------------------------------------------------------------

constexpr std::uint32_t kMapMagic = 0xCE4AF672;
constexpr std::uint32_t kSectorMagic = 0xCF4BF774;

#pragma pack(push, 1)
// The .mp file's header (0x26 bytes).
struct MapHeader {
    std::uint32_t magic;                // kMapMagic
    float maxZ;                         // +0x04 the highest point: a vertex's altitude is a fraction of it
    std::int32_t sectorsX, sectorsY;    // +0x08
    std::uint32_t textureCount;         // +0x10 tile textures of 64 tiles each
    std::uint32_t textureSize;
    std::uint32_t tileCount;            // +0x18 the tile type words
    std::uint32_t tileSize;
    std::uint16_t materialCount;        // +0x20
    std::uint32_t animatedTileCount;    // +0x22
};
// A material of the map (0x2C bytes); the AI map only asks whether it is water.
struct MapMaterial {
    std::int32_t kind;                  // 2 and 3: water
    std::uint8_t rest[0x28];
};
// A sector file's header, and a vertex.
struct SectorHeader {
    std::uint32_t magic;                // kSectorMagic
    std::uint8_t layers;                // bit 0: water vertices and materials follow; bits 0-1: water tiles
};
struct SectorFileVertex {
    std::int8_t offsetX, offsetY;
    std::uint16_t altitude;             // +0x02 in kAltitudeUnit of the map's highest point
    std::uint32_t normal;
};
#pragma pack(pop)
static_assert(sizeof(MapHeader) == 0x26 && offsetof(MapHeader, materialCount) == 0x20);
static_assert(sizeof(MapMaterial) == 0x2C && sizeof(SectorHeader) == 5 && sizeof(SectorFileVertex) == 8);

constexpr int kSquares = 32;                        // terrain squares along a sector's side
constexpr int kVertexRow = kSquares + 1;            // vertices along it
constexpr int kTiles = 16;                          // tiles along it
constexpr int kSectorCells = 64;                    // AI cells along it
constexpr int kCellsPerTile = 4;
constexpr std::size_t kVertexBytes = kVertexRow * kVertexRow * sizeof(SectorFileVertex);        // 0x2208
constexpr std::size_t kTileBytes = kTiles * kTiles * sizeof(std::uint16_t);                     // 0x200
constexpr std::uint16_t kNoTile = 0xFFFF;
constexpr std::uint16_t kTileIndexMask = 0x3FFF;    // the rest of a tile word turns the texture
constexpr int kTilesPerTexture = 64;
constexpr std::uint32_t kTypeMask = 0x1F;           // the terrain type in an occluder word
constexpr std::uint8_t kUnknownTileType = 0xF;      // a tile outside the textures
constexpr int kWaterKindA = 2, kWaterKindB = 3;

constexpr std::uint8_t kCellDepthMask = 0x3F;       // AIMapCell::flags: the water's depth (object height later)
constexpr std::uint8_t kCellWater = 0x80;           // AIMapCell::flags: under water
constexpr int kMaxDepth = 0x3F;

// What the .mp file gives the sectors: its header and copies of the materials and the tile types
// (the file is closed before the sectors are read).
struct MapDescription {
    MapDescription() {
        materials.Construct();
        tileTypes.Construct();
    }
    ~MapDescription() {
        tileTypes.FreeData();
        materials.FreeData();
    }
    MapDescription(const MapDescription&) = delete;

    MapHeader header;
    lib::AlignedArray<MapMaterial> materials;
    lib::AlignedArray<std::uint32_t> tileTypes;
};

// A resource archive as a local: constructed in place (00671280), destroyed through the destructor
// body's address.
struct Archive {
    Archive() { Get()->Construct(); }
    ~Archive() { MappedResourceDestroyOriginal(Get()); }
    Archive(const Archive&) = delete;
    CMappedResource* Get() { return reinterpret_cast<CMappedResource*>(storage); }
    bool Open(const char* path) { return MappedResourceOpenOriginal(Get(), nullptr, path, 0) != 0; }
    void* File(const char* name, std::uint32_t* size) { return Get()->CMappedResource::GetFileImpl(name, size); }
    void Close(void* file) { Get()->CMappedResource::CloseFileImpl(file); }

    alignas(void*) unsigned char storage[sizeof(CMappedResource)];
};

// A registry as a local (00694520 / 00694540).
struct Registry {
    Registry() { file.Construct(); }
    ~Registry() { file.Destroy(); }
    Registry(const Registry&) = delete;
    CFileRegistry file;
};

int Fatal(const mfc::CString& message) { return game::FatalError(message); }

// The .mp file `name`.MP of `archive` into `map`. Returns 1, or the fatal error's result when the
// file is missing or not a map (its size must be the header's counts).
int ReadDescription(Archive& archive, const mfc::CString& path, const mfc::CString& name, MapDescription& map) {
    std::uint32_t size;
    void* file = archive.File(name + ".MP", &size);
    if (!file) return Fatal(("Unable to open file \"" + path) + "\"");
    if (size < sizeof(MapHeader)) return Fatal("Wrong file format: " + path);
    std::memcpy(&map.header, file, sizeof(MapHeader));
    const MapHeader& header = map.header;
    const std::uint32_t words = header.animatedTileCount + header.materialCount * (sizeof(MapMaterial) / 4) + header.tileCount;
    if (header.magic != kMapMagic || size != words * 4 + sizeof(MapHeader)) return Fatal("Wrong file format: " + path);

    const auto* materials = static_cast<const std::uint8_t*>(file) + sizeof(MapHeader);
    const std::size_t materialBytes = header.materialCount * sizeof(MapMaterial);
    map.materials.SetSize(header.materialCount);
    std::memcpy(map.materials.data, materials, materialBytes);
    map.tileTypes.SetSize(static_cast<int>(header.tileCount));
    std::memcpy(map.tileTypes.data, materials + materialBytes, header.tileCount * sizeof(std::uint32_t));
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
// The original divides and multiplies these on the FPU as it goes, at the thread's precision; they
// are read at run time so the compiler does not fold them at its own.
const volatile double kPi = 3.141592653589793;
const volatile double kHalfTurn = 180.0;
const volatile double kPerDegree = 1.0 / 180.0;

// The cost of a height step between neighbouring cells, by its slope: (sin^2 + 1)^2 times the flat
// cost uphill and on the level, the flat cost over sin^2 + 1 downhill. The larger size classes take
// no step steeper than 40 degrees; size class 0 takes any up to 60 at the flat cost.
void BuildClimbTables(CAIMap& map) {
    map.climbLarge.SetSize(kClimbSteps);
    map.climbSmall.SetSize(kClimbSteps);
    for (int i = 0; i < kClimbSteps; ++i) {
        const float rise = std::fabs(static_cast<float>(static_cast<float>(i - kLevelStep) / map.heightScale));
        const float slope = std::fabs(static_cast<float>(Atan2(rise, kCellSize) * (kHalfTurn / kPi)));
        const double sine = x87::fsin(kPi * kPerDegree * slope);
        double effort = sine * sine + 1.0;
        effort *= effort;
        if (i < kLevelStep) effort = 1.0 / std::sqrt(effort);
        const int cost = x87::fistp(static_cast<float>(effort * kFlatCost));
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
int ReadTerrainTypes(CAIMap& map) {
    Archive archive;
    bool opened;
    {
        mfc::CString folder(*static_cast<const mfc::CString*>(worlddata::TextFolderString()));
        opened = archive.Open(folder + "aiinfo.res");
    }
    if (!opened) return game::FatalError("Unable to read file aiinfo.res");
    void* file = archive.File("tileDesc.reg", nullptr);
    if (!file) return game::FatalError("CAIMap::Load.\n Can't open file \"tileDesc.reg\"");
    Registry registry;
    if (!registry.file.Attach(file)) return game::FatalError("CAIMap::Load.\n Can't open aiReg");
    console::CStringArray sections;
    registry.file.GetSectionNames(&sections);

    map.materials.SetSize(kTerrainTypes);
    for (int i = 0; i < map.materials.size; ++i) {
        AITerrainType& type = map.materials[i];
        type.name[0] = '\0';
        type.cost = 1;
        type.speed = -1;
        type.look = 1.0f;
        type.smell = 1.0f;
        type.noise = 1.0f;
    }
    for (int i = 0; i < sections.size; ++i) {
        const int index = libcrt::Atoi(sections[i].GetBuffer(0));
        {
            mfc::CString name = registry.file.GetString(sections[i], "Name", mfc::CString(""));
            const char* text = name.GetBuffer(0);
            std::memcpy(TerrainTypeAt(&map.materials.data, nullptr, index)->name, text, std::strlen(text) + 1);
        }
        AITerrainType& type = map.materials[index];
        type.cost = registry.file.GetInt2(sections[i], "CostMul", -1);
        type.speed = registry.file.GetInt2(sections[i], "Speed", -1);
        type.look = static_cast<float>(registry.file.GetInt2(sections[i], "StepLook", 100)) * kHundredth;
        type.smell = static_cast<float>(registry.file.GetInt2(sections[i], "StepSmell", 100)) * kHundredth;
        type.noise = static_cast<float>(registry.file.GetInt2(sections[i], "StepSound", 100)) * kHundredth;
    }
    registry.file.Close();
    archive.Close(file);
    archive.Get()->CMappedResource::CloseResource();
    return 1;
}

// ---- The sectors -------------------------------------------------------------------------------------

// A vertex's altitude as a fraction of the map's highest point. The game's constant (0073FD84,
// which the landscape's sector loader uses too) is 1 / 65534 as a float, 2^-16 + 2^-31; not
// 1 / 65535 (X31: with that the ground levels of a few dozen cells per map were one unit off).
// Written as the exact fraction: /fp:fast folds `1.0f / 65534.0f` at double precision wherever the
// constant meets a double, and the heights below are doubles.
constexpr float kAltitudeUnit = 32769.0f / 2147483648.0f;
static_assert(std::bit_cast<std::uint32_t>(kAltitudeUnit) == 0x37800100u);
static_assert(static_cast<double>(kAltitudeUnit) == 32769.0 / 2147483648.0);

// A grid of the map sized for it: reallocated (and filled as its element type's Allocate fills
// it) unless it has that size already.
template <typename T>
void ResizeGrid(SearchGrid<T>& grid, int width, int height, void(__fastcall* allocate)(SearchGrid<T>*)) {
    if (grid.width == width && grid.height == height) return;
    gamecrt::operator_delete(grid.data);
    gamecrt::operator_delete(grid.rows);
    grid.width = width;
    grid.height = height;
    allocate(&grid);
}

// The grids of plain bytes (the marks 005B14C0; the water depths' is the same code inline) are left
// as allocated.
void __fastcall AllocateBytes(SearchGrid<std::int8_t>* grid) { AllocateGrid(*grid); }

constexpr float kHalf = 0.5f;               // 0073B86C
constexpr float kQuarter = 0.25f;           // 0073C3B0

// The heights follow the original's x87 sequence value by value: a float below is one it stores
// (FSTP to a float), a double one it keeps on the FPU stack. At the main thread's 24-bit precision
// the two are the same number; told apart, the results are also the original's at 53 bits (and at
// 64 while the compiler keeps the doubles in registers: nothing unrounded is alive across a call).

// A vertex's height in world units as the original leaves it on the FPU stack: the altitude times
// the map's highest point times the altitude unit, in that order, neither product stored.
__forceinline double VertexHeight(const SectorFileVertex& vertex, float maxZ) {
    return static_cast<double>(vertex.altitude) * maxZ * kAltitudeUnit;
}

// Four floats of a terrain square, one per cell: [0] at its first vertex, [1] towards the next
// vertex of the row, [2] towards the next row, [3] opposite.
struct SquareHeights {
    float cell[4];
};

// The ground's height at the four cells of a terrain square: half of the cell's own corner and a
// quarter of each of the two corners beside it. The original stores the heights of the three other
// corners and keeps the first one, and the quarter of the first and the opposite corner, on the
// stack.
SquareHeights LandHeights(const SectorFileVertex* vertex, float maxZ) {
    const float next = static_cast<float>(VertexHeight(vertex[1], maxZ));
    const float below = static_cast<float>(VertexHeight(vertex[kVertexRow], maxZ));
    const float opposite = static_cast<float>(VertexHeight(vertex[kVertexRow + 1], maxZ));
    const double first = VertexHeight(vertex[0], maxZ);
    const float sides = static_cast<float>((below + next) * kQuarter);
    SquareHeights heights;
    heights.cell[0] = static_cast<float>(first * kHalf + sides);
    const double diagonal = (first + opposite) * kQuarter;
    heights.cell[1] = static_cast<float>(next * kHalf + diagonal);
    heights.cell[2] = static_cast<float>(below * kHalf + diagonal);
    heights.cell[3] = static_cast<float>(opposite * kHalf + sides);
    return heights;
}

// The water's depth over the four cells of a terrain square: its surface, weighted like the ground,
// less the ground `land`. Here the original keeps the third cell's surface on the stack as well
// and subtracts the ground from it there.
SquareHeights WaterDepths(const SectorFileVertex* vertex, float maxZ, const SquareHeights& land) {
    const float next = static_cast<float>(VertexHeight(vertex[1], maxZ));
    const float below = static_cast<float>(VertexHeight(vertex[kVertexRow], maxZ));
    const float opposite = static_cast<float>(VertexHeight(vertex[kVertexRow + 1], maxZ));
    const double first = VertexHeight(vertex[0], maxZ);
    const float sides = static_cast<float>((below + next) * kQuarter);
    const float surface0 = static_cast<float>(first * kHalf + sides);
    const double diagonal = (first + opposite) * kQuarter;
    const float surface1 = static_cast<float>(next * kHalf + diagonal);
    const double surface2 = below * kHalf + diagonal;
    const float surface3 = static_cast<float>(opposite * kHalf + sides);
    SquareHeights depths;
    depths.cell[0] = static_cast<float>(surface0 - land.cell[0]);
    depths.cell[1] = static_cast<float>(surface1 - land.cell[1]);
    depths.cell[2] = static_cast<float>(surface2 - land.cell[2]);
    depths.cell[3] = static_cast<float>(surface3 - land.cell[3]);
    return depths;
}

// A world height in the map's units (FISTP of the float product).
int HeightUnits(float height, float scale) { return x87::fistp(static_cast<float>(height * scale)); }

// The terrain type of a tile word: the type word of its tile, 0xF outside the textures.
std::uint32_t TileType(const MapDescription& map, std::uint16_t tile) {
    const std::uint32_t index = tile & kTileIndexMask;
    const std::uint8_t type =
        index < map.header.textureCount * kTilesPerTexture ? static_cast<std::uint8_t>(map.tileTypes[static_cast<int>(index)]) : kUnknownTileType;
    return type & kTypeMask;
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
void StampSector(CAIMap& map, const MapDescription& description, const void* file, int x0, int y0) {
    const auto* header = static_cast<const SectorHeader*>(file);
    const bool hasWater = (header->layers & 1) != 0;
    const bool hasWaterTiles = (header->layers & 3) != 0;
    const auto* ground = reinterpret_cast<const SectorFileVertex*>(header + 1);
    const SectorFileVertex* water = ground + kVertexRow * kVertexRow;
    const float maxZ = description.header.maxZ;
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
            forTileCells(tx, ty, [type](AIMapCell&, std::uint32_t& occluder) { occluder = (occluder & ~kTypeMask) | type; });
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
                    const bool under = liquid && (cell.flags & kCellDepthMask) != 0;
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
                    if ((cell.flags & kCellWater) && (cell.flags & kCellDepthMask)) occluder = (occluder & ~kTypeMask) | type;
                });
            }
}

} // namespace

int CAIMap::LoadTerrain(const char* path) {
    Archive archive;
    mfc::CString full(path);
    // (The original also builds the path without its extension here and never uses it.)
    if (!archive.Open(full)) return 0;
    console::CStringArray maps;
    archive.Get()->ListFiles(&maps, "*.mp");
    mfc::CString name = maps[0].Left(maps[0].Find('.'));

    MapDescription description;
    if (const int result = ReadDescription(archive, full, name, description); result != 1) return result;
    heightScale = kHeightLevels / description.header.maxZ;
    BuildClimbTables(*this);
    if (const int result = ReadTerrainTypes(*this); result != 1) return result;

    const int cellsX = description.header.sectorsX * kSectorCells;
    const int cellsY = description.header.sectorsY * kSectorCells;
    ResizeGrid(CellGrid(), cellsX, cellsY, AllocateCellGrid);
    ResizeGrid(OccluderGrid(), cellsX, cellsY, AllocateOccluderGrid);
    ResizeGrid(marks, cellsX, cellsY, AllocateBytes);
    for (int sectorY = 0; sectorY < description.header.sectorsY; ++sectorY) {
        for (int sectorX = 0; sectorX < description.header.sectorsX; ++sectorX) {
            char file[432];
            gamecrt::sprintf(file, "%s%3.3i%3.3i%s", name.GetBuffer(0), sectorX, sectorY, ".SEC");
            std::uint32_t size;
            void* sector = archive.File(file, &size);
            if (!sector) return Fatal("Unable to locate resource: " + mfc::CString(file));
            if (!IsSector(sector, size)) return Fatal("Wrong format resource: " + mfc::CString(file));
            StampSector(*this, description, sector, sectorX * kSectorCells, sectorY * kSectorCells);
            archive.Close(sector);
        }
    }
    return 1;
}

void CAIMap::AttachTerrain() {
    loaded = true;
    refreshUnits = false;
    BuildOpenMarks();
    InitCostTables(this);
    SearchGrid<std::int8_t>& water = WaterGrid();
    ResizeGrid(water, width, height, AllocateBytes);
    std::memset(water.data, 0, static_cast<std::size_t>(water.height * water.width));
    forwardSearch.Attach(this, false);
    backwardSearch.Attach(this, true);
    regionMap.Attach(this);
    AIEventsSetupAt(Events(), nullptr, this, static_cast<float>(width) * kCellSize, static_cast<float>(height) * kCellSize);
    SectorMap()->Init(width, height, this);
    QueueRefresh(0, width - 1, 0, height - 1);
}

int CAIMap::Load(const char* path) {
    if (const int result = LoadTerrain(path); result != 1) return result;
    AttachTerrain();
    return 1;
}
EI_REPLACE_METHOD(0x005B43C0, CAIMap::Load);
