// Per-tick movement checks on the AI map: which unit blocks a moving unit, and the speed costs of
// the steps a unit is about to walk.
#include "game/ai/ai_map.h"
#include "game/ai/unit_shape.h"
#include "engine/core/core_maths.h"
#include "engine/core/core_refcount.h"
#include "game/effects/effects.h"

#include "lib/crt.h"
#include "lib/rounding.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {

constexpr float kShapeMinHeight = 0.2f;
constexpr int kExpensive = 12000;           // cost above which a step is flagged 2

// A square grid of `size`: reallocated when the size differs, then cleared.
void ResizeCleared(SearchGrid<std::uint8_t>* grid, int size) {
    grid->Resize(size, size);
    grid->data.assign(grid->data.size(), 0);
}

} // namespace

Unit* AIMap::BlockingUnitAt(Unit* unit, const Vector3& position) {
    float unitHeight = unit->standingHeight;
    std::vector<Unit*> nearby = sectorMap.FindUnitsInRadius(position, 10.0f);

    Unit* blocker = nullptr;
    for (Unit* other : nearby) {
        if (other == unit) continue;
        float reach = other->standingHeight + unitHeight;
        if (DistanceSquaredXY(position, other->localPosition) >= reach * reach) continue;
        if (other->IsDead() != 0) continue;

        const Point apart = WorldToCell(unit->localPosition) - WorldToCell(other->localPosition);
        float otherHeight = other->standingHeight;
        ShapeCacheHandle handle;
        const UnitShape* shape = nullptr;
        if (otherHeight <= kShapeMinHeight) {
            handle.ref.Reset();
        } else {
            int tenths = lib::RoundEven(otherHeight * 10.0f + 1.0f);
            handle.height = static_cast<float>(tenths) * 0.1f;
            shape = g_unitShapes.Get(&handle);
            handle.ref.Reset();
        }

        int level = lib::RoundEven((2.0f - unit->standingHeight) * 16.0f);
        int minLevel = level < 0 ? 0 : (level & 0xFF);
        int dxCells = apart.x + 8;
        int dyCells = apart.y + 8;
        if (!shape || dxCells < 0 || dyCells < 0 || dxCells >= 16 || dyCells >= 16 ||
            static_cast<std::int8_t>(shape->levels[dyCells][dxCells]) >= minLevel) {
            blocker = other;
            break;
        }
    }
    return blocker;
}

const int* AIMap::CostContext(int sizeClass, PassCostContext* out) {
    out->forwardSpeed = 0.1f;
    out->backwardSpeed = 0.1f;
    out->turnScale = 10.0f;
    out->shift = sizeClass * 4;
    const std::vector<int>& climb = out->shift == 0 ? climbSmall : climbLarge;
    out->costs = kPassCosts;
    out->climb = climb.data() + std::ssize(climb) / 2;
    return out->climb;
}

void AIMap::SetStepSpeeds(Unit* unit, PathCells* steps) {
    // The original also asks the unit for its movement speeds twice and divides by one of them, but never uses
    // the results; the getter has no side effects.
    int shift = unit->sizeClass * 4;
    const std::vector<int>& climbTable = shift == 0 ? climbSmall : climbLarge;
    const int* climb = climbTable.data() + std::ssize(climbTable) / 2;
        PassCostContext context;
    CostContext(unit->sizeClass, &context);

    auto node = steps->begin();
    if (node == steps->end()) return;
    for (;;) {
        auto next = std::next(node);
        const AIMapCell& cell = cells[node->y][node->x];
        if (next == steps->end()) {
            int cost = context.costs[cell.Passability(context.shift)];
            node->terrainBits =
                static_cast<std::uint16_t>((cell.InWater() ? 1 : 0) + (cost > kExpensive ? 2 : 0));
            return;
        }
        const AIMapCell& to = cells[next->y][next->x];
        int cost = kStepCosts[to.Passability(shift)];
        int contextCost = context.costs[to.Passability(context.shift)];
        if (cost < 0) cost = 0x400;
        int climbCost = climb[static_cast<int>(to.groundLevel) - static_cast<int>(cell.groundLevel)];
        int speed = climbCost < 0 ? 0x200 : static_cast<int>(static_cast<unsigned>(cost) << 9) / climbCost;
        node->flags = static_cast<std::uint16_t>(speed);
        node->terrainBits = static_cast<std::uint16_t>((to.InWater() ? 1 : 0) + (contextCost > kExpensive ? 2 : 0));
        node = next;
    }
}

bool AIMap::StandsOnCheapCell(Unit* unit) {
    PassCostContext context;
    CostContext(unit->sizeClass, &context);
    // The original also asks the unit for its movement speeds and ignores them.
    const Point cell = WorldToCell(unit->localPosition);
    if (!InBounds(cell)) return false;
    if (HasSpellArea(cell)) {
        int sizeClass = unit->sizeClass;
        int level = lib::RoundEven(kLevelHeights[sizeClass] * heightScale);
        CellMarkState mark;
        CellMark(&mark, sizeClass, level, cell);
        return mark.cost < kExpensive;
    }
    int cost = context.costs[cells[cell].Passability(context.shift)];
    return cost > 0 && cost < kExpensive;
}

void FogGrid::Grow(int x, int y, int n) {
    int left = x - n, right = x + n, top = y - n, bottom = y + n;
    if (x0 == 0x7FFFFFFF) {
        int size = (bottom - top > right - left ? bottom - top : right - left) + 1;
        ResizeCleared(&cells, size);
        x0 = left;
        y0 = top;
        return;
    }
    const int width = cells.width, height = cells.height;
    bool grow = left < x0 || top < y0 || right > x0 + width - 1 || bottom > y0 + height - 1;
    if (left >= x0) left = x0;
    if (right <= x0 + width - 1) right = x0 + width - 1;
    if (top >= y0) top = y0;
    if (bottom <= y0 + height - 1) bottom = y0 + height - 1;
    if (!grow) return;
    int size = (bottom - top > right - left ? bottom - top : right - left) + 1;
    // A copy of the old marks, moved into the cleared grid.
    const SearchGrid<std::uint8_t> old = cells;
    ResizeCleared(&cells, size);
    for (int row = 0; row < old.height; ++row)
        for (int col = 0; col < old.width; ++col)
            cells[row - top + y0][col - left + x0] = old[row][col];
    x0 = left;
    y0 = top;
}

void AIMap::MarkVisibleCells(const Vector3& eye, float radius, const Vector3& look, float cosLimit,
                              float targetHeight, FogGrid* grid) {
    const Point eyeCell = WorldToCell(eye);
    const int eyeX = eyeCell.x, eyeY = eyeCell.y;
    int reach = WorldToCell(radius) + 1;
    grid->Grow(eyeX, eyeY, reach);
    bool anyDirection = cosLimit <= -1.0f;
    bool noLineOfSight = targetHeight < -100.0f;

    const float half = 0.5f;    // half a cell
    for (int y = eyeY - reach; y < eyeY + reach; ++y) {
        for (int x = eyeX - reach; x < eyeX + reach; ++x) {
            int gx = x - grid->x0;
            int gy = y - grid->y0;
            if (x * 9 < 0 || y < 0 || x >= cells.width || y >= cells.height) continue;
            Vector3 cell;
            cell.x = static_cast<float>(x) * half;
            cell.y = static_cast<float>(y) * half;
            cell.z = 0.0f;
            float dx = cell.x - eye.x;
            float dy = cell.y - eye.y;
            float dz = -eye.z;
            float distance = std::sqrt(LengthSquared2D(dy, dx));
            if (radius < distance) continue;
            if (!anyDirection) {
                float dot = Dot(Vector3{dx, dy, dz}, Vector3{look.x, look.y, look.z});
                if (distance * cosLimit > dot) continue;
            }
            if (!noLineOfSight) {
                cell.z = static_cast<float>(cells[y][x].groundLevel) / heightScale + targetHeight;
                float seen = LineOfSight(eye, cell) * radius;
                if (seen < distance) continue;
            }
            grid->cells[gy][gx] = 1;
        }
    }
}

UnitShape* AIMap::ShapeForHeight(float standingHeight) {
    ShapeCacheHandle handle;
    if (standingHeight <= kShapeMinHeight) return nullptr;
    int tenths = lib::RoundEven(standingHeight * 10.0f + 1.0f);
    handle.height = static_cast<float>(tenths) * 0.1f;

    UnitShape* shape = g_unitShapes.Resolve(&handle);
    handle.ref.Reset();
    return shape;
}

// --- Ai plan -----------------------------------------------------------------------------------------

// AIMap::PlanPath: the long-range path. The region map gives a chain of regions from the start to the
// target; the local search (+0x2594) walks from one region's representative cell to the next, then to the
// target; the path builder turns the search into cells. When a leg fails the target moves to the best
// reachable cell (if it is far enough from the start) or, with `retry`, the failing region is excluded and
// the plan starts again. See ai_map.h and docs/notes/ai.md.

namespace {

constexpr int kUnreachable = 0x7FFFFFFF;
constexpr float kAllOffsets = 1.0e20f;   // the spiral radius covering every offset

struct Waypoint {
    Point cell;
    PathCells::iterator region;
};

constexpr float kThreshold = 15.0f;

} // namespace

bool AIMap::PlanLongPath(Unit* unit, Point from, Point to, AIPath* path, int retry, int maxCells) {
    PathCells chain;
    static SpiralCache spiral{0, -1.0f};
    int sizeClass = unit->sizeClass;
    path->cells.clear();
    bool ok = regionMap.FindRegionChain(sizeClass, from, to, &chain, &path->excludedBlocks);
    if (!ok || std::ssize(chain) * 8 > maxCells) {
        chain.clear();
        return false;
    }
    ClearMarksAround(&chain);

    // The waypoints: each region's representative cell (the region cell byte's nibbles inside its
    // 8x8 block), then the target.
    Point current = from;
    Point best = from;
    std::vector<Waypoint> waypoints;
    SearchGrid<std::uint8_t>& cellRows = regionMap.cells[sizeClass];
    PathCells::iterator previous = chain.begin();
    for (PathCells::iterator node = chain.begin(); node != chain.end(); ++node) {
        PathCells::iterator owner = previous;
        previous = node;
        std::uint8_t cell = cellRows[node->y][node->x];
        waypoints.push_back({Representative(cell) + Point{node->x, node->y} * 8, owner});
    }
    waypoints.push_back({to, previous});

    int count = 0;
    const std::int16_t* offsets = spiral.Get(kAllOffsets, &count);
    LocalSearch* search = &forwardSearch;
    bool result = true;
    bool reachedLast = true;   // the previous leg ended on its waypoint
    int legs = 0;
    std::vector<Waypoint>::iterator failedAt;
    bool failed = false;
    for (auto w = waypoints.begin(); w != waypoints.end();) {
        auto next = std::next(w);
        if (w->cell != current) {
            bool exact;
            result = legs == 0 ? search->Begin(unit, current, w->cell)
                               : search->Recenter(current, w->cell, reachedLast ? 0 : 1);
            ++legs;
            if (!result) {
                failed = true;
                failedAt = w;
                break;
            }
            if (search->ExpandUntil(w->cell)) {
                exact = true;
                best = w->cell;
            } else {
                exact = false;
                bool found = false;
                for (int k = 0; k < count; ++k) {
                    const Point at = Point{offsets[k * 2], offsets[k * 2 + 1]} + w->cell;
                    if (search->CostAt(at) < kUnreachable) {
                        best = at;
                        result = true;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    failed = true;
                    failedAt = w;
                    break;
                }
            }
            if (next == waypoints.end() && search->CostAt(to) == kUnreachable) {
                search->CommitDirections();
                if (!search->Recenter(best, to, 1)) {
                    failed = true;
                    failedAt = w;
                    break;
                }
                search->ExpandAll();
                result = search->CostAt(to) != kUnreachable;
            }
            if (!result) {
                failed = true;
                failedAt = w;
                break;
            }
            current = best;
            search->CommitDirections();
            reachedLast = exact;
        }
        w = next;
    }

    if (failed) {
        int dx = from.x - best.x;
        if (dx < 0) dx = -dx;
        int dy = from.y - best.y;
        if (dy < 0) dy = -dy;
        int shorter = dx < dy ? dx : dy;
        int diff = dx - dy;
        if (diff < 0) diff = -diff;
        int distance = diff + ((shorter * 1448) >> 10);
        if (static_cast<float>(distance) > kThreshold) {
            search->CommitDirections();
            to = best;
            path->failed = true;
            result = true;
            path->target = {static_cast<float>(best.x) + 0.5f, static_cast<float>(best.y) + 0.5f};
        } else {
            search->Reset();
            bool retried = false;
            if (static_cast<char>(retry)) {
                PathCells::iterator bad = failedAt->region;
                path->excludedBlocks.push_back({bad->x, bad->y});
                retried = PlanLongPath(unit, from, to, path, retry, maxCells);
            }
            return retried;
        }
    }

    if (result) {
        marks[from] = 0;
        BuildCellsTo(to, &path->cells);
        auto first = path->cells.begin();
        if (first->x != from.x || first->y != from.y) result = false;
    }
    search->Reset();
    return result;
}
