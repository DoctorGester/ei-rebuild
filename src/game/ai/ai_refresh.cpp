// AIMap::Refresh: recomputes the passability of a rectangle of cells after the map changed: per standing level
// the cell marks over the rectangle grown by 3 cells, the level's special areas, the second pass and the map
// border; with the sector map on the objects around it are taken out and put back. See docs/notes/ai.md.
#include "game/ai/ai_map.h"

#include "engine/core/core_maths.h"
#include "game/ai/sector_map.h"

#include <cstdint>

#include "lib/maths.h"
#include "lib/rounding.h"

#include <vector>

namespace {

// Per occluder type the cost added by full cover and the damping exponent.
constexpr int kOccluderCosts[3] = /*007912A4*/ {290, 20};
constexpr int kOccluderDamping[3] = /*007912B0*/ {7, 2};

constexpr int kLevels = 8;

// The occluders' damping scale, ln 0.6.
float g_dampScale = /*007C2F8C*/ {-0.51082563f};
// The special areas of a standing level: levels 5 and 6 use the second list, 7 the third, the
// others the first.
int AreaList(int level) {
    if (level >= 5 && level <= 6) return 1;
    if (level == 7) return 2;
    return 0;
}

std::uint32_t& CellWord(AIMap* map, int x, int y) {
    return map->cells[y][x].passable;
}

// passage * e^(table * a * b), rounded to an int.
int Damp(int table, float a, float b, int passage) {
    const float damped = lib::Exp(static_cast<float>(table) * a * b) * static_cast<float>(passage);
    return lib::RoundEven(damped);
}

int TableInt(const int* table, int i) { return table[i]; }

// One occluder of kind `type` spanning heights [lowest, highest] against the cell's column.
// `sectorOrder`: the order of the two factors of the exponent (the two callers differ).
void Occlude(CellMarkState* s, OccluderKind type, int lowest, int highest, bool sectorOrder) {
    switch (type) {
    case OccluderKind::Crown:
    case OccluderKind::BridgeDeck:
        return;
    case OccluderKind::SpellArea:
        s->doubled = 1;
        return;
    default:
        break;
    }
    if (s->passage < 0 || lowest > s->top || highest < s->bottom) return;
    int lo = lowest > s->bottom ? lowest : s->bottom;
    int hi = highest < s->top ? highest : s->top;
    float frac = static_cast<float>(hi - lo) / static_cast<float>(s->top - s->bottom);
    float added = static_cast<float>(TableInt(kOccluderCosts, static_cast<int>(type))) * frac;
    s->cost += lib::RoundEven(added);
    float scale = g_dampScale;
    s->passage = sectorOrder ? Damp(TableInt(kOccluderDamping, static_cast<int>(type)), scale, frac, s->passage)
                             : Damp(TableInt(kOccluderDamping, static_cast<int>(type)), frac, scale, s->passage);
}

// 17 cell offsets {dy, dx}; the first 4 the direct neighbours, the other 13 the cells
// whose nibble 7 a steep cell clears.
struct CellOffset {
    int dy = 0, dx = 0;
};
constexpr CellOffset kSteepOffsets[17] = /*007916BC*/ {
    {0, 1}, {0, -1}, {1, 0}, {-1, 0}, {0, 2}, {-1, 1}, {0, 1}, {1, 1},
    {-2, 0}, {-1, 0}, {0, 0}, {1, 0}, {2, 0}, {-1, -1}, {0, -1}, {1, -1}, {0, -2}};

// The largest climbable step of a climb table: the run of non-negative costs from the middle, less one.
int MaxStep(const std::vector<int>& table) {
    int n = 0;
    while (table[std::ssize(table) / 2 + n] > -1) ++n;
    return n - 1;
}

} // namespace

void AIMap::RefreshArea(Point first, Point last) {
    if (first.x > last.x || first.y > last.y) return;
    // The cells refreshed, and the ones whose marks are measured (3 more each way); both clamped to the map.
    const Point mapLast{cells.width - 1, cells.height - 1};
    first = Max(first, Point{});
    last = Min(last, mapLast);
    const Point outerFirst = Max(first - Point{3, 3}, Point{});
    const Point outerLast = Min(last + Point{3, 3}, mapLast);

    CellMarkState scratch;
    for (int level = 0; level < kLevels; ++level) {
        float h = kLevelHeights[level] * heightScale;
        int levelHeight = lib::RoundEven(h);
        for (int y = outerFirst.y; y <= outerLast.y; ++y)
            for (int x = outerFirst.x; x <= outerLast.x; ++x) {
                std::int8_t* row = marks[y];
                row[x] = static_cast<std::int8_t>(CellMark(&scratch, level, levelHeight, {x, y}));
            }
        int shift = level * 4;
        std::uint32_t keep = ~(0xFu << shift);
        for (int y = first.y; y <= last.y; ++y)
            for (int x = first.x; x <= last.x; ++x) {
                CellWord(this, x, y) &= keep;
                int mark = marks[y][x];
                CellWord(this, x, y) |= static_cast<std::uint32_t>(mark << shift);
            }
        const std::vector<AreaOffset>& areas = areaLists[AreaList(level)];
        for (int i = 0; i < std::ssize(areas); ++i) {
            const AreaOffset& entry = areas[i];
            ClearUnmarkedArea(level, {entry.dx, entry.dy}, first, last);
        }
    }
    LimitSteepCells(first, last);

    // The three outermost rows and columns are never passable.
    for (int x = outerFirst.x; x <= outerLast.x; ++x) {
        CellWord(this, x, 0) = 0;
        CellWord(this, x, cells.height - 1) = 0;
        CellWord(this, x, 1) = 0;
        CellWord(this, x, cells.height - 2) = 0;
        CellWord(this, x, 2) = 0;
        CellWord(this, x, cells.height - 3) = 0;
    }
    for (int y = outerFirst.y; y <= outerLast.y; ++y) {
        CellWord(this, 0, y) = 0;
        CellWord(this, cells.width - 1, y) = 0;
        CellWord(this, 1, y) = 0;
        CellWord(this, cells.width - 2, y) = 0;
        CellWord(this, 2, y) = 0;
        CellWord(this, cells.width - 3, y) = 0;
    }

    if (!refreshUnits) return;
    // The blocks from the one before the area's first to the one after its last (right and bottom outside).
    const Rect blocks = Rect::FromCorners(outerFirst / 8 - Point{1, 1}, outerLast / 8 + Point{1, 1});
    float halfW = static_cast<float>(outerLast.x - outerFirst.x) * 0.5f + 1.0f;
    float halfH = static_cast<float>(outerLast.y - outerFirst.y) * 0.5f + 1.0f;
    float scale = 0.5f;
    float radius = std::sqrt(halfW * halfW + halfH * halfH) * scale;
    Vector3 centre;   // x and y (FindUnitsInRadius reads no more)
    float cx = static_cast<float>(outerFirst.x + outerLast.x) * scale * 0.5f;
    float cy = static_cast<float>(outerFirst.y + outerLast.y) * scale * 0.5f;
    centre.x = cx;
    centre.y = cy;
    SectorMap* sectors = &sectorMap;
    std::vector<Unit*> objects = sectors->FindUnitsInRadius(centre, radius);
    for (Unit* object : objects) sectors->Remove(object);
    regionMap.UpdateBlocks(this, blocks);
    for (Unit* object : objects) sectors->Add(object);
}

// ---- CellMark ---------------------------------------------------------------------------------

std::uint32_t AIMap::CellMark(CellMarkState* s, int level, int levelHeight, Point at) {
    AIMapCell* cell = &cells[at];
    std::uint32_t* occluder = &occluders[at];
    AITerrainType* terrain = &materials[occluder::Type(*occluder)];
    const int depth = cell->WaterDepth();   // the original reads the flags as a word and keeps the low six bits
    if ((*occluder & occluder::kNone) && depth == 0) return (terrain->openMarks >> (level * 4)) & kPassabilityMask;

    int bottom = cell->groundLevel;
    if (level == 0) {
        bottom += depth;
        s->doubled = 0;
        s->passage = 0x400;
        s->cost = 1;
        s->bottom = bottom;
        s->top = bottom + levelHeight;
        s->ceiling = 0x7F8;
    } else {
        int base = terrain->cost;
        int ceiling = level != 1 ? levelHeight : 0;
        s->doubled = 0;
        s->passage = 0x400;
        s->cost = base;
        s->bottom = bottom;
        s->top = levelHeight + bottom;
        s->ceiling = ceiling;
        if (depth != 0) {
            if (depth == kCellDepthMask || depth >= ceiling - 1) {
                s->passage = -1;
            } else if (ceiling < 0x400) {
                if (depth > ceiling / 2) {
                    s->passage = 0x320;
                    s->cost = base + 30;
                } else {
                    s->passage = 0x370;
                }
            }
        }
    }

    std::uint32_t word = *occluder;
    if (!(word & occluder::kNone)) {
        if (word & occluder::kChained) {
            const Sector& sector = sectorMap.sectors[(at.y / 32) * sectorMap.width + at.x / 32];
            int index = occluder::Head(word);
            do {
                const SectorOccluder& record = sector.occluders[index];
                int lowest = record.bottom;
                OccluderKind type = record.kind;
                int highest = record.top;
                Occlude(s, type, lowest, highest, true);
                index = record.next;
            } while (index >= 0);
        } else {
            OccluderKind type = occluder::Kind(word);
            int lowest = occluder::Bottom(word);
            int highest = occluder::Top(word);
            Occlude(s, type, lowest, highest, false);
        }
    }

    if (s->passage < 0) return 0;
    if (s->cost > 0x46) return 0;
    int column = (s->passage + 0x32) / 102;
    if (column > 15) column = 15;
    if (s->doubled) s->cost *= 2;
    int cost = s->cost;
    int row;
    if (cost >= 10) row = cost >= 0x23 ? 4 : 3;
    else if (cost >= 3) row = 2;
    else row = cost >= 2 ? 1 : 0;
    return kMarkTable[row * 16 + column];
}

void AIMap::LimitSteepCells(Point first, Point last) {
    first = Max(first, Point{2, 2});
    last = Min(last, Point{cells.width - 3, cells.height - 3});
    int maxLarge = MaxStep(climbLarge);
    int maxSmall = MaxStep(climbSmall);
    const CellOffset* offsets = kSteepOffsets;
    for (int y = first.y; y <= last.y; ++y)
        for (int x = first.x; x <= last.x; ++x) {
            int here = cells[y][x].groundLevel;
            int steepest = 0;
            int k = 0;
            for (; k < 4; ++k) {
                int d = cells[y + offsets[k].dy][x + offsets[k].dx].groundLevel - here;
                if (d < 0) d = -d;
                if (d > steepest) {
                    steepest = d;
                    if (d > maxSmall) break;
                }
            }
            // k is the neighbour that broke the loop, or 4 (the first of the 13) when none did.
            if (steepest > maxSmall)
                CellWord(this, x, y) = 0;
            else if (steepest > maxLarge)
                CellWord(this, x, y) &= 0xF;
            else
                continue;
            CellWord(this, x + offsets[k].dx, y + offsets[k].dy) &= 0xF00FFFFF;
            for (int i = 4; i < 17; ++i) CellWord(this, x + offsets[i].dx, y + offsets[i].dy) &= 0x0FFFFFFF;
        }
}

void AIMap::ClearUnmarkedArea(int level, Point offset, Point first, Point last) {
    first = Max(first, -offset);
    last = Min(last, Point{cells.width - offset.x - 1, cells.height - offset.y - 1});
    // The shift count is masked to 5 bits, as the original's shift does.
    std::uint32_t keep = ~(0xFu << ((level * 4) & 31));
    for (int y = first.y; y <= last.y; ++y)
        for (int x = first.x; x <= last.x; ++x)
            if (marks[y + offset.y][x + offset.x] == 0) cells[y][x].passable &= keep;
}
