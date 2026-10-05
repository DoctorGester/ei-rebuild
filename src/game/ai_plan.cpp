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

#include <cstring>

// ai_join.cpp: the word BuildCellsTo gives its cells (see CommitLeg).
void SetBuildCellsStaleWord(std::uint32_t value);

namespace {

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(object) + offset);
}

constexpr std::uintptr_t kRegionListVtable = 0x0073EDB0;   // CList of region nodes (+8 x, +0xA y: words)
constexpr std::uintptr_t kWaypointListVtable = 0x0073FD9C; // CList of waypoints {x, y, region}
constexpr int kRegionMap = 0x22F8;
constexpr int kRegionCells = 0x2498;     // per size class (16 bytes each): rows of region cell bytes
constexpr int kSearch = 0x2594;          // the forward local search
constexpr int kStartMarks = 0x26BC;      // rows of per-cell bytes; the start cell is cleared
constexpr int kUnreachable = 0x7FFFFFFF;
constexpr std::uint32_t kAllOffsets = 0x60AD78EC;   // the spiral radius covering every offset

// The region nodes and the waypoint nodes (MFC CList nodes).
struct RegionNode {
    RegionNode* next;
    RegionNode* prev;
    std::int16_t x, y;
};
struct WaypointNode {
    WaypointNode* next;
    WaypointNode* prev;
    int x, y;
    RegionNode* region;
};
struct List {
    void* vtable;
    void* head;
    void* tail;
    int count;
    void* free;
    void* blocks;
    int blockSize;
};

// 00556D10 (RET 4) / 00556D40: the region list's constructor (block size) and RemoveAll.
__declspec(naked) void __fastcall RegionListConstruct(List*, void*, int) EI_ORIGINAL(0x00556D10)
__declspec(naked) void __fastcall ListRemoveAll(void*, void*) EI_ORIGINAL(0x00556D40)
// 005B0D90 (RET 0x1C, AL). The region map's chain of regions from (fromX, fromY) to (toX, toY),
// avoiding the regions in `excluded` (AIPath + 0x70).
__declspec(naked) bool __fastcall RegionChain(void*, void*, int, int, int, int, int, List*, void*) EI_ORIGINAL(0x005B0D90)
// 005B9BC0 (RET 4). Prepares the region chain (CAIMap).
__declspec(naked) void __fastcall PrepareChain(CAIMap*, void*, List*) EI_ORIGINAL(0x005B9BC0)
// 005BEDB0 (RET 8). The waypoint list's NewNode(prev, next).
__declspec(naked) WaypointNode* __fastcall WaypointNewNode(List*, void*, void*, void*) EI_ORIGINAL(0x005BEDB0)
// 005BFC40 (RET 8). SpiralCache::Get (radius as float bits).
__declspec(naked) const std::int16_t* __fastcall SpiralGet(void*, void*, std::uint32_t, int*) EI_ORIGINAL(0x005BFC40)
// The local search: begin (RET 0x14: unit, from, to), continue (RET 0x14: from, to, fresh), whether
// a cell was reached (RET 8, AL), the cost of a cell (RET 8), commit, finish, reset.
__declspec(naked) bool __fastcall SearchBegin(void*, void*, CUnit*, int, int, int, int) EI_ORIGINAL(0x005C2420)
__declspec(naked) bool __fastcall SearchContinue(void*, void*, int, int, int, int, int) EI_ORIGINAL(0x005C25D0)
__declspec(naked) bool __fastcall SearchReached(void*, void*, int, int) EI_ORIGINAL(0x005C31A0)
__declspec(naked) void __fastcall SearchCommit(void*, void*) EI_ORIGINAL(0x005C2A30)
void __fastcall SearchReset(void* a0, void*) { LocalSearchResetOriginal(a0); }
// 005B16D0 (RET 8). The excluded regions' CArray<{int x, y}>::SetSize.
inline void __fastcall ExcludedSetSize(void* a, void*, int n, int g) { PointArraySetSizeOriginal(a, nullptr, n, g); }
// 005B9A20 (RET 0xC). Builds the path cells to (toX, toY) from the search into `cells`.
__declspec(naked) void __fastcall BuildCells(CAIMap*, void*, int, int, PathCellList*) EI_ORIGINAL(0x005B9A20)
// 006E727B. atexit, for the function-local spiral cache.

// The function-local static SpiralCache at 007C2F58 (guard bit 0 of 007C2F60, destructor 005BA3F0).
std::uint8_t& SpiralGuard() { return *reinterpret_cast<std::uint8_t*>(0x007C2F60); }
void* Spiral() { return reinterpret_cast<void*>(0x007C2F58); }

// The second dword of every cell BuildCells (005B9A20) adds is a stack slot it never writes: in
// the original it is the dword the planner's last call at the same depth left 20 bytes below the
// planner's stack pointer. SpiralGet (two arguments) pushes the planner's EBP there, which is 0
// at that call; SearchCommit (no arguments) keeps its row offset there, 4 * (last row + 1) after
// its loop over rows max(1, -originY) .. min(32, height - originY - 1). Nothing else is called
// between the last of those and BuildCells; the value reaches the steering as the last step's
// speed word (SetStepSpeeds rewrites the others). BuildCellsTo (ai_join.cpp) takes it from here.
void CommitLeg(void* search) {
    auto* s = static_cast<std::uint8_t*>(search);
    int originY = *reinterpret_cast<int*>(s + 0x58);
    int height = *reinterpret_cast<int*>(*reinterpret_cast<std::uint8_t**>(s + 0x24) + 0x26A4);
    int first = -originY > 1 ? -originY : 1;
    int last = height - originY - 1 > 32 ? 32 : height - originY - 1;
    // When the loop does not run the slot keeps the previous call's word; that needs the search
    // window outside the map, which the planner never sets up.
    if (first <= last) SetBuildCellsStaleWord(static_cast<std::uint32_t>(4 * (last + 1)));
    SearchCommit(search, nullptr);
}

// An inlined CList destructor: the nodes need no destruction; the blocks are freed.
void DestroyList(List& list, std::uintptr_t vtable) {
    list.vtable = reinterpret_cast<void*>(vtable);
    list.count = 0;
    list.free = nullptr;
    list.tail = nullptr;
    list.head = nullptr;
    mfc::PlexFreeDataChain(static_cast<mfc::CPlex*>(list.blocks));
    list.blocks = nullptr;
}

double Threshold() { return *reinterpret_cast<const volatile double*>(0x0073F0A8); }
float Half() { return *reinterpret_cast<const volatile float*>(0x0073B86C); }

} // namespace

bool CAIMap::PlanLongPath(CUnit* unit, int fromX, int fromY, int toX, int toY, AIPath* path, int retry, int maxCells) {
    auto* self = reinterpret_cast<std::uint8_t*>(this);
    List chain;
    RegionListConstruct(&chain, nullptr, 0x80);
    if (!(SpiralGuard() & 1)) {
        At<std::uint32_t>(Spiral(), 4) = 0xBF800000;   // radius -1
        SpiralGuard() |= 1;
        At<int>(Spiral(), 0) = 0;
        GameAtExit(reinterpret_cast<void (*)()>(0x005BA3F0));
    }
    int sizeClass = At<int>(unit, 0x324);
    ListRemoveAll(&path->cells, nullptr);
    bool ok = RegionChain(self + kRegionMap, nullptr, sizeClass, fromX, fromY, toX, toY, &chain,
                          reinterpret_cast<std::uint8_t*>(path) + 0x70);
    if (!ok || chain.count * 8 > maxCells) {
        chain.vtable = reinterpret_cast<void*>(kRegionListVtable);
        ListRemoveAll(&chain, nullptr);
        return false;
    }
    PrepareChain(this, nullptr, &chain);

    // The waypoints: each region's representative cell (the region cell byte's nibbles inside its
    // 8x8 block), then the target.
    int curX = fromX, curY = fromY;
    int bestX = fromX, bestY = fromY;
    List waypoints{reinterpret_cast<void*>(kWaypointListVtable), nullptr, nullptr, 0, nullptr, nullptr, 10};
    std::uint8_t** cellRows = At<std::uint8_t**>(self, kRegionCells + sizeClass * 16);
    auto* region = static_cast<RegionNode*>(chain.head);
    RegionNode* previous = region;
    for (RegionNode* node = region; node;) {
        RegionNode* owner = previous;
        previous = node;
        RegionNode* next = node->next;
        std::uint8_t cell = cellRows[node->y][node->x];
        int px = (cell & 0xF) + node->x * 8;
        int py = (cell >> 4) + node->y * 8;
        WaypointNode* w = WaypointNewNode(&waypoints, nullptr, waypoints.tail, nullptr);
        w->x = px;
        w->y = py;
        w->region = owner;
        if (waypoints.tail) static_cast<WaypointNode*>(waypoints.tail)->next = w;
        else waypoints.head = w;
        waypoints.tail = w;
        node = next;
    }
    {
        WaypointNode* w = WaypointNewNode(&waypoints, nullptr, waypoints.tail, nullptr);
        w->x = toX;
        w->y = toY;
        w->region = previous;
        if (waypoints.tail) static_cast<WaypointNode*>(waypoints.tail)->next = w;
        else waypoints.head = w;
        waypoints.tail = w;
    }

    int count = 0;
    const std::int16_t* offsets = SpiralGet(Spiral(), nullptr, kAllOffsets, &count);
    SetBuildCellsStaleWord(0);
    void* search = self + kSearch;
    bool result = true;
    bool reachedLast = true;   // the previous leg ended on its waypoint
    int legs = 0;
    WaypointNode* failedAt = nullptr;
    bool failed = false;
    for (auto* w = static_cast<WaypointNode*>(waypoints.head); w;) {
        WaypointNode* next = w->next;
        if (!(w->x == curX && w->y == curY)) {
            bool exact;
            result = legs == 0 ? SearchBegin(search, nullptr, unit, curX, curY, w->x, w->y)
                               : SearchContinue(search, nullptr, curX, curY, w->x, w->y, reachedLast ? 0 : 1);
            ++legs;
            if (!result) {
                failed = true;
                failedAt = w;
                break;
            }
            if (SearchReached(search, nullptr, w->x, w->y)) {
                exact = true;
                bestX = w->x;
                bestY = w->y;
            } else {
                // The nearest cell around the waypoint the search did reach.
                exact = false;
                bool found = false;
                for (int k = 0; k < count; ++k) {
                    int x = offsets[k * 2] + w->x;
                    int y = offsets[k * 2 + 1] + w->y;
                    if (SearchCost(search, nullptr, x, y) < kUnreachable) {
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
            if (!next && SearchCost(search, nullptr, toX, toY) == kUnreachable) {
                // The last leg did not reach the target: one more leg from the best cell.
                CommitLeg(search);
                if (!SearchContinue(search, nullptr, bestX, bestY, toX, toY, 1)) {
                    failed = true;
                    failedAt = w;
                    break;
                }
                SearchFinish(search, nullptr);
                result = SearchCost(search, nullptr, toX, toY) != kUnreachable;
            }
            if (!result) {
                failed = true;
                failedAt = w;
                break;
            }
            curY = bestY;
            curX = bestX;
            CommitLeg(search);
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
            CommitLeg(search);
            toY = bestY;
            toX = bestX;
            path->failed = true;
            result = true;
            float tx = StoreFloat(static_cast<float>(static_cast<double>(bestX) + Half()));
            path->targetX = tx;
            float ty = StoreFloat(static_cast<float>(static_cast<double>(bestY) + Half()));
            path->targetY = ty;
        } else {
            SearchReset(search, nullptr);
            bool retried = false;
            if (static_cast<char>(retry)) {
                // Exclude the failing waypoint's region and plan again.
                RegionNode* bad = failedAt->region;
                int x = bad->x;
                int y = bad->y;
                auto* excluded = reinterpret_cast<std::uint8_t*>(path) + 0x70;
                int size = At<int>(excluded, 8);
                ExcludedSetSize(excluded, nullptr, size + 1, -1);
                int* data = At<int*>(excluded, 4);
                data[size * 2] = x;
                data[size * 2 + 1] = y;
                retried = PlanPath(this, nullptr, unit, fromX, fromY, toX, toY, path, retry, maxCells);
            }
            DestroyList(waypoints, kWaypointListVtable);
            DestroyList(chain, kRegionListVtable);
            return retried;
        }
    }

    if (result) {
        At<std::uint8_t**>(self, kStartMarks)[fromY][fromX] = 0;
        BuildCells(this, nullptr, toX, toY, &path->cells);
        auto* first = reinterpret_cast<PathCellList::Node*>(At<void*>(&path->cells, 4));
        if (first->data.x != fromX || first->data.y != fromY) result = false;
    }
    SearchReset(search, nullptr);
    DestroyList(waypoints, kWaypointListVtable);
    DestroyList(chain, kRegionListVtable);
    return result;
}

EI_REPLACE_METHOD(0x005B9C90, CAIMap::PlanLongPath);
