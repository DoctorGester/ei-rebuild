// CAIMap::JoinLocalSearches: the bidirectional local search's meeting. The forward and backward searches
// expand in turn; after each round the best open cell of one side is looked up in the other, first forward then
// backward, until a cell reached by both is found (or a side has nothing left: false). Then the forward
// search's path to the cell goes into `path`, the backward one's into a scratch list, which is reversed and
// appended, and the result fixed up; `cost` (optional) gets the summed cost. Both searches are reset either
// way. See ai_map.h and docs/notes/ai.md.
#include "ai_map.h"

#include "lib/mfc.h"

#include <cstdint>

namespace {

// The local search: expand one round, the best open cell (x, y, cost), the cost of a cell (0x7FFFFFFF
// unreached), the path to a cell into a list, reset. The path cell list: a reversed copy into `out`, append a
// list, and the fix-up of the joined path.

constexpr int kUnreached = 0x7FFFFFFF;

} // namespace

bool CAIMap::JoinLocalSearches(PathCellList* path, int* cost) {
    LocalSearch* forward = &forwardSearch;
    LocalSearch* backward = &backwardSearch;
    bool ok = true;
    int x = 0, y = 0, open = 0, other = 0;
    for (;;) {
        forward->Expand();
        backward->Expand();
        forward->Best(&x, &y, &open);
        if (open == kUnreached) {
            ok = false;
            break;
        }
        other = backward->CostAt(x, y);
        if (other < kUnreached) break;
        backward->Best(&x, &y, &open);
        if (open == kUnreached) {
            ok = false;
            break;
        }
        other = forward->CostAt(x, y);
        if (other < kUnreached) break;
    }
    PathCellList scratch;
    if (ok) {
        if (cost) *cost = other + open;
        forward->PathTo(x, y, path);
        backward->PathTo(x, y, &scratch);
        PathCellList reversed;
        path->Append(scratch.Reversed(&reversed));
        PathCellsRemoveRepeats(path);
        // Like the original, the cells keep the stack word under SearchPathTo's local cell (the forward search's
        // address); ours get the plain words.
        SetPlainCellWords(path);
    }
    backward->Reset();
    forward->Reset();
    return ok;
}

void CAIMap::BuildCellsTo(int x, int y, PathCellList* path) {
    if (x < 0 || x >= cells.width || y < 0 || y >= cells.height) return;
    // The original gives each cell's speed word and terrain bits a stack slot it never writes (the planner's last
    // call left a search's row offset there); ours get the plain words.
    auto cellAt = [](int cx, int cy) {
        PathCell c{static_cast<std::int16_t>(cx), static_cast<std::int16_t>(cy), kPlainStepSpeed, 0};
        return c;
    };
    // Walk back along the marks (directions 1..8; 0 ends), each cell added at the head.
    while (int dir = marks.rows[y][x]) {
        path->push_front(cellAt(x, y));
        switch (dir) {
        case 1: --y; break;
        case 2: --y; --x; break;
        case 3: --x; break;
        case 4: ++y; --x; break;
        case 5: ++y; break;
        case 6: ++y; ++x; break;
        case 7: ++x; break;
        case 8: --y; ++x; break;
        default: break;
        }
    }
    path->push_front(cellAt(x, y));
}
