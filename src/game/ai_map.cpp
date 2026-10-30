#include "ai_map.h"

#include "core_maths.h"
#include "map_object.h"
#include "sector_map.h"
#include "world_data.h"
#include "lib/containers.h"
#include "lib/nan_audit.h"
#include "lib/pc24.h"
#include "lib/rounding.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <list>
#include <vector>

int WorldToCell(float world) {
    float cell = 2.0f * world - 0.5f;
    return lib::RoundEven(cell);
}

namespace {
constexpr float kSizeClassHeights[8] = /*0079169C*/ {0.4f, 0.4f, 0.4f, 0.4f, 0.4f, 0.7f, 0.7f, 1.5f};

float CellToWorld(int cell) { return (static_cast<float>(cell) + 0.5f) * 0.5f; }

int PositiveOrZero(int v) { return v < 0 ? 0 : v; }

// The occluder records of the sector holding cell (x, y), for cells holding more than one (occluder::kChained).
const SectorOccluder* OccluderRecords(const AIMap* map, int x, int y) {
    const SectorMap& sectors = map->sectorMap;
    return sectors.sectors[(y / 32) * sectors.width + x / 32].occluders.data();
}

constexpr float kTransparency[4] = /*00791744*/ {0.0f, 0.98f, 0.99f, 1.0f};
constexpr float kMinVisibility = 0.0001f;

// The spiral of cell offsets ordered by distance (sector_map.h's tables, built on first use): a binary tree
// mapping a radius (compared as raw float bits) to the number of offsets within it.
const std::int16_t* SpiralOffsets() { return &SectorSpiralTables().offsets.data()->x; }

} // namespace

int LevelForSizeClass(int sizeClass) {
    if (sizeClass < 0 || sizeClass >= 8) return sizeClass & ~0xFF;   // XOR AL,AL
    return PositiveOrZero(lib::RoundEven((2.0f - kSizeClassHeights[sizeClass]) * 16.0f));
}

int StandingLevel(const Unit* unit) {
    return PositiveOrZero(lib::RoundEven((2.0f - unit->standingHeight) * 16.0f));
}

int BridgeHeights::HeightAt(short x, short y) const {
    auto node = saved.find(static_cast<std::uint32_t>(x * 0x10000 + y));   // the key is (short)x << 16 | (short)y
    return node != saved.end() ? static_cast<std::uint16_t>(node->second) : 0;
}

// --- Cell queries --------------------------------------------------------------------------------

bool AIMap::CanStandAt(Unit* unit, int x, int y, int* reason) {
    if (x < 0 || x >= cells.width || y < 0 || y >= cells.height) return false;
    if (unit->GetClassId() == ClassId::Unit) {
        int level = lib::RoundEven((2.0f - unit->standingHeight) * 16.0f);
        if (level < 0) level = 0;
        *reason = 1;
        if (cells[y][x].level > static_cast<std::uint8_t>(level)) return false;
        *reason = 2;
        if (waterDepth[y][x] > (level & 0xFF)) return false;
    }
    *reason = 0;
    return true;
}

bool AIMap::CanStandAtFromHere(Unit* unit, int x, int y, int* reason) {
    if (x < 0 || x >= cells.width || y < 0 || y >= cells.height) return false;
    int level = PositiveOrZero(lib::RoundEven((2.0f - unit->standingHeight) * 16.0f));
    *reason = 1;
    // The unit's own cell is not bounds-checked, like the original.
    if (cells[y][x].level > static_cast<std::uint8_t>(level)) {
        int ux = WorldToCell(unit->localPosition.x), uy = WorldToCell(unit->localPosition.y);
        if (cells[y][x].level > cells[uy][ux].level) return false;
    }
    level &= 0xFF;
    *reason = 2;
    if (waterDepth[y][x] > level) {
        int ux = WorldToCell(unit->localPosition.x), uy = WorldToCell(unit->localPosition.y);
        if (waterDepth[y][x] > waterDepth[uy][ux]) return false;
    }
    *reason = 0;
    return true;
}

bool AIMap::IsPassableWithoutSelf(Unit* unit, const Vector3& position) {
    if (unit->GetClassId() != ClassId::Unit) return true;
    sectorMap.Remove(unit);
    int y = WorldToCell(position.y);
    int x = WorldToCell(position.x);
    bool passable = true;
    if (unit->GetClassId() == ClassId::Unit) {
        int reason;
        passable = CanStandAt(unit, x, y, &reason) &&
                   cells[y][x].Passability(unit->sizeClass * 4) != 0;
    }
    sectorMap.Add(unit);
    return passable;
}

bool AIMap::IsUnitCellPassable(Unit* unit) {
    int x = WorldToCell(unit->localPosition.x), y = WorldToCell(unit->localPosition.y);
    if (x < 0 || y < 0 || x >= cells.width || y >= cells.height) return false;
    return cells[y][x].Passability(unit->sizeClass * 4) != 0;
}

bool AIMap::IsPassableForClass(std::uint8_t sizeClass, const Vector3& position) {
    int x = WorldToCell(position.x), y = WorldToCell(position.y);
    if (x < 0 || y < 0 || x >= cells.width || y >= cells.height) return false;
    return cells[y][x].Passability(sizeClass * 4) != 0;
}

bool AIMap::CanLeaveCell(Unit* unit) {
    static const int kOffsets[9][2] = {{0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    int x = WorldToCell(unit->localPosition.x), y = WorldToCell(unit->localPosition.y);
    if (x < 0 || y < 0 || x >= cells.width || y >= cells.height) return false;
    int sizeClass = unit->sizeClass;
    const std::vector<int>& table = sizeClass == 0 ? climbSmall : climbLarge;
    const int* climbCost = table.data() + std::ssize(table) / 2;
    int here = cells[y][x].groundLevel;
    for (const auto& o : kOffsets) {
        int nx = o[0] + x, ny = y + o[1];
        if (nx < 0 || ny < 0 || nx >= cells.width || ny >= cells.height) continue;
        const AIMapCell& cell = cells[ny][nx];
        if (cell.Passability(sizeClass * 4) == 0) continue;
        if (climbCost[cell.groundLevel - here] > 0) return true;
    }
    return false;
}

// --- Heights -------------------------------------------------------------------------------------

float AIMap::GroundHeight(int x, int y) {
    return static_cast<float>(static_cast<int>(cells[y][x].groundLevel)) / heightScale;
}

float AIMap::TopHeightAt(const Vector3& position) {
    int x = WorldToCell(position.x), y = WorldToCell(position.y);
    if (x < 0 || y < 0 || x >= cells.width || y >= cells.height) return 0.0f;
    const AIMapCell& cell = cells[y][x];
    return static_cast<float>(cell.WaterDepth() + cell.groundLevel) / heightScale;
}

float AIMap::SlopeAt(const Vector3& position) {
    int x = WorldToCell(position.x), y = WorldToCell(position.y);
    if (x < 1 || y < 1 || x > cells.width - 2 || y > cells.height - 2) return 0.0f;
    int here = cells[y][x].groundLevel;
    auto step = [here](int level) {
        int d = level - here;
        return d < 0 ? -d : d;
    };
    int slope = 0;
    int d = step(cells[y + 1][x].groundLevel);
    if (d >= 0) slope = d;
    d = step(cells[y - 1][x].groundLevel);
    if (!(slope > d)) slope = d;
    d = step(cells[y][x + 1].groundLevel);
    if (!(slope > d)) slope = d;
    d = step(cells[y][x - 1].groundLevel);
    if (!(slope > d)) slope = d;
    return static_cast<float>(slope) / heightScale;
}

bool AIMap::InWaterAt(const Vector3& position) {
    int x = WorldToCell(position.x), y = WorldToCell(position.y);
    if (x < 0 || y < 0 || x >= cells.width || y >= cells.height) return false;
    return cells[y][x].InWater();
}

bool AIMap::HasSpellArea(int x, int y) {
    std::uint32_t word = occluders[y][x];
    if (word & occluder::kNone) return false;
    if (!(word & occluder::kChained)) return occluder::Kind(word) == OccluderKind::SpellArea;
    const SectorOccluder* records = OccluderRecords(this, x, y);
    for (int index = occluder::Head(word); index >= 0; index = records[index].next) {
        if (records[index].kind == OccluderKind::SpellArea) return true;
    }
    return false;
}

bool AIMap::HasSpellAreaAt(const Vector3& position) {
    int x = WorldToCell(position.x), y = WorldToCell(position.y);
    if (x < 0 || y < 0 || x >= cells.width || y >= cells.height) return false;
    return HasSpellArea(x, y);
}

int AIMap::MaterialAt(const Vector3& position) {
    int x = WorldToCell(position.x), y = WorldToCell(position.y);
    if (x < 0 || y < 0 || x >= cells.width || y >= cells.height) return -1;
    return static_cast<int>(occluder::Type(occluders[y][x]));
}

AITerrainType* AIMap::MaterialRecordAt(const Vector3& position) {
    int x = WorldToCell(position.x), y = WorldToCell(position.y);
    if (x < 0 || y < 0 || x >= cells.width || y >= cells.height) return materials.data();
    return materials.data() + occluder::Type(occluders[y][x]);
}

void AIMap::TurnObject(MapObject* object, const Quaternion* rotation) {
    SectorMap* map = &sectorMap;
    map->Remove(object);
    object->localRotation = *rotation;
    map->Add(object);
}

bool AIMap::BridgeDeckAt(int x, int y, int* deckHeight) {
    int deck = 0;
    bool found = false;
    std::uint32_t word = occluders[y][x];
    if (!(word & occluder::kNone)) {
        if (word & occluder::kChained) {
            const SectorOccluder* records = OccluderRecords(this, x, y);
            for (int index = occluder::Head(word); index >= 0; index = records[index].next) {
                if (records[index].kind == OccluderKind::BridgeDeck) {
                    if (!(deck > records[index].top)) deck = records[index].top;
                    found = true;
                }
            }
        } else if (occluder::Kind(word) == OccluderKind::BridgeDeck) {
            deck = occluder::Top(word);
            found = true;
        }
    }
    if (deckHeight) *deckHeight = deck;
    return found;
}

// --- Line of sight -------------------------------------------------------------------------------

float AIMap::LineOfSight(const Vector3& from, const Vector3& to) {
    float dx = to.x - from.x, dy = to.y - from.y;
    float dz = to.z - from.z;
    float length = std::sqrt(LengthSquaredUnrounded<Sum3::ZYX>(dx, dy, dz)) / 0.5f;   // half-cells
    EI_NAN_AUDIT(length);
    if (length == 0.0f) return 1.0f;
    float inverse = 1.0f / length;
    int steps = lib::RoundEven(length);

    // 16.16 fixed point position and step, in half-cells (x, y) and AI height units (z).
    int x = lib::RoundEven(from.x * 65536.0f / 0.5f);
    int y = lib::RoundEven(from.y * 65536.0f / 0.5f);
    int z = lib::RoundEven(from.z * heightScale) << 16;
    int stepX = lib::RoundEven(dx * inverse * 65536.0f / 0.5f);
    int stepY = lib::RoundEven(dy * inverse * 65536.0f / 0.5f);
    int toZ = lib::RoundEven(to.z * heightScale);
    int stepZ = lib::RoundEven(pc24::Mul(static_cast<double>((toZ - (z >> 16)) << 16), inverse));

    auto outside = [this](int fx, int fy) {
        int cx = fx >> 16, cy = fy >> 16;
        return cx < 0 || cy < 0 || cx >= cells.width || cy >= cells.height;
    };
    if (outside(x, y) || outside(stepX * steps + x, stepY * steps + y)) return 0.0f;

    float visibility = 1.0f;
    x += stepX;
    y += stepY;
    z += stepZ;
    for (int i = 0; i < steps; ++i, x += stepX, y += stepY, z += stepZ) {
        int cx = x >> 16, cy = y >> 16, cz = z >> 16;
        if (cells[cy][cx].groundLevel >= cz) return 0.0f;

        std::uint32_t word = occluders[cy][cx];
        if (word & occluder::kNone) continue;
        if (!(word & occluder::kChained)) {
            // A single occluder packed into the word.
            int high = occluder::Top(word), low = occluder::Bottom(word);
            if (high < cz || low > cz) continue;
            visibility *= kTransparency[occluder::Transparency(word)];
            EI_NAN_AUDIT(visibility);
            if (visibility < kMinVisibility) return kMinVisibility;
            continue;
        }
        // Several occluders: a chain in the sector's record list.
        const SectorOccluder* records = OccluderRecords(this, cx, cy);
        for (int index = occluder::Head(word); index >= 0; index = records[index].next) {
            const SectorOccluder& o = records[index];
            if (o.top < cz || o.bottom > cz) continue;
            visibility *= kTransparency[o.transparency];
            EI_NAN_AUDIT(visibility);
            if (visibility < kMinVisibility) return kMinVisibility;
        }
    }
    return visibility;
}

// --- Free cell search ----------------------------------------------------------------------------

const std::int16_t* SpiralCache::Get(float r, int* count) {
    EI_NAN_AUDIT(r, radius);
    if (r == radius) {
        *count = length;
        return SpiralOffsets();
    }
    const SectorSpiral& spiral = SectorSpiralTables();
    int found = spiral.lengths[spiral.radii.Find(r)];
    *count = found;
    length = found;
    radius = r;
    return SpiralOffsets();
}

bool AIMap::FindFreeCellNear(Unit* unit, const Vector3& from, Vector3* target) {
    static SpiralCache cache{0, -1.0f};

    int sizeClass = unit->sizeClass;
    int x = WorldToCell(from.x), y = WorldToCell(from.y);
    if (x < 1 || y < 1 || x >= cells.width - 1 || y >= cells.height - 1) return false;
    short region = static_cast<short>(regionMap.DominantRegion(sizeClass, x, y));

    int tx = WorldToCell(target->x), ty = WorldToCell(target->y);
    int count;
    float radius;
    std::uint32_t radiusBits = 0x60AD78EC;   // about 1e20: the whole spiral
    radius = std::bit_cast<float>(radiusBits);
    const std::int16_t* offset = cache.Get(radius, &count);
    for (int i = 0; i < count; ++i, offset += 2) {
        int cx = offset[0] + tx, cy = offset[1] + ty;
        int reason;
        if (!CanStandAt(unit, cx, cy, &reason)) continue;
        if (cells[cy][cx].Passability(sizeClass * 4) == 0) continue;
        short r = static_cast<short>(regionMap.DominantRegion(sizeClass, cx, cy));
        if (region != 0 && r != region) continue;
        if (i != 0) {
            target->x = CellToWorld(cx);
            target->y = CellToWorld(cy);
        }
        return true;
    }
    return false;
}

// --- Ai join -----------------------------------------------------------------------------------------

// AIMap::JoinLocalSearches: the bidirectional local search's meeting. The forward and backward searches
// expand in turn; after each round the best open cell of one side is looked up in the other, first forward then
// backward, until a cell reached by both is found (or a side has nothing left: false). Then the forward
// search's path to the cell goes into `path`, the backward one's into a scratch list, which is reversed and
// appended, and the result fixed up; `cost` (optional) gets the summed cost. Both searches are reset either
// way. See ai_map.h and docs/notes/ai.md.

namespace {

// The local search: expand one round, the best open cell (x, y, cost), the cost of a cell (0x7FFFFFFF
// unreached), the path to a cell into a list, reset. The joined path: the backward search's cells appended in
// reverse, then the fix-up.

constexpr int kUnreached = 0x7FFFFFFF;

} // namespace

bool AIMap::JoinLocalSearches(PathCells* path, int* cost) {
    LocalSearch* forward = &forwardSearch;
    LocalSearch* backward = &backwardSearch;
    bool ok = true;
    int x = 0, y = 0, open = 0, other = 0;
    for (;;) {
        forward->Expand();
        backward->Expand();
        forward->Best(&x, &y, &open);
        if (open == kUnreached) {
            ok = false;
            break;
        }
        other = backward->CostAt(x, y);
        if (other < kUnreached) break;
        backward->Best(&x, &y, &open);
        if (open == kUnreached) {
            ok = false;
            break;
        }
        other = forward->CostAt(x, y);
        if (other < kUnreached) break;
    }
    PathCells scratch;
    if (ok) {
        if (cost) *cost = other + open;
        forward->PathTo(x, y, path);
        backward->PathTo(x, y, &scratch);
        path->insert(path->end(), scratch.rbegin(), scratch.rend());
        PathCellsRemoveRepeats(path);
        // Like the original, the cells keep the stack word under SearchPathTo's local cell (the forward search's
        // address); ours get the plain words.
        SetPlainCellWords(path);
    }
    backward->Reset();
    forward->Reset();
    return ok;
}

void AIMap::BuildCellsTo(int x, int y, PathCells* path) {
    if (x < 0 || x >= cells.width || y < 0 || y >= cells.height) return;
    // The original takes each cell's speed word and terrain bits from stack memory it never writes (the planner's last
    // call left a search's row offset there); ours get the plain words.
    auto cellAt = [](int cx, int cy) {
        PathCell c{static_cast<std::int16_t>(cx), static_cast<std::int16_t>(cy), kPlainStepSpeed, 0};
        return c;
    };
    // Walk back along the marks (directions 1..8; 0 ends); the cells go before the path's, the last one walked first.
    PathCells walked;
    while (int dir = marks[y][x]) {
        walked.push_back(cellAt(x, y));
        switch (dir) {
        case 1: --y; break;
        case 2: --y; --x; break;
        case 3: --x; break;
        case 4: ++y; --x; break;
        case 5: ++y; break;
        case 6: ++y; ++x; break;
        case 7: ++x; break;
        case 8: --y; ++x; break;
        default: break;
        }
    }
    walked.push_back(cellAt(x, y));
    path->insert(path->begin(), walked.rbegin(), walked.rend());
}

// --- Ai line step ------------------------------------------------------------------------------------

// AIMap::LargestStepAlong: the largest ground height step between consecutive cells on the line from `from`
// to `to`, in world height (the AI height units over heightScale). The line is walked in 16.16 fixed point,
// one step per cell of its 3D length (scaled by 1 / (cell size * 0.5)); 1.0 when that length is 0 (or NaN),
// 100.0 when the first or last cell lies outside the map. See ai_map.h and docs/notes/ai.md.

float AIMap::LargestStepAlong(const Vector3& from, const Vector3& to) {
    float dx = to.x - from.x;
    float dy = to.y - from.y;
    float dz = to.z - from.z;
    float length = std::sqrt(LengthSquaredUnrounded<Sum3::ZYX>(dx, dy, dz));
    float cell = 0.5f;
    float steps = length / cell;
    // A zero or NaN length also returns 1.0.
    if (steps == 0.0f || steps != steps) return 1.0f;
    float inverse = 1.0f / steps;
    int count = lib::RoundEven(steps);
    float fixed = worlddata::kFixedPoint;
    auto toFixed = [&](float value) { return lib::RoundEven(value * fixed / 0.5f); };
    int fx = toFixed(from.x);
    int fy = toFixed(from.y);
    int stepX = toFixed(dx * inverse);
    int stepY = toFixed(dy * inverse);

    int cx = fx >> 16, cy = fy >> 16;
    if (cx < 0 || cy < 0 || cx >= cells.width || cy >= cells.height) return worlddata::kOutsideDistance;
    int ex = (stepX * count + fx) >> 16, ey = (stepY * count + fy) >> 16;
    if (ex < 0 || ey < 0 || ex >= cells.width || ey >= cells.height) return worlddata::kOutsideDistance;

    int previous = cells[cy][cx].groundLevel;
    int largest = 0;
    int px = fx + stepX, py = fy + stepY;
    for (int i = 0; i < count; ++i) {
        int here = cells[py >> 16][px >> 16].groundLevel;
        int step = here - previous;
        if (step < 0) step = -step;
        if (largest <= step) largest = step;
        previous = here;
        px += stepX;
        py += stepY;
    }
    return static_cast<float>(largest) / heightScale;
}

// --- Ai area lists -----------------------------------------------------------------------------------

// Fills the six per-level area lists of AIMap (arrays of {int, int}) from their tables in the game's data, each
// a run of {a, b} pairs ended by a = 100. The last list appends through its Add, the others through SetSize.
// See ai_map.h and docs/notes/ai.md.

namespace {

constexpr int kLevel0Table[12] = /*007912DC*/ {
    0, 0, 1, 0, -1, 0, 0, 1, 0, -1, 100, 100};
constexpr int kLevel1Table[28] = /*0079130C*/ {
    -2, 0, -1, -1, -1, 0, -1, 1, 0, -2, 0, -1, 0, 0, 0, 1, 0, 2, 1, -1, 1, 0, 1, 1, 2, 0, 100, 100};
constexpr int kLevel2Table[60] = /*0079137C*/ {
    -3, 0, -2, -2, -2, -1, -2, 0, -2, 1, -2, 2, -1, -2, -1, -1, -1, 0, -1, 1, -1, 2, 0, -3, 0, -2, 0, -1, 0, 0, 0,
    1, 0, 2, 0, 3, 1, -2, 1, -1, 1, 0, 1, 1, 1, 2, 2, -2, 2, -1, 2, 0, 2, 1, 2, 2, 3, 0, 100, 100};
constexpr int kLevel3Table[20] = /*0079146C*/ {
    -1, -1, -1, 0, -1, 1, 0, -1, 0, 0, 0, 1, 1, -1, 1, 0, 1, 1, 100, 100};
constexpr int kLevel4Table[44] = /*007914BC*/ {
    -2, -1, -2, 0, -2, 1, -1, -2, -1, -1, -1, 0, -1, 1, -1, 2, 0, -2, 0, -1, 0, 0, 0, 1, 0, 2, 1, -2, 1, -1, 1, 0,
    1, 1, 1, 2, 2, -1, 2, 0, 2, 1, 100, 100};
constexpr int kLevel5Table[76] = /*0079156C*/ {
    -3, -1, -3, 0, -3, 1, -2, -2, -2, -1, -2, 0, -2, 1, -2, 2, -1, -3, -1, -2, -1, -1, -1, 0, -1, 1, -1, 2, -1, 3,
    0, -3, 0, -2, 0, -1, 0, 0, 0, 1, 0, 2, 0, 3, 1, -3, 1, -2, 1, -1, 1, 0, 1, 1, 1, 2, 1, 3, 2, -2, 2, -1, 2, 0,
    2, 1, 2, 2, 3, -1, 3, 0, 3, 1, 100, 100};
constexpr const int* kTables[6] = {kLevel0Table, kLevel1Table, kLevel2Table, kLevel3Table, kLevel4Table, kLevel5Table};
constexpr int kEnd = 100;

} // namespace

void FillAreaLists(AIMap* map) {
    for (int i = 0; i < 6; ++i) {
        std::vector<AreaOffset>* list = &map->areaLists[i];
        list->clear();
        for (const int* entry = kTables[i]; entry[0] != kEnd; entry += 2) {
            AreaOffset pair = {entry[0], entry[1]};
            if (i == 5) {
                list->push_back(pair);
                continue;
            }
            int n = lib::Count(*list);
            list->resize(n + 1);
            (*list)[n].dx = pair.dx;
            (*list)[n].dy = pair.dy;
        }
    }
}

// --- Ai cost tables ----------------------------------------------------------------------------------

// The three passability cost tables of AIMap, 16 ints each indexed by a cell's passability nibble (entry 0,
// impassable, is -1): the per-nibble costs, a flat 0x400, and the step costs (PassCostContext and
// SetStepSpeeds use them). See ai_map.h.

namespace {
constexpr int kCosts[16] = {-1,    0x6400, 0x77EC, 0x8BD8, 0x2FF8, 0x37F0, 0x3FFC, 0x0DFC,
                            0x0FFF, 0x11FD, 0x1400, 0x0666, 0x0732, 0x0800, 0x0400, 0x04CC};
constexpr int kStepCosts[16] = {-1,    0x200, 0x266, 0x2CC, 0x266, 0x2CC, 0x333, 0x2CC,
                                0x333, 0x399, 0x400, 0x333, 0x399, 0x400, 0x400, 0x4CC};
} // namespace

void InitCostTables(AIMap* map) {
    int* costs = map->passCosts;
    int* flat = map->passCostsAlt;
    int* steps = map->stepCosts;
    for (int i = 0; i < 16; ++i) {
        costs[i] = kCosts[i];
        flat[i] = i == 0 ? -1 : 0x400;
        steps[i] = kStepCosts[i];
    }
}

// --- Script runtime helpers --------------------------------------------------------------------------

// The AI map's sector map wrappers the script commands reach.

// ---- The AI map's sector map ------------------------------------------------------------------------

namespace {
SectorMap* Sectors(AIMap* map) { return &map->sectorMap; }
} // namespace

std::uint8_t AIMap::SetRefreshUnits(int on) {
    std::uint8_t& flag = refreshUnits;
    std::uint8_t old = flag;
    flag = static_cast<std::uint8_t>(on);
    return old;
}

bool AIMap::AddToSectors(MapObject* object) { return Sectors(this)->Add(object); }

bool AIMap::RemoveFromSectors(MapObject* object) { return Sectors(this)->Remove(object); }

bool AIMap::MoveInSectors(MapObject* object, const Vector3& position) {
    return Sectors(this)->Move(object, position);
}

std::vector<Unit*> AIMap::FindUnitsInRadius(const Vector3& position, float radius) {
    return Sectors(this)->FindUnitsInRadius(position, radius);
}
