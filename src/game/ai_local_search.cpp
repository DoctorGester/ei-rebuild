// The local search (LocalSearch, ai_map.h): Dijkstra over a 34 x 34 window of AI map cells, used
// for short paths (CAIMap::JoinLocalSearches runs a forward and a backward one until they meet),
// the long-range planner's legs and the region map's block measurements. See docs/notes/ai.md.
#include "ai_map.h"

#include "ei/hook.h"
#include "lib/mfc.h"

#include <cstdint>

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
