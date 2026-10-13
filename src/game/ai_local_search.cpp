// The local search (LocalSearch, ai_map.h): Dijkstra over a 34 x 34 window of AI map cells, used
// for short paths (CAIMap::JoinLocalSearches runs a forward and a backward one until they meet),
// the long-range planner's legs and the region map's block measurements. See docs/notes/ai.md.
#include "ai_map.h"
#include "ai_grids.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/mfc.h"

#include <cstdint>
#include <cstring>

namespace {

constexpr int kUnreached = 0x7FFFFFFF;
constexpr int kWindow = 0x22;       // 34 cells, the outer ring never entered
constexpr int kStraight = 0x400;
constexpr int kDiagonal = 0x5A8;

SearchNode* NodeAt(LocalSearch* search, int wx, int wy) { return search->nodes.rows[wy] + wx; }

void ResetNode(SearchNode* node) {
    node->cost = kUnreached;
    node->next = nullptr;
    node->prev = nullptr;
    node->direction = 0;
    node->settled = false;
    node->usable = true;
}

// A copy of the inlined CList<PathCell>::AddHead of the original (NewNode, then linked first).
void AddHead(PathCellList* list, const PathCell& cell) {
    PathCellList::Node* node = list->NewNode(nullptr, list->head);
    node->data = cell;
    if (list->head) list->head->prev = node;
    else list->tail = node;
    list->head = node;
}

// |v| as CDQ / XOR / SUB computes it (INT_MIN stays negative).
int CdqAbs(int v) {
    auto u = static_cast<std::uint32_t>(v);
    auto sign = static_cast<std::uint32_t>(v >> 31);
    return static_cast<int>((u ^ sign) - sign);
}

// The window's map rows: cellRows[i] = &map cell (originX, originY + i) for the rows on the map.
void FillCellRows(LocalSearch* search) {
    for (int i = 0; i < kWindow; ++i) {
        int row = search->originY + i;
        if (row >= 0 && row < search->map->height) search->cellRows[i] = search->map->cells[row] + search->originX;
    }
}

// Window cell (wx, wy) becomes the start: cost 0, settled, nothing queued; the search is active.
void Start(LocalSearch* search, int wx, int wy) {
    SearchNode* start = NodeAt(search, wx, wy);
    search->current = start;
    start->cost = 0;
    start->direction = 0;
    start->settled = true;
    for (SearchNode*& bucket : search->buckets) bucket = nullptr;
    search->active = true;
}

// The inclusive window range of cells on the map, off the window's border:
// [max(1, -origin), min(32, size - origin - 1)] for x and y.
struct WindowRange {
    int x0, x1, y0, y1;
};
WindowRange OnMapRange(const LocalSearch* search) {
    WindowRange r;
    r.x0 = -search->originX > 0 ? -search->originX : 1;
    r.y0 = -search->originY > 0 ? -search->originY : 1;
    r.x1 = search->map->width - search->originX - 1;
    if (r.x1 > 32) r.x1 = 32;
    r.y1 = search->map->height - search->originY - 1;
    if (r.y1 > 32) r.y1 = 32;
    return r;
}

} // namespace

void LocalSearch::Reset() {
    for (SearchNode* node = settledList; node;) {
        SearchNode* next = node->next;
        ResetNode(node);
        node = next;
    }
    for (SearchNode* head : buckets) {
        for (SearchNode* node = head; node;) {
            SearchNode* next = node->next;
            ResetNode(node);
            node = next;
        }
    }
    settledList = nullptr;
    active = false;
    sizeClass = -1;
    level = 0;
}
EI_REPLACE_METHOD(0x005C35F0, LocalSearch::Reset);

void LocalSearch::PickCurrent() {
    current = nullptr;
    int best = kUnreached;
    for (SearchNode* head : buckets) {
        if (head && head->cost < best) {
            current = head;
            best = head->cost;
        }
    }
}
EI_REPLACE_METHOD(0x005C2F50, LocalSearch::PickCurrent);

void LocalSearch::Requeue(SearchNode* node, int cost) {
    SearchNode*& bucket = buckets[node->x & 3];
    SearchNode* head = bucket;
    if (node == head) return;
    // Unlink it from wherever it is.
    if (node->next) node->next->prev = node->prev;
    if (node->prev) node->prev->next = node->next;
    node->next = nullptr;
    node->prev = nullptr;
    if (!head) {
        bucket = node;
        node->next = nullptr;
        return;
    }
    if (cost <= head->cost) {
        bucket = node;
        node->next = head;
        head->prev = node;
        return;
    }
    SearchNode* after = head;
    while (after->next && after->next->cost < cost) after = after->next;
    node->next = after->next;
    if (after->next) after->next->prev = node;
    after->next = node;
    node->prev = after;
}
EI_REPLACE_METHOD(0x005C2FB0, LocalSearch::Requeue);

void LocalSearch::Best(int* x, int* y, int* cost) {
    *cost = kUnreached;
    if (!current) return;
    *x = current->x + originX;
    *y = current->y + originY;
    *cost = current->cost;
}
EI_REPLACE_METHOD(0x005C3050, LocalSearch::Best);

int LocalSearch::CostAt(int x, int y) {
    int wx = x - originX;
    int wy = y - originY;
    if (wx < 0 || wx >= kWindow || wy < 0 || wy >= kWindow) return kUnreached;
    const SearchNode* node = NodeAt(this, wx, wy);
    return node->settled ? node->cost : kUnreached;
}
EI_REPLACE_METHOD(0x005C3090, LocalSearch::CostAt);

int LocalSearch::StepCost(const SearchNode* node, int factor) {
    if (!node->usable) return -1;
    const AIMapCell& to = cellRows[node->y][node->x];
    const AIMapCell& from = cellRows[current->y][current->x];
    int pass, climb;
    if (backward) {
        // The step is taken from `node` into `current`.
        pass = costs.costs[(from.passable >> (costs.shift & 31)) & 0xF];
        if (pass < 0) return -1;
        if (level < to.level && from.level <= to.level) return -1;
        climb = costs.climb[static_cast<int>(from.groundLevel) - static_cast<int>(to.groundLevel)];
    } else {
        if (level < to.level && from.level <= to.level) return -1;
        pass = costs.costs[(to.passable >> (costs.shift & 31)) & 0xF];
        if (pass < 0) return -1;
        climb = costs.climb[static_cast<int>(to.groundLevel) - static_cast<int>(from.groundLevel)];
    }
    if (climb < 0) return -1;
    return ((climb * pass) >> 10) * factor >> 10;
}
EI_REPLACE_METHOD(0x005C1D50, LocalSearch::StepCost);

void LocalSearch::Expand() {
    SearchNode* node = current;
    node->settled = true;
    SearchNode* next = node->next;
    buckets[node->x & 3] = next;
    if (next) next->prev = nullptr;
    node->next = settledList;
    settledList = node;
    currentCost = node->cost;
    // The neighbours in the original's order, each with the direction leading back to `node`.
    static const struct {
        int dx, dy, factor;
        std::int8_t direction;
    } kSteps[8] = {
        {-1, 1, kDiagonal, 8}, {0, 1, kStraight, 1},  {1, 1, kDiagonal, 2},   {1, 0, kStraight, 3},
        {-1, 0, kStraight, 7}, {-1, -1, kDiagonal, 6}, {0, -1, kStraight, 5}, {1, -1, kDiagonal, 4},
    };
    for (const auto& step : kSteps) {
        SearchNode* neighbour = node + step.dy * kWindow + step.dx;
        if (currentCost >= neighbour->cost) continue;
        int cost = StepCost(neighbour, step.factor);
        if (cost < 0) continue;
        cost += currentCost;
        if (cost >= neighbour->cost) continue;
        neighbour->cost = cost;
        neighbour->direction = step.direction;
        Requeue(neighbour, cost);
    }
    PickCurrent();
}
EI_REPLACE_METHOD(0x005C2D40, LocalSearch::Expand);

void LocalSearch::ExpandAll() {
    if (!active) return;
    while (current) Expand();
}
EI_REPLACE_METHOD(0x005C30E0, LocalSearch::ExpandAll);

bool LocalSearch::ExpandUntil(int x, int y) {
    if (!active) return false;
    // The original forms the row pointer without a bounds check; it is only compared.
    const SearchNode* target = nodes.rows[y - originY] + (x - originX);
    while (current && current != target) Expand();
    return current != nullptr;
}
EI_REPLACE_METHOD(0x005C31A0, LocalSearch::ExpandUntil);

void LocalSearch::PathTo(int x, int y, PathCellList* out) {
    // The inlined RemoveAll.
    out->count = 0;
    out->free = nullptr;
    out->tail = nullptr;
    out->head = nullptr;
    mfc::PlexFreeDataChain(static_cast<mfc::CPlex*>(out->blocks));
    out->blocks = nullptr;
    int wx = x - originX;
    int wy = y - originY;
    if (wx < 0 || wx >= kWindow || wy < 0 || wy >= kWindow) return;
    // The cells' second dword comes from a stack slot the original never writes. Its only caller,
    // CAIMap::JoinLocalSearches, leaves the forward search's address there (pushed by Best).
    auto stale = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&map->forwardSearch));
    auto cellAt = [&](int cx, int cy) {
        return PathCell{static_cast<std::int16_t>(originX + cx), static_cast<std::int16_t>(originY + cy),
                        static_cast<std::uint16_t>(stale), static_cast<std::uint16_t>(stale >> 16)};
    };
    for (;;) {
        std::int8_t direction = NodeAt(this, wx, wy)->direction;
        if (!direction) break;
        AddHead(out, cellAt(wx, wy));
        switch (direction) {
        case 1: --wy; break;
        case 2: --wy; --wx; break;
        case 3: --wx; break;
        case 4: ++wy; --wx; break;
        case 5: ++wy; break;
        case 6: ++wy; ++wx; break;
        case 7: ++wx; break;
        case 8: --wy; ++wx; break;
        default: break;
        }
    }
    AddHead(out, cellAt(wx, wy));
}
EI_REPLACE_METHOD(0x005C3200, LocalSearch::PathTo);

bool LocalSearch::PlaceWindow(int x0, int y0, int x1, int y1) {
    if (CdqAbs(x0 - x1) >= 32 || CdqAbs(y0 - y1) >= 32) return false;
    if (x0 < 0 || x0 >= map->width || x1 < 0 || x1 >= map->width) return false;
    if (y0 < 0 || y0 >= map->height || y1 < 0 || y1 >= map->height) return false;
    int ox = (x0 + x1) / 2 - 16;
    int oy = (y0 + y1) / 2 - 16;
    int minX = x1 <= x0 ? x1 : x0;
    int minY = y1 <= y0 ? y1 : y0;
    originX = minX - 1 <= ox ? minX - 1 : ox;
    originY = minY - 1 <= oy ? minY - 1 : oy;
    return true;
}
EI_REPLACE_METHOD(0x005C22A0, LocalSearch::PlaceWindow);

void LocalSearch::SetUnit(CUnit* unit) {
    map->CostContextFor(unit, &costs);
    sizeClass = unit->sizeClass;
    level = static_cast<std::uint8_t>(StandingLevel(unit));
}
EI_REPLACE_METHOD(0x005C21A0, LocalSearch::SetUnit);

bool LocalSearch::Begin(CUnit* unit, int x0, int y0, int x1, int y1) {
    SetUnit(unit);
    if (!PlaceWindow(x0, y0, x1, y1)) return false;
    FillCellRows(this);
    Start(this, x0 - originX, y0 - originY);
    return true;
}
EI_REPLACE_METHOD(0x005C2420, LocalSearch::Begin);

bool LocalSearch::BeginForClass(int sizeClassArg, int x0, int y0, int x1, int y1) {
    map->CostContext(sizeClassArg, &costs);
    sizeClass = sizeClassArg;
    level = static_cast<std::uint8_t>(LevelForSizeClass(sizeClassArg));
    if (!PlaceWindow(x0, y0, x1, y1)) return false;
    FillCellRows(this);
    Start(this, x0 - originX, y0 - originY);
    return true;
}
EI_REPLACE_METHOD(0x005C24F0, LocalSearch::BeginForClass);

bool LocalSearch::BeginInRect(int x0, int y0, int x1, int y1, int sx, int sy) {
    int dx = CdqAbs(x1 - x0);
    int dy = CdqAbs(y1 - y0);
    if (dx + 4 > 32 || dy + 4 > 32) return false;
    if (sx < x0 || sx > x1 || sy < y0 || sy > y1) return false;
    originX = (x1 < x0 ? x1 : x0) - 1;
    originY = (y1 < y0 ? y1 : y0) - 1;
    // Fence the rectangle: the row below it and the column right of it join the settled list,
    // unusable (row 0 and column 0 are the window's border).
    for (int i = 1; i <= dx + 1; ++i) {
        SearchNode* node = NodeAt(this, i, dy + 2);
        node->usable = false;
        node->next = settledList;
        settledList = node;
    }
    for (int i = 1; i <= dy + 2; ++i) {
        SearchNode* node = NodeAt(this, dx + 2, i);
        node->usable = false;
        node->next = settledList;
        settledList = node;
    }
    FillCellRows(this);
    Start(this, sx - originX, sy - originY);
    return true;
}
EI_REPLACE_METHOD(0x005C2B10, LocalSearch::BeginInRect);

bool LocalSearch::BeginInRectFor(CUnit* unit, int x0, int y0, int x1, int y1, int sx, int sy) {
    SetUnit(unit);
    return BeginInRect(x0, y0, x1, y1, sx, sy);
}
EI_REPLACE_METHOD(0x005C2C90, LocalSearch::BeginInRectFor);

bool LocalSearch::BeginInRectForClass(int sizeClassArg, int x0, int y0, int x1, int y1, int sx, int sy) {
    map->CostContext(sizeClassArg, &costs);
    sizeClass = sizeClassArg;
    level = static_cast<std::uint8_t>(LevelForSizeClass(sizeClassArg));
    return BeginInRect(x0, y0, x1, y1, sx, sy);
}
EI_REPLACE_METHOD(0x005C2CF0, LocalSearch::BeginInRectForClass);

void LocalSearch::CommitDirections() {
    WindowRange r = OnMapRange(this);
    for (int wy = r.y0; wy <= r.y1; ++wy) {
        for (int wx = r.x0; wx <= r.x1; ++wx) {
            const SearchNode* node = NodeAt(this, wx, wy);
            if (node->cost >= kUnreached) continue;
            std::int8_t& mark = map->marks.rows[originY + wy][originX + wx];
            if (mark == 0) mark = node->direction;
        }
    }
}
EI_REPLACE_METHOD(0x005C2A30, LocalSearch::CommitDirections);

void LocalSearch::SaveLevels(bool clear) {
    if (levels.width != kWindow || levels.height != kWindow) {
        gamecrt::operator_delete(levels.data);
        gamecrt::operator_delete(levels.rows);
        levels.width = kWindow;
        levels.height = kWindow;
        AllocateGrid(levels);
    }
    WindowRange r = OnMapRange(this);
    for (int wy = r.y0; wy <= r.y1; ++wy) {
        for (int wx = r.x0; wx <= r.x1; ++wx) levels.rows[wy][wx] = map->cells[originY + wy][originX + wx].level;
    }
    if (clear) {
        for (int wy = r.y0; wy <= r.y1; ++wy) {
            for (int wx = r.x0; wx <= r.x1; ++wx) map->cells[originY + wy][originX + wx].level = 0;
        }
    }
    levelsSaved = true;
}
EI_REPLACE_METHOD(0x005C33F0, LocalSearch::SaveLevels);

void LocalSearch::RestoreLevels() {
    WindowRange r = OnMapRange(this);
    for (int wy = r.y0; wy <= r.y1; ++wy) {
        for (int wx = r.x0; wx <= r.x1; ++wx) map->cells[originY + wy][originX + wx].level = levels.rows[wy][wx];
    }
    levelsSaved = false;
}
EI_REPLACE_METHOD(0x005C3530, LocalSearch::RestoreLevels);

void CAIMap::CostContextFor(CUnit* unit, PassCostContext* out) {
    out->forwardSpeed = 0.1f;
    out->backwardSpeed = 0.1f;
    out->unknown_10 = 10.0f;
    out->shift = unit->sizeClass * 4;
    const ClimbTable& climb = out->shift == 0 ? climbSmall : climbLarge;
    out->climb = climb.costs + climb.count / 2;
    out->costs = passCosts;
    unit->GetMoveSpeeds(&out->forwardSpeed, &out->backwardSpeed);
    out->costs = unit->unknown_43C ? passCostsAlt : passCosts;
}
EI_REPLACE_METHOD(0x005B9580, CAIMap::CostContextFor);

void LocalSearch::Attach(CAIMap* mapArg, bool backwardArg) {
    map = mapArg;
    backward = backwardArg;
    if (nodes.width != kWindow || nodes.height != kWindow) {
        gamecrt::operator_delete(nodes.data);
        gamecrt::operator_delete(nodes.rows);
        nodes.width = kWindow;
        nodes.height = kWindow;
        AllocateGrid(nodes);
    }
    for (int wy = 0; wy < kWindow; ++wy) {
        for (int wx = 0; wx < kWindow; ++wx) {
            SearchNode* node = NodeAt(this, wx, wy);
            node->usable = !(wx == 0 || wx == kWindow - 1 || wy == 0 || wy == kWindow - 1);
            node->settled = false;
            node->cost = kUnreached;
            node->x = static_cast<std::int16_t>(wx);
            node->y = static_cast<std::int16_t>(wy);
            node->next = nullptr;
            node->prev = nullptr;
            node->direction = 0;
        }
    }
    // The inlined CArray<AIMapCell*>::SetSize(34).
    if (!cellRows) {
        cellRows = static_cast<AIMapCell**>(gamecrt::operator_new(kWindow * sizeof(AIMapCell*)));
        for (int i = 0; i < kWindow; ++i) cellRows[i] = nullptr;
        cellRowMax = kWindow;
        cellRowCount = kWindow;
    } else if (cellRowMax >= kWindow) {
        for (int i = cellRowCount; i < kWindow; ++i) cellRows[i] = nullptr;
        cellRowCount = kWindow;
    } else {
        int grow = cellRowGrowBy;
        if (grow == 0) {
            grow = cellRowCount / 8;
            if (grow < 4) grow = 4;
            else if (grow > 1024) grow = 1024;
        }
        int newMax = cellRowMax + grow;
        if (newMax < kWindow + 1) newMax = kWindow;
        auto* rows = static_cast<AIMapCell**>(gamecrt::operator_new(newMax * sizeof(AIMapCell*)));
        for (int i = 0; i < cellRowCount; ++i) rows[i] = cellRows[i];
        for (int i = cellRowCount; i < kWindow; ++i) rows[i] = nullptr;
        gamecrt::operator_delete(cellRows);
        cellRows = rows;
        cellRowCount = kWindow;
        cellRowMax = newMax;
    }
    settledList = nullptr;
    active = false;
}
EI_REPLACE_METHOD(0x005C1EA0, LocalSearch::Attach);

bool LocalSearch::Recenter(int x0, int y0, int x1, int y1, bool settleInside) {
    int oldX = originX, oldY = originY;
    if (!PlaceWindow(x0, y0, x1, y1)) return false;
    FillCellRows(this);
    if (CdqAbs(originX - oldX) >= 31 || CdqAbs(originY - oldY) >= 31) return false;
    // A copy of the node grid (005C4BA0, inlined here).
    SearchGrid<SearchNode> old{nullptr, nullptr, nodes.width, nodes.height};
    AllocateGrid(old);
    if (old.width * old.height > 0) std::memcpy(old.data, nodes.data, old.width * old.height * sizeof(SearchNode));
    int shiftX = originX - oldX, shiftY = originY - oldY;
    // Shift the nodes; remember the cheapest one kept on the old window's inner ring.
    int cheapestBorder = kUnreached;
    for (int wy = 1; wy <= 32; ++wy) {
        int oy = shiftY + wy;
        for (int wx = 1; wx <= 32; ++wx) {
            int ox = shiftX + wx;
            SearchNode* node = NodeAt(this, wx, wy);
            if (ox < 1 || oy < 1 || ox > 32 || oy > 32) {
                node->cost = kUnreached;
                node->settled = false;
                node->direction = 0;
            } else {
                const SearchNode& from = old.rows[oy][ox];
                if ((ox == 1 || ox == 32 || oy == 1 || oy == 32) && from.cost <= cheapestBorder) cheapestBorder = from.cost;
                *node = from;
                node->x = static_cast<std::int16_t>(wx);
                node->y = static_cast<std::int16_t>(wy);
            }
            node->next = nullptr;
            node->prev = nullptr;
        }
    }
    settledList = nullptr;
    for (SearchNode*& bucket : buckets) bucket = nullptr;
    for (int wy = 1; wy <= 32; ++wy) {
        for (int wx = 1; wx <= 32; ++wx) {
            SearchNode* node = NodeAt(this, wx, wy);
            if (settleInside) {
                if (node->cost == kUnreached) continue;
                if (node->cost < cheapestBorder) {
                    node->next = settledList;
                    settledList = node;
                    node->settled = true;
                } else {
                    node->settled = false;
                    Requeue(node, node->cost);
                }
            } else if (node->settled) {
                node->next = settledList;
                settledList = node;
            } else if (node->cost < kUnreached) {
                Requeue(node, node->cost);
            }
        }
    }
    PickCurrent();
    gamecrt::operator_delete(old.data);
    gamecrt::operator_delete(old.rows);
    return true;
}
EI_REPLACE_METHOD(0x005C25D0, LocalSearch::Recenter);
