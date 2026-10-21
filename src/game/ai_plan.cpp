// CAIMap::PlanPath (005B9C90): the long-range path. The region map gives a chain of regions from
// the start to the target; the local search (+0x2594) walks from one region's representative cell
// to the next, then to the target; the path builder (005B9A20) turns the search into cells. When a
// leg fails the target moves to the best reachable cell (if it is far enough from the start) or,
// with `retry`, the failing region is excluded and the plan starts again. See ai_map.h and
// docs/notes/ai.md.
#include "ai_map.h"

#include "core_maths.h"

#include "lib/mfc.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "originals_ai.h"
#include "originals_library.h"
#include "originals_units.h"
#include "lib/mfc_list.h"

#include <cstring>

namespace {

constexpr int kUnreachable = 0x7FFFFFFF;
constexpr float kAllOffsets = 1.0e20f;   // the spiral radius covering every offset (0x60AD78EC)

// The chain of regions is a CList<PathCell> (block x, y); the waypoints a CList of {x, y, the
// region node it belongs to} whose NewNode stays the instantiation's (005BEDB0).
using RegionNode = PathCellList::Node;
struct Waypoint {
    int x, y;
    RegionNode* region;
};
using WaypointList = mfc::CList<Waypoint, mfc::RawElements<Waypoint>>;
using WaypointNode = WaypointList::Node;
static_assert(sizeof(WaypointNode) == 0x14);

// 005BEDB0 (RET 8). The waypoint list's NewNode(prev, next): List12's (the element zeroed).
WaypointNode* WaypointNewNode(WaypointList* list, WaypointNode* prev, WaypointNode* next) {
    return mfc::impl::NewNodeAs<mfc::impl::List12>(list, prev, next);
}
// The local search: begin (RET 0x14: unit, from, to), continue (RET 0x14: from, to, fresh), whether
// a cell was reached (RET 8, AL), the cost of a cell (RET 8), commit, finish, reset.
// 006E727B. atexit, for the function-local spiral cache.

// The function-local static SpiralCache (guard bit 0 of g_spiralGuard, destructor 005BA3F0).
SpiralCache g_spiral = /*007C2F58*/ {};
std::uint8_t g_spiralGuard = /*007C2F60*/ {};
std::uint8_t& SpiralGuard() { return g_spiralGuard; }
SpiralCache* Spiral() { return &g_spiral; }

// An inlined CList destructor: the nodes need no destruction; the blocks are freed.
template <typename List>
void DestroyList(List& list, void* vtable) {
    list.vtable = vtable;
    list.RemoveAll();
}

double Threshold() { return 15.0 /*0073F0A8*/; }
float Half() { return 0.5f /*0073B86C*/; }

} // namespace

bool CAIMap::PlanLongPath(CUnit* unit, int fromX, int fromY, int toX, int toY, AIPath* path, int retry, int maxCells) {
    PathCellList chain;
    mfc::impl::ConstructListHeader<mfc::impl::CellListClass>((void*)&chain, 0x80);
    if (!(SpiralGuard() & 1)) {
        Spiral()->radius = -1.0f;
        SpiralGuard() |= 1;
        Spiral()->length = 0;
        GameAtExit(reinterpret_cast<void (*)()>(0x005BA3F0));
    }
    int sizeClass = unit->sizeClass;
    path->cells.RemoveAll();   // 00556D40
    bool ok = regionMap.FindRegionChain(sizeClass, fromX, fromY, toX, toY, &chain, &path->excludedBlocks);
    if (!ok || chain.count * 8 > maxCells) {
        chain.vtable = PathCellListVtable();
        chain.RemoveAll();
        return false;
    }
    ClearMarksAround(&chain);

    // The waypoints: each region's representative cell (the region cell byte's nibbles inside its
    // 8x8 block), then the target.
    int curX = fromX, curY = fromY;
    int bestX = fromX, bestY = fromY;
    WaypointList waypoints{mfc::impl::WaypointListClass::Vtable(), nullptr, nullptr, 0, nullptr, nullptr, 10};
    std::uint8_t** cellRows = regionMap.cells[sizeClass].rows;
    RegionNode* region = chain.head;
    RegionNode* previous = region;
    for (RegionNode* node = region; node;) {
        RegionNode* owner = previous;
        previous = node;
        RegionNode* next = node->next;
        std::uint8_t cell = cellRows[node->data.y][node->data.x];
        int px = (cell & 0xF) + node->data.x * 8;
        int py = (cell >> 4) + node->data.y * 8;
        WaypointNode* w = WaypointNewNode(&waypoints, waypoints.tail, nullptr);
        w->data.x = px;
        w->data.y = py;
        w->data.region = owner;
        if (waypoints.tail) waypoints.tail->next = w;
        else waypoints.head = w;
        waypoints.tail = w;
        node = next;
    }
    {
        WaypointNode* w = WaypointNewNode(&waypoints, waypoints.tail, nullptr);
        w->data.x = toX;
        w->data.y = toY;
        w->data.region = previous;
        if (waypoints.tail) waypoints.tail->next = w;
        else waypoints.head = w;
        waypoints.tail = w;
    }

    int count = 0;
    const std::int16_t* offsets = Spiral()->Get(kAllOffsets, &count);
    LocalSearch* search = &forwardSearch;
    bool result = true;
    bool reachedLast = true;   // the previous leg ended on its waypoint
    int legs = 0;
    WaypointNode* failedAt = nullptr;
    bool failed = false;
    for (WaypointNode* w = waypoints.head; w;) {
        WaypointNode* next = w->next;
        if (!(w->data.x == curX && w->data.y == curY)) {
            bool exact;
            result = legs == 0 ? search->Begin(unit, curX, curY, w->data.x, w->data.y)
                               : search->Recenter(curX, curY, w->data.x, w->data.y, reachedLast ? 0 : 1);
            ++legs;
            if (!result) {
                failed = true;
                failedAt = w;
                break;
            }
            if (search->ExpandUntil(w->data.x, w->data.y)) {
                exact = true;
                bestX = w->data.x;
                bestY = w->data.y;
            } else {
                // The nearest cell around the waypoint the search did reach.
                exact = false;
                bool found = false;
                for (int k = 0; k < count; ++k) {
                    int x = offsets[k * 2] + w->data.x;
                    int y = offsets[k * 2 + 1] + w->data.y;
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
            if (!next && search->CostAt(toX, toY) == kUnreachable) {
                // The last leg did not reach the target: one more leg from the best cell.
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
        // Far enough from the start: aim at the best cell instead.
        int dx = fromX - bestX;
        if (dx < 0) dx = -dx;
        int dy = fromY - bestY;
        if (dy < 0) dy = -dy;
        int shorter = dx < dy ? dx : dy;
        int diff = dx - dy;
        if (diff < 0) diff = -diff;
        int distance = diff + ((shorter * 1448) >> 10);
        if (static_cast<double>(distance) > Threshold()) {
            search->CommitDirections();
            toY = bestY;
            toX = bestX;
            path->failed = true;
            result = true;
            float tx = static_cast<float>(static_cast<double>(bestX) + Half());
            path->targetX = tx;
            float ty = static_cast<float>(static_cast<double>(bestY) + Half());
            path->targetY = ty;
        } else {
            search->Reset();
            bool retried = false;
            if (static_cast<char>(retry)) {
                // Exclude the failing waypoint's region and plan again.
                RegionNode* bad = failedAt->data.region;
                int x = bad->data.x;
                int y = bad->data.y;
                BlockPointArray* excluded = &path->excludedBlocks;
                int size = excluded->size;
                excluded->SetSize(size + 1, -1);
                BlockPoint* data = excluded->data;
                data[size].x = x;
                data[size].y = y;
                retried = PlanPath(this, nullptr, unit, fromX, fromY, toX, toY, path, retry, maxCells);
            }
            DestroyList(waypoints, mfc::impl::WaypointListClass::Vtable());
            DestroyList(chain, PathCellListVtable());
            return retried;
        }
    }

    if (result) {
        marks.rows[fromY][fromX] = 0;
        BuildCellsTo(toX, toY, &path->cells);
        PathCellList::Node* first = path->cells.head;
        if (first->data.x != fromX || first->data.y != fromY) result = false;
    }
    search->Reset();
    DestroyList(waypoints, mfc::impl::WaypointListClass::Vtable());
    DestroyList(chain, PathCellListVtable());
    return result;
}

EI_REPLACE_METHOD(0x005B9C90, CAIMap::PlanLongPath);
