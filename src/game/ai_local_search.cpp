// The local search (LocalSearch, ai_map.h): Dijkstra over a 34 x 34 window of AI map cells, used
// for short paths (AIMap::JoinLocalSearches runs a forward and a backward one until they meet),
// the long-range planner's legs and the region map's block measurements. See docs/notes/ai.md.
#include "ai_map.h"

#include "lib/crt.h"
#include <algorithm>
#include <cstdint>
#include <cstring>


namespace {

constexpr int kUnreached = 0x7FFFFFFF;
constexpr int kWindow = 0x22;       // 34 cells, the outer ring never entered
constexpr int kStraight = 0x400;
constexpr int kDiagonal = 0x5A8;

SearchNode* NodeAt(LocalSearch* search, int wx, int wy) { return search->nodes[wy] + wx; }

bool InWindow(Point window) { return window.x >= 0 && window.x < kWindow && window.y >= 0 && window.y < kWindow; }

void ResetNode(SearchNode* node) {
    node->cost = kUnreached;
    node->next = nullptr;
    node->prev = nullptr;
    node->direction = 0;
    node->settled = false;
    node->usable = true;
}

// |v| as CDQ / XOR / SUB computes it (INT_MIN stays negative).
int CdqAbs(int v) {
    auto u = static_cast<std::uint32_t>(v);
    auto sign = static_cast<std::uint32_t>(v >> 31);
    return static_cast<int>((u ^ sign) - sign);
}

// The window's map rows: cellRows[i] = &map cell origin + (0, i) for the rows on the map.
void FillCellRows(LocalSearch* search) {
    for (int i = 0; i < kWindow; ++i) {
        int row = search->origin.y + i;
        if (row >= 0 && row < search->map->cells.height) search->cellRows[i] = search->map->cells[row] + search->origin.x;
    }
}

// Window cell `window` becomes the start: cost 0, settled, nothing queued; the search is active.
void Start(LocalSearch* search, Point window) {
    SearchNode* start = &search->nodes[window];
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
    int x0 = 0, x1 = 0, y0 = 0, y1 = 0;
};
WindowRange OnMapRange(const LocalSearch* search) {
    WindowRange r;
    r.x0 = -search->origin.x > 0 ? -search->origin.x : 1;
    r.y0 = -search->origin.y > 0 ? -search->origin.y : 1;
    r.x1 = search->map->cells.width - search->origin.x - 1;
    if (r.x1 > 32) r.x1 = 32;
    r.y1 = search->map->cells.height - search->origin.y - 1;
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

void LocalSearch::Requeue(SearchNode* node, int cost) {
    SearchNode*& bucket = buckets[node->x & 3];
    SearchNode* head = bucket;
    if (node == head) return;
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

void LocalSearch::Best(Point* cell, int* cost) {
    *cost = kUnreached;
    if (!current) return;
    *cell = Point{current->x, current->y} + origin;
    *cost = current->cost;
}

int LocalSearch::CostAt(Point cell) {
    const Point window = cell - origin;
    if (!InWindow(window)) return kUnreached;
    const SearchNode& node = nodes[window];
    return node.settled ? node.cost : kUnreached;
}

int LocalSearch::StepCost(const SearchNode* node, int factor) {
    if (!node->usable) return -1;
    const AIMapCell& to = cellRows[node->y][node->x];
    const AIMapCell& from = cellRows[current->y][current->x];
    int pass, climb;
    if (backward) {
        pass = costs.costs[from.Passability(costs.shift)];
        if (pass < 0) return -1;
        if (level < to.level && from.level <= to.level) return -1;
        climb = costs.climb[static_cast<int>(from.groundLevel) - static_cast<int>(to.groundLevel)];
    } else {
        if (level < to.level && from.level <= to.level) return -1;
        pass = costs.costs[to.Passability(costs.shift)];
        if (pass < 0) return -1;
        climb = costs.climb[static_cast<int>(to.groundLevel) - static_cast<int>(from.groundLevel)];
    }
    if (climb < 0) return -1;
    return ((climb * pass) >> 10) * factor >> 10;
}

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
        int dx = 0, dy = 0, factor = 0;
        std::int8_t direction = 0;
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

void LocalSearch::ExpandAll() {
    if (!active) return;
    while (current) Expand();
}

bool LocalSearch::ExpandUntil(Point cell) {
    if (!active) return false;
    // The node's address is formed without a bounds check; it is only compared.
    const SearchNode* target = &nodes[cell - origin];
    while (current && current != target) Expand();
    return current != nullptr;
}

void LocalSearch::PathTo(Point cell, PathCells* out) {
    out->clear();
    Point window = cell - origin;
    if (!InWindow(window)) return;
    auto cellAt = [&](Point at) {
        return PathCell{static_cast<std::int16_t>(origin.x + at.x), static_cast<std::int16_t>(origin.y + at.y),
                        kPlainStepSpeed, 0};
    };
    // Walk back along the directions (kDirectionDx / Dy), then turn the cells round: the start first.
    for (;;) {
        std::int8_t direction = nodes[window].direction;
        if (!direction) break;
        out->push_back(cellAt(window));
        if (direction >= 1 && direction <= 8) window += Point{kDirectionDx[direction], kDirectionDy[direction]};
    }
    out->push_back(cellAt(window));
    std::ranges::reverse(*out);
}

bool LocalSearch::PlaceWindow(Point a, Point b) {
    if (CdqAbs(a.x - b.x) >= 32 || CdqAbs(a.y - b.y) >= 32) return false;
    if (!map->InBounds(a) || !map->InBounds(b)) return false;
    const Point centred = (a + b) / 2 - Point{16, 16};
    origin = Min(Min(b, a) - Point{1, 1}, centred);
    return true;
}

void LocalSearch::SetUnit(Unit* unit) {
    map->CostContextFor(unit, &costs);
    sizeClass = unit->sizeClass;
    level = static_cast<std::uint8_t>(StandingLevel(unit));
}

bool LocalSearch::Begin(Unit* unit, Point from, Point to) {
    SetUnit(unit);
    if (!PlaceWindow(from, to)) return false;
    FillCellRows(this);
    Start(this, from - origin);
    return true;
}

bool LocalSearch::BeginInRect(Point first, Point last, Point start) {
    int dx = CdqAbs(last.x - first.x);
    int dy = CdqAbs(last.y - first.y);
    if (dx + 4 > 32 || dy + 4 > 32) return false;
    if (start.x < first.x || start.x > last.x || start.y < first.y || start.y > last.y) return false;
    origin = Min(last, first) - Point{1, 1};
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
    Start(this, start - origin);
    return true;
}

bool LocalSearch::BeginInRectForClass(int sizeClassArg, Point first, Point last, Point start) {
    map->CostContext(sizeClassArg, &costs);
    sizeClass = sizeClassArg;
    level = static_cast<std::uint8_t>(LevelForSizeClass(sizeClassArg));
    return BeginInRect(first, last, start);
}

void LocalSearch::CommitDirections() {
    WindowRange r = OnMapRange(this);
    for (int wy = r.y0; wy <= r.y1; ++wy) {
        for (int wx = r.x0; wx <= r.x1; ++wx) {
            const SearchNode* node = NodeAt(this, wx, wy);
            if (node->cost >= kUnreached) continue;
            std::int8_t& mark = map->marks[origin.y + wy][origin.x + wx];
            if (mark == 0) mark = node->direction;
        }
    }
}

void LocalSearch::SaveLevels() {
    levels.Resize(kWindow, kWindow);
    WindowRange r = OnMapRange(this);
    for (int wy = r.y0; wy <= r.y1; ++wy) {
        for (int wx = r.x0; wx <= r.x1; ++wx) levels[wy][wx] = map->cells[origin.y + wy][origin.x + wx].level;
    }
    for (int wy = r.y0; wy <= r.y1; ++wy) {
        for (int wx = r.x0; wx <= r.x1; ++wx) map->cells[origin.y + wy][origin.x + wx].level = 0;
    }
    levelsSaved = true;
}

void LocalSearch::RestoreLevels() {
    WindowRange r = OnMapRange(this);
    for (int wy = r.y0; wy <= r.y1; ++wy) {
        for (int wx = r.x0; wx <= r.x1; ++wx) map->cells[origin.y + wy][origin.x + wx].level = levels[wy][wx];
    }
    levelsSaved = false;
}

void AIMap::CostContextFor(Unit* unit, PassCostContext* out) {
    out->forwardSpeed = 0.1f;
    out->backwardSpeed = 0.1f;
    out->turnScale = 10.0f;
    out->shift = unit->sizeClass * 4;
    const std::vector<int>& climb = out->shift == 0 ? climbSmall : climbLarge;
    out->climb = climb.data() + std::ssize(climb) / 2;
    out->costs = kPassCosts;
    const Unit::MoveSpeeds speeds = unit->GetMoveSpeeds();
    out->forwardSpeed = speeds.forward;
    out->backwardSpeed = speeds.turn;
    out->costs = unit->altPassCosts ? kPassCostsAlt : kPassCosts;
}

void LocalSearch::Attach(AIMap* mapArg, bool backwardArg) {
    map = mapArg;
    backward = backwardArg;
    nodes.Resize(kWindow, kWindow);
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
    cellRows.resize(kWindow, nullptr);   // new rows empty; the old ones are kept
    settledList = nullptr;
    active = false;
}

bool LocalSearch::Recenter(Point from, Point to, bool settleInside) {
    const Point oldOrigin = origin;
    if (!PlaceWindow(from, to)) return false;
    FillCellRows(this);
    const Point shift = origin - oldOrigin;
    if (CdqAbs(shift.x) >= 31 || CdqAbs(shift.y) >= 31) {
        // Too far to shift the nodes: the window stays where they are.
        origin = oldOrigin;
        FillCellRows(this);
        return false;
    }
    const SearchGrid<SearchNode> old = nodes;
    // Shift the nodes; remember the cheapest one kept on the old window's inner ring.
    int cheapestBorder = kUnreached;
    for (int wy = 1; wy <= 32; ++wy) {
        int oy = shift.y + wy;
        for (int wx = 1; wx <= 32; ++wx) {
            int ox = shift.x + wx;
            SearchNode* node = NodeAt(this, wx, wy);
            if (ox < 1 || oy < 1 || ox > 32 || oy > 32) {
                node->cost = kUnreached;
                node->settled = false;
                node->direction = 0;
            } else {
                const SearchNode& kept = old[oy][ox];
                if ((ox == 1 || ox == 32 || oy == 1 || oy == 32) && kept.cost <= cheapestBorder) cheapestBorder = kept.cost;
                *node = kept;
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
    return true;
}
