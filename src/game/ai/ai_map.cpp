#include "game/ai/ai_map.h"

#include "game/ai/sector_map.h"
#include "engine/core/core_maths.h"
#include "game/objects/map_object.h"

#include "lib/containers.h"
#include "lib/rounding.h"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>
#include <list>
#include <vector>

namespace {

constexpr float kFixedPoint = 65536.0f;
constexpr float kOutsideDistance = 100.0f;

} // namespace

int WorldToCell(float world) {
    float cell = 2.0f * world - 0.5f;
    return lib::RoundEven(cell);
}

Point WorldToCell(const Vector3& position) { return {WorldToCell(position.x), WorldToCell(position.y)}; }

namespace {
constexpr float kSizeClassHeights[8] = /*0079169C*/ {0.4f, 0.4f, 0.4f, 0.4f, 0.4f, 0.7f, 0.7f, 1.5f};

float CellToWorld(int cell) { return (static_cast<float>(cell) + 0.5f) * 0.5f; }

int PositiveOrZero(int v) { return v < 0 ? 0 : v; }

// The occluder records of the sector holding `cell`, for cells holding more than one (occluder::kChained).
const SectorOccluder* OccluderRecords(const AIMap* map, Point cell) {
    const SectorMap& sectors = map->sectorMap;
    return sectors.sectors[(cell.y / 32) * sectors.width + cell.x / 32].occluders.data();
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

// --- Cell queries --------------------------------------------------------------------------------

bool AIMap::CanStandAt(Unit* unit, Point cell, int* reason) {
    if (!InBounds(cell)) return false;
    if (unit->GetClassId() == ClassId::Unit) {
        int level = lib::RoundEven((2.0f - unit->standingHeight) * 16.0f);
        if (level < 0) level = 0;
        *reason = 1;
        if (cells[cell].level > static_cast<std::uint8_t>(level)) return false;
        *reason = 2;
        if (waterDepth[cell] > (level & 0xFF)) return false;
    }
    *reason = 0;
    return true;
}

bool AIMap::CanStandAtFromHere(Unit* unit, Point cell, int* reason) {
    if (!InBounds(cell)) return false;
    int level = PositiveOrZero(lib::RoundEven((2.0f - unit->standingHeight) * 16.0f));
    *reason = 1;
    // The unit's own cell, clamped into the map.
    const Point own = Min(Max(WorldToCell(unit->localPosition), Point{0, 0}), Point{cells.width - 1, cells.height - 1});
    if (cells[cell].level > static_cast<std::uint8_t>(level) && cells[cell].level > cells[own].level) return false;
    level &= 0xFF;
    *reason = 2;
    if (waterDepth[cell] > level && waterDepth[cell] > waterDepth[own]) return false;
    *reason = 0;
    return true;
}

bool AIMap::IsPassableWithoutSelf(Unit* unit, const Vector3& position) {
    if (unit->GetClassId() != ClassId::Unit) return true;
    sectorMap.Remove(unit);
    const Point cell = WorldToCell(position);
    bool passable = true;
    if (unit->GetClassId() == ClassId::Unit) {
        int reason;
        passable = CanStandAt(unit, cell, &reason) && cells[cell].Passability(unit->sizeClass * 4) != 0;
    }
    sectorMap.Add(unit);
    return passable;
}

bool AIMap::IsUnitCellPassable(Unit* unit) {
    const Point cell = WorldToCell(unit->localPosition);
    if (!InBounds(cell)) return false;
    return cells[cell.y][cell.x].Passability(unit->sizeClass * 4) != 0;
}

bool AIMap::IsPassableForClass(std::uint8_t sizeClass, const Vector3& position) {
    const Point cell = WorldToCell(position);
    if (!InBounds(cell)) return false;
    return cells[cell.y][cell.x].Passability(sizeClass * 4) != 0;
}

bool AIMap::CanLeaveCell(Unit* unit) {
    static const int kOffsets[9][2] = {{0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    const Point here = WorldToCell(unit->localPosition);
    if (!InBounds(here)) return false;
    int sizeClass = unit->sizeClass;
    const std::vector<int>& table = sizeClass == 0 ? climbSmall : climbLarge;
    const int* climbCost = table.data() + std::ssize(table) / 2;
    int hereLevel = cells[here.y][here.x].groundLevel;
    for (const auto& o : kOffsets) {
        const Point next = here + Point{o[0], o[1]};
        if (!InBounds(next)) continue;
        const AIMapCell& cell = cells[next.y][next.x];
        if (cell.Passability(sizeClass * 4) == 0) continue;
        if (climbCost[cell.groundLevel - hereLevel] > 0) return true;
    }
    return false;
}

// --- Heights -------------------------------------------------------------------------------------

float AIMap::GroundHeight(Point cell) {
    return static_cast<float>(static_cast<int>(cells[cell].groundLevel)) / heightScale;
}

float AIMap::TopHeightAt(const Vector3& position) {
    const Point at = WorldToCell(position);
    if (!InBounds(at)) return 0.0f;
    const AIMapCell& cell = cells[at.y][at.x];
    return static_cast<float>(cell.WaterDepth() + cell.groundLevel) / heightScale;
}

float AIMap::SlopeAt(const Vector3& position) {
    const Point at = WorldToCell(position);
    const int x = at.x, y = at.y;
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
    const Point cell = WorldToCell(position);
    if (!InBounds(cell)) return false;
    return cells[cell.y][cell.x].InWater();
}

bool AIMap::HasSpellArea(Point cell) {
    std::uint32_t word = occluders[cell];
    if (word & occluder::kNone) return false;
    if (!(word & occluder::kChained)) return occluder::Kind(word) == OccluderKind::SpellArea;
    const SectorOccluder* records = OccluderRecords(this, cell);
    for (int index = occluder::Head(word); index >= 0; index = records[index].next) {
        if (records[index].kind == OccluderKind::SpellArea) return true;
    }
    return false;
}

bool AIMap::HasSpellAreaAt(const Vector3& position) {
    const Point cell = WorldToCell(position);
    if (!InBounds(cell)) return false;
    return HasSpellArea(cell);
}

int AIMap::MaterialAt(const Vector3& position) {
    const Point cell = WorldToCell(position);
    if (!InBounds(cell)) return -1;
    return static_cast<int>(occluder::Type(occluders[cell.y][cell.x]));
}

AITerrainType* AIMap::MaterialRecordAt(const Vector3& position) {
    const Point cell = WorldToCell(position);
    if (!InBounds(cell)) return materials.data();
    return materials.data() + occluder::Type(occluders[cell.y][cell.x]);
}

void AIMap::TurnObject(MapObject* object, const Quaternion* rotation) {
    SectorMap* map = &sectorMap;
    map->Remove(object);
    object->localRotation = *rotation;
    map->Add(object);
}

bool AIMap::BridgeDeckAt(Point cell, int* deckHeight) {
    int deck = 0;
    bool found = false;
    std::uint32_t word = occluders[cell];
    if (!(word & occluder::kNone)) {
        if (word & occluder::kChained) {
            const SectorOccluder* records = OccluderRecords(this, cell);
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
    float length = std::sqrt(LengthSquared(Vector3{dx, dy, dz})) / 0.5f;   // half-cells
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
    int stepZ = lib::RoundEven(static_cast<float>((toZ - (z >> 16)) << 16) * inverse);

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
            if (visibility < kMinVisibility) return kMinVisibility;
            continue;
        }
        // Several occluders: a chain in the sector's record list.
        const SectorOccluder* records = OccluderRecords(this, {cx, cy});
        for (int index = occluder::Head(word); index >= 0; index = records[index].next) {
            const SectorOccluder& o = records[index];
            if (o.top < cz || o.bottom > cz) continue;
            visibility *= kTransparency[o.transparency];
            if (visibility < kMinVisibility) return kMinVisibility;
        }
    }
    return visibility;
}

// --- Free cell search ----------------------------------------------------------------------------

const std::int16_t* SpiralCache::Get(float r, int* count) {
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
    const Point here = WorldToCell(from);
    if (here.x < 1 || here.y < 1 || here.x >= cells.width - 1 || here.y >= cells.height - 1) return false;
    short region = static_cast<short>(regionMap.DominantRegion(sizeClass, here));

    const Point goal = WorldToCell(*target);
    int count;
    float radius;
    std::uint32_t radiusBits = 0x60AD78EC;   // about 1e20: the whole spiral
    radius = std::bit_cast<float>(radiusBits);
    const std::int16_t* offset = cache.Get(radius, &count);
    for (int i = 0; i < count; ++i, offset += 2) {
        const Point cell = goal + Point{offset[0], offset[1]};
        int reason;
        if (!CanStandAt(unit, cell, &reason)) continue;
        if (cells[cell].Passability(sizeClass * 4) == 0) continue;
        short r = static_cast<short>(regionMap.DominantRegion(sizeClass, cell));
        if (region != 0 && r != region) continue;
        if (i != 0) {
            target->x = CellToWorld(cell.x);
            target->y = CellToWorld(cell.y);
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
    Point meet;
    int open = 0, other = 0;
    for (;;) {
        forward->Expand();
        backward->Expand();
        forward->Best(&meet, &open);
        if (open == kUnreached) {
            ok = false;
            break;
        }
        other = backward->CostAt(meet);
        if (other < kUnreached) break;
        backward->Best(&meet, &open);
        if (open == kUnreached) {
            ok = false;
            break;
        }
        other = forward->CostAt(meet);
        if (other < kUnreached) break;
    }
    PathCells scratch;
    if (ok) {
        if (cost) *cost = other + open;
        forward->PathTo(meet, path);
        backward->PathTo(meet, &scratch);
        path->insert(path->end(), scratch.rbegin(), scratch.rend());
        PathCellsRemoveRepeats(path);
    }
    backward->Reset();
    forward->Reset();
    return ok;
}

void AIMap::BuildCellsTo(Point cell, PathCells* path) {
    if (!InBounds(cell)) return;
    // The original takes each cell's speed word and terrain bits from stack memory it never writes (the planner's last
    // call left a search's row offset there); ours get the plain words.
    auto cellAt = [](Point at) {
        return PathCell{static_cast<std::int16_t>(at.x), static_cast<std::int16_t>(at.y), kPlainStepSpeed, 0};
    };
    // Walk back along the marks (directions 1..8, kDirectionDx / Dy; 0 ends); the cells go before the path's, the last
    // one walked first.
    PathCells walked;
    Point at = cell;
    while (int dir = marks[at]) {
        walked.push_back(cellAt(at));
        if (dir >= 1 && dir <= 8) at += Point{kDirectionDx[dir], kDirectionDy[dir]};
    }
    walked.push_back(cellAt(at));
    path->insert(path->begin(), walked.rbegin(), walked.rend());
}

// --- Ai line step ------------------------------------------------------------------------------------

// AIMap::LargestStepAlong: the largest ground height step between consecutive cells on the line from `from`
// to `to`, in world height (the AI height units over heightScale). The line is walked in 16.16 fixed point,
// one step per cell of its 3D length (scaled by 1 / (cell size * 0.5)); 1.0 when that length is 0 (or NaN),
// 100.0 when the first or last cell lies outside the map. See ai_map.h and docs/notes/ai.md.

float AIMap::LargestStepAlong(const Vector3& from, const Vector3& to) {
    Vector3 d = to - from;
    float length = std::sqrt(LengthSquared(d));
    float cell = 0.5f;
    float steps = length / cell;
    // A zero or NaN length also returns 1.0.
    if (steps == 0.0f || steps != steps) return 1.0f;
    float inverse = 1.0f / steps;
    int count = lib::RoundEven(steps);
    float fixed = kFixedPoint;
    auto toFixed = [&](Vector2 value) {
        return Point{lib::RoundEven(value.x * fixed / 0.5f), lib::RoundEven(value.y * fixed / 0.5f)};
    };
    auto cellOf = [](Point fixedPoint) { return Point{fixedPoint.x >> 16, fixedPoint.y >> 16}; };
    const Point start = toFixed(XY(from)), step = toFixed(XY(d) * inverse);
    const Point first = cellOf(start), last = cellOf(step * count + start);
    if (!cells.Contains(first) || !cells.Contains(last)) return kOutsideDistance;

    int previous = cells[first].groundLevel;
    int largest = 0;
    Point at = start + step;
    for (int i = 0; i < count; ++i) {
        int here = cells[cellOf(at)].groundLevel;
        int rise = here - previous;
        if (rise < 0) rise = -rise;
        if (largest <= rise) largest = rise;
        previous = here;
        at += step;
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

// --- Construction ------------------------------------------------------------------------------------

// The AIMap constructor and Reset: every member as a new map has it, the map's own grids 1 x 1 and the area lists
// filled. Of the members only the event table's reset reaches outside the map (its events go back to the free
// list). See ai_map.h and docs/notes/ai.md.

AIMap::AIMap() { Reset(); }

void AIMap::Reset() {
    loaded = false;
    groundFolder.clear();
    batching = false;
    refreshUnits = 0;
    dirty.Reset();
    climbLarge = {};
    climbSmall = {};
    materials = {};
    bridgeHeights = {};
    heightScale = 0.0f;
    sectorMap = {};
    footprint = {};
    bridges = {};
    waterDepth = {};
    waterDepth.Resize(1, 1);
    FillAreaLists(this);
    unitLayerSmall.Clear();
    unitLayerLarge.Clear();
    smoother = {};
    regionMap.Reset();
    forwardSearch = {};
    backwardSearch = {};
    footprintTick = 0;
    cells = {};
    cells.Resize(1, 1);
    occluders = {};
    occluders.Resize(1, 1, occluder::kNone);
    marks = {};
    marks.Resize(1, 1);
    events.Reset();
}
