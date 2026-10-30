// AIMap::PlanPath: the long-range path. The region map gives a chain of regions from the start to the
// target; the local search (+0x2594) walks from one region's representative cell to the next, then to the
// target; the path builder turns the search into cells. When a leg fails the target moves to the best
// reachable cell (if it is far enough from the start) or, with `retry`, the failing region is excluded and
// the plan starts again. See ai_map.h and docs/notes/ai.md.
#include "ai_map.h"

#include "core_maths.h"
#include "lib/containers.h"

#include <cstdint>


#include <cstring>

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
