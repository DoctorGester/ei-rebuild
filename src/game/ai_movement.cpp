// Per-tick movement checks on the AI map: which unit blocks a moving unit, and the speed costs of
// the steps a unit is about to walk.
#include "ai_map.h"

#include "ai_shape_cache.h"
#include "ai_unit_layer.h"
#include "core_maths.h"
#include "core_refcount.h"
#include "effects.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/nan_audit.h"
#include "lib/rounding.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

// The unit shape cache. Here, not in ai_unit_shape.cpp, so the path vectors' replay links it.
UnitShapeCache g_unitShapes;

namespace {

// World coordinate to cell: (x / (half a cell) - 0.5) rounded to float, then to the nearest integer.
int CellOf(float world) { return lib::RoundEven(2.0f * world - 0.5f); }

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
        float dy = position.y - other->localPosition.y;
        float dx = position.x - other->localPosition.x;
        if (LengthSquared2DUnrounded(dx, dy) >= reach * reach) continue;
        if (other->IsDead() != 0) continue;

        int unitX = CellOf(unit->localPosition.x);
        int unitY = CellOf(unit->localPosition.y);
        int otherX = CellOf(other->localPosition.x);
        int otherY = CellOf(other->localPosition.y);
        float otherHeight = other->standingHeight;
        ShapeCacheHandle handle;
        const UnitShape* shape = nullptr;
        EI_NAN_AUDIT(otherHeight);
        if (otherHeight <= kShapeMinHeight) {
            handle.ref.Reset();
        } else {
            int tenths = lib::RoundEven(otherHeight * 10.0f + 1.0f);
            handle.height = static_cast<float>(tenths) * 0.1f;
            shape = static_cast<const UnitShape*>(g_unitShapes.Get(&handle));
            handle.ref.Reset();
        }

        int level = lib::RoundEven((2.0f - unit->standingHeight) * 16.0f);
        int minLevel = level < 0 ? 0 : (level & 0xFF);
        int dxCells = unitX - otherX + 8;
        int dyCells = unitY - otherY + 8;
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
    out->costs = passCosts;
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
        int cost = stepCosts[to.Passability(shift)];
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
    int x = CellOf(unit->localPosition.x);
    int y = CellOf(unit->localPosition.y);
    if (x < 0 || y < 0 || x >= cells.width || y >= cells.height) return false;
    if (HasSpellArea(x, y)) {
        int sizeClass = unit->sizeClass;
        int level = lib::RoundEven(kLevelHeights[sizeClass] * heightScale);
        CellMarkState mark;
        CellMark(&mark, sizeClass, level, x, y);
        return mark.cost < kExpensive;
    }
    int cost = context.costs[cells[y][x].Passability(context.shift)];
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
    int eyeX = CellOf(eye.x);
    int eyeY = CellOf(eye.y);
    int reach = CellOf(radius) + 1;
    grid->Grow(eyeX, eyeY, reach);
    EI_NAN_AUDIT(cosLimit, targetHeight);
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
            float distance = std::sqrt(LengthSquared2DUnrounded(dy, dx));
            EI_NAN_AUDIT(radius, distance);
            if (radius < distance) continue;
            if (!anyDirection) {
                float dot = DotUnrounded<Sum3::ZYX>(dx, dy, dz, look.x, look.y, look.z);
                if (distance * cosLimit > dot) continue;
            }
            if (!noLineOfSight) {
                cell.z = static_cast<float>(cells[y][x].groundLevel) / heightScale + targetHeight;
                float seen = LineOfSight(eye, cell) * radius;
                EI_NAN_AUDIT(seen, distance);
                if (seen < distance) continue;
            }
            grid->cells[gy][gx] = 1;
        }
    }
}

void* AIMap::ShapeForHeight(float standingHeight) {
    ShapeCacheHandle handle;
    EI_NAN_AUDIT(standingHeight);
    if (standingHeight <= kShapeMinHeight) return nullptr;
    int tenths = lib::RoundEven(standingHeight * 10.0f + 1.0f);
    handle.height = static_cast<float>(tenths) * 0.1f;

    RefObject* shape = g_unitShapes.Resolve(&handle);
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
    int x, y;
    PathCells::iterator region;
};

constexpr float kThreshold = 15.0f;

} // namespace

bool AIMap::PlanLongPath(Unit* unit, int fromX, int fromY, int toX, int toY, AIPath* path, int retry, int maxCells) {
    PathCells chain;
    static SpiralCache spiral{0, -1.0f};
    int sizeClass = unit->sizeClass;
    path->cells.clear();
    bool ok = regionMap.FindRegionChain(sizeClass, fromX, fromY, toX, toY, &chain, &path->excludedBlocks);
    if (!ok || std::ssize(chain) * 8 > maxCells) {
        chain.clear();
        return false;
    }
    ClearMarksAround(&chain);

    // The waypoints: each region's representative cell (the region cell byte's nibbles inside its
    // 8x8 block), then the target.
    int curX = fromX, curY = fromY;
    int bestX = fromX, bestY = fromY;
    std::vector<Waypoint> waypoints;
    SearchGrid<std::uint8_t>& cellRows = regionMap.cells[sizeClass];
    PathCells::iterator previous = chain.begin();
    for (PathCells::iterator node = chain.begin(); node != chain.end(); ++node) {
        PathCells::iterator owner = previous;
        previous = node;
        std::uint8_t cell = cellRows[node->y][node->x];
        waypoints.push_back({RepresentativeX(cell) + node->x * 8, RepresentativeY(cell) + node->y * 8, owner});
    }
    waypoints.push_back({toX, toY, previous});

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
        if (!(w->x == curX && w->y == curY)) {
            bool exact;
            result = legs == 0 ? search->Begin(unit, curX, curY, w->x, w->y)
                               : search->Recenter(curX, curY, w->x, w->y, reachedLast ? 0 : 1);
            ++legs;
            if (!result) {
                failed = true;
                failedAt = w;
                break;
            }
            if (search->ExpandUntil(w->x, w->y)) {
                exact = true;
                bestX = w->x;
                bestY = w->y;
            } else {
                exact = false;
                bool found = false;
                for (int k = 0; k < count; ++k) {
                    int x = offsets[k * 2] + w->x;
                    int y = offsets[k * 2 + 1] + w->y;
                    if (search->CostAt(x, y) < kUnreachable) {
                        bestX = x;
                        bestY = y;
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
            if (next == waypoints.end() && search->CostAt(toX, toY) == kUnreachable) {
                search->CommitDirections();
                if (!search->Recenter(bestX, bestY, toX, toY, 1)) {
                    failed = true;
                    failedAt = w;
                    break;
                }
                search->ExpandAll();
                result = search->CostAt(toX, toY) != kUnreachable;
            }
            if (!result) {
                failed = true;
                failedAt = w;
                break;
            }
            curY = bestY;
            curX = bestX;
            search->CommitDirections();
            reachedLast = exact;
        }
        w = next;
    }

    if (failed) {
        int dx = fromX - bestX;
        if (dx < 0) dx = -dx;
        int dy = fromY - bestY;
        if (dy < 0) dy = -dy;
        int shorter = dx < dy ? dx : dy;
        int diff = dx - dy;
        if (diff < 0) diff = -diff;
        int distance = diff + ((shorter * 1448) >> 10);
        if (static_cast<float>(distance) > kThreshold) {
            search->CommitDirections();
            toY = bestY;
            toX = bestX;
            path->failed = true;
            result = true;
            float tx = static_cast<float>(bestX) + 0.5f;
            path->targetX = tx;
            float ty = static_cast<float>(bestY) + 0.5f;
            path->targetY = ty;
        } else {
            search->Reset();
            bool retried = false;
            if (static_cast<char>(retry)) {
                PathCells::iterator bad = failedAt->region;
                int x = bad->x;
                int y = bad->y;
                std::vector<BlockPoint>* excluded = &path->excludedBlocks;
                int size = lib::Count(*excluded);
                excluded->resize(size + 1);
                BlockPoint* data = excluded->data();
                data[size].x = x;
                data[size].y = y;
                retried = PlanLongPath(unit, fromX, fromY, toX, toY, path, retry, maxCells);
            }
            return retried;
        }
    }

    if (result) {
        marks[fromY][fromX] = 0;
        BuildCellsTo(toX, toY, &path->cells);
        auto first = path->cells.begin();
        if (first->x != fromX || first->y != fromY) result = false;
    }
    search->Reset();
    return result;
}
