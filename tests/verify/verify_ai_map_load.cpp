// Checks for CAIMap::Load (src/game/ai_map_load.cpp, A-W15). The function is Rebirth-patched: the
// wrapper is installed in the vanilla launch only.
//
// The original loads the real map. Before it runs, the map is copied (its 0x26C8 bytes, and deep
// copies of what the first step reads and writes: both climb tables, the terrain types and the
// cells', occluder words' and marks' grids, allocated the way the code frees them), and afterwards
// our LoadTerrain loads the same archive into the copy. Compared: the height scale, both climb
// tables, the sixteen terrain types (name, speed, cost, the three step factors), the three grids'
// sizes and row tables, every cell's ground level and flags and every occluder word's bits 0-4, 7
// and 8 (the terrain type and what the grid's allocator sets). Not compared: a cell's passability,
// level and byte 7, the other occluder bits and the marks (as allocated, or written by refreshes),
// and the terrain types' open marks (the second step writes them).
//
// The second step (AttachTerrain: the open marks, cost tables, water depths, searches, region map,
// events, sector map and the refresh) runs in the original only: it cannot run twice on the real
// map, and every function it calls has its own check. With the refresh deferred (the server's map
// load wraps the load in a batch) the original's second step leaves the cells alone; when it is
// not deferred the flags are skipped, the refresh having rewritten them.
//
// The float steps are the original's x87 sequence in plain C++ (what it stores to a float is
// rounded, what it keeps on the FPU stack is not), so every value is expected to be the same;
// "CAIMap::Load exact" checks that. "CAIMap::Load" allows a height unit or a cost unit of
// difference where a float sat on a rounding edge (and the water flag and water tile type of a
// cell whose depth is on such an edge). X31: the first run failed the exact check on every map (a
// few dozen ground levels one unit off): the altitude unit was taken as 1 / 65535, the game's
// constant 0073FD84 is 1 / 65534.
//
// Reached by CWorldServer::LoadMap: `newgame` and every zone change of a hosted game.
#include "verify.h"

#include "../../src/game/ai_grids.h"
#include "../../src/game/ai_map.h"
#include "../../src/lib/containers.h"
#include "../../src/lib/crt.h"

#include <cstdlib>
#include <cstring>

namespace {

constexpr int kClimbSteps = 0x3FF;
constexpr int kTerrainTypes = 16;
constexpr std::uint8_t kDepthMask = 0x3F;
constexpr std::uint32_t kOccluderBits = 0x19F;

void* NewBytes(std::size_t bytes) { return gamecrt::operator_new(bytes ? bytes : 1); }

// The grid's block and row table replaced by copies.
template <typename T>
void CopyGrid(SearchGrid<T>& grid) {
    const T* data = grid.data;
    T** rows = grid.rows;
    if (!data || !rows) return;
    const std::size_t cells = static_cast<std::size_t>(grid.width) * static_cast<std::size_t>(grid.height);
    grid.data = static_cast<T*>(NewBytes(cells * sizeof(T)));
    std::memcpy(grid.data, data, cells * sizeof(T));
    grid.rows = static_cast<T**>(NewBytes(static_cast<std::size_t>(grid.height) * sizeof(T*)));
    for (int i = 0; i < grid.height; ++i) grid.rows[i] = grid.data + (rows[i] - data);
}

template <typename T>
void FreeGridCopy(SearchGrid<T>& grid) {
    gamecrt::operator_delete(grid.data);
    gamecrt::operator_delete(grid.rows);
}

void CopyClimb(ClimbTable& table) {
    const int* data = table.data;
    if (!data) return;
    const int capacity = table.maxSize > table.size ? table.maxSize : table.size;
    table.data = static_cast<int*>(NewBytes(static_cast<std::size_t>(capacity) * sizeof(int)));
    std::memcpy(table.data, data, static_cast<std::size_t>(table.size) * sizeof(int));
}

// A copy of the map for LoadTerrain: everything the step touches is the copy's own.
class MapCopy {
public:
    explicit MapCopy(const CAIMap* source) {
        std::memcpy(bytes_, source, sizeof(CAIMap));
        CAIMap* map = Map();
        CopyClimb(map->climbLarge);
        CopyClimb(map->climbSmall);
        if (const AITerrainType* types = map->materials.data) {
            const int capacity = map->materials.capacity > map->materials.size ? map->materials.capacity : map->materials.size;
            map->materials.data = lib::AlignedArray<AITerrainType>::Allocate(capacity > 0 ? capacity : 1);
            std::memcpy(map->materials.data, types, static_cast<std::size_t>(map->materials.size) * sizeof(AITerrainType));
        }
        CopyGrid(map->CellGrid());
        CopyGrid(map->OccluderGrid());
        CopyGrid(map->marks);
    }
    ~MapCopy() {
        CAIMap* map = Map();
        gamecrt::operator_delete(map->climbLarge.data);
        gamecrt::operator_delete(map->climbSmall.data);
        map->materials.FreeData();
        FreeGridCopy(map->CellGrid());
        FreeGridCopy(map->OccluderGrid());
        FreeGridCopy(map->marks);
    }
    MapCopy(const MapCopy&) = delete;
    CAIMap* Map() { return reinterpret_cast<CAIMap*>(bytes_); }

private:
    alignas(8) unsigned char bytes_[sizeof(CAIMap)];
};

template <typename T>
bool RowsFit(const SearchGrid<T>& grid) {
    if (!grid.data || !grid.rows) return grid.width * grid.height == 0;
    for (int i = 0; i < grid.height; ++i)
        if (grid.rows[i] != grid.data + i * grid.width) return false;
    return true;
}

template <typename A, typename B>
bool SameSize(const SearchGrid<A>& a, const SearchGrid<B>& b) {
    return a.width == b.width && a.height == b.height;
}

// What differs between the two loads: `wrong` beyond what a rounding edge explains, `edges` within it.
// CompareCells also notes the first cell whose ground level differs.
struct Differences {
    int wrong = 0;
    int edges = 0;
    int firstX = -1, firstY = -1;
    int firstExpected = 0, firstActual = 0;
    void Units(int expected, int actual) {
        const int difference = std::abs(expected - actual);
        if (difference > 1) ++wrong;
        else if (difference == 1) ++edges;
    }
    void Same(bool same) {
        if (!same) ++wrong;
    }
};

Differences CompareClimb(const ClimbTable& expected, const ClimbTable& actual) {
    Differences result;
    if (expected.size != kClimbSteps || actual.size != kClimbSteps || !expected.data || !actual.data) {
        result.wrong = kClimbSteps;
        return result;
    }
    for (int i = 0; i < kClimbSteps; ++i) result.Units(expected.data[i], actual.data[i]);
    return result;
}

Differences CompareTypes(const lib::AlignedArray<AITerrainType>& expected, const lib::AlignedArray<AITerrainType>& actual) {
    Differences result;
    if (expected.size != kTerrainTypes || actual.size != kTerrainTypes) {
        result.wrong = kTerrainTypes;
        return result;
    }
    for (int i = 0; i < kTerrainTypes; ++i) {
        const AITerrainType& a = expected[i];
        const AITerrainType& b = actual[i];
        result.Same(std::memchr(a.name, 0, sizeof(a.name)) && std::memchr(b.name, 0, sizeof(b.name)) &&
                    std::strcmp(a.name, b.name) == 0 && a.speed == b.speed && a.cost == b.cost &&
                    ei::verify::NearlyEqual(a.look, b.look, 4, 1e-6f) && ei::verify::NearlyEqual(a.smell, b.smell, 4, 1e-6f) &&
                    ei::verify::NearlyEqual(a.noise, b.noise, 4, 1e-6f));
    }
    return result;
}

// The cells and occluder words of two maps of the same size. A cell whose depth differs by one may
// also differ in its water flag and its water tile's terrain type.
Differences CompareCells(CAIMap* expected, CAIMap* actual, bool flags) {
    Differences result;
    for (int y = 0; y < expected->height; ++y) {
        for (int x = 0; x < expected->width; ++x) {
            const AIMapCell& a = expected->cells[y][x];
            const AIMapCell& b = actual->cells[y][x];
            result.Units(a.groundLevel, b.groundLevel);
            if (a.groundLevel != b.groundLevel && result.firstX < 0) {
                result.firstX = x;
                result.firstY = y;
                result.firstExpected = a.groundLevel;
                result.firstActual = b.groundLevel;
            }
            bool depthEdge = false;
            if (flags) {
                const int depthA = a.flags & kDepthMask, depthB = b.flags & kDepthMask;
                result.Units(depthA, depthB);
                depthEdge = depthA != depthB;
                if (!depthEdge) result.Same(a.flags == b.flags);
            }
            const bool sameWord = ((expected->occluders[y][x] ^ actual->occluders[y][x]) & kOccluderBits) == 0;
            if (!sameWord && depthEdge) ++result.edges;
            else result.Same(sameWord);
        }
    }
    return result;
}

} // namespace

// 005B43C0: PUSH EBP; MOV EBP,ESP; PUSH -1 (5).
static int(__fastcall* s_load)(CAIMap*, void*, const char*);
static int __fastcall LoadVerify(CAIMap* self, void*, const char* path) {
    const bool deferred = self->batching;
    MapCopy copy(self);
    const int expected = s_load(self, nullptr, path);
    // A failed load is a fatal error (or a missing archive): it is not repeated.
    if (expected != 1) return expected;
    CAIMap* ours = copy.Map();
    const int result = ours->LoadTerrain(path);

    const Differences climbLarge = CompareClimb(self->climbLarge, ours->climbLarge);
    const Differences climbSmall = CompareClimb(self->climbSmall, ours->climbSmall);
    const Differences types = CompareTypes(self->materials, ours->materials);
    const bool sizes = SameSize(self->CellGrid(), ours->CellGrid()) && SameSize(self->OccluderGrid(), ours->OccluderGrid()) &&
                       SameSize(self->marks, ours->marks) && SameSize(ours->CellGrid(), ours->OccluderGrid()) &&
                       SameSize(ours->CellGrid(), ours->marks) && RowsFit(ours->CellGrid()) && RowsFit(ours->OccluderGrid()) &&
                       RowsFit(ours->marks);
    Differences cells;
    if (sizes) cells = CompareCells(self, ours, deferred);
    const bool scale = ei::verify::NearlyEqual(self->heightScale, ours->heightScale, 4, 1e-6f);

    const int wrong = climbLarge.wrong + climbSmall.wrong + types.wrong + cells.wrong;
    const int edges = climbLarge.edges + climbSmall.edges + cells.edges;
    const bool ok = result == 1 && sizes && scale && wrong == 0;
    ei::verify::Check("CAIMap::Load", true, ok,
                      "%s result %d sizes %d (%d x %d) scale %g / %g wrong: climb %d %d types %d cells %d; flags compared %d",
                      path, result, sizes, self->width, self->height, self->heightScale, ours->heightScale, climbLarge.wrong,
                      climbSmall.wrong, types.wrong, cells.wrong, deferred);
    ei::verify::Check("CAIMap::Load exact", true, ok && edges == 0 && self->heightScale == ours->heightScale,
                      "%s on a rounding edge: climb %d %d cells %d; the first ground level at (%d, %d): %d / ours %d; scale %.9g", path,
                      climbLarge.edges, climbSmall.edges, cells.edges, cells.firstX, cells.firstY, cells.firstExpected,
                      cells.firstActual, self->heightScale);
    return expected;
}
EI_VERIFY(0x005B43C0, LoadVerify, 5, s_load);
