// The path smoother (PathSmoother, in AIMap): a dynamic programme over the path's cells that may move each
// cell to a neighbour when that makes the path cheaper, counting step costs and turns. Its 81 transition tables
// are built on first use. See ai_map.h and docs/notes/ai.md.
#include "ai_map.h"

#include "lib/rounding.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace {

constexpr int kUnreached = 0x7FFFFFFF;
constexpr int kStates = 9;
constexpr float kTurnCost = 60.0f;
constexpr float kDiagonalEndCost = 2048.0f;

// A step's cost factor in 1/1024 by its turn against the previous step, in eighths: straight on
// (1 or the diagonal's length), 1, 2 (again by whether the previous step was diagonal), 3; a full
// turn back costs nothing.
constexpr int kStraightFactor = 0x400;
constexpr int kDiagonalFactor = 0x5A8;
constexpr int kEighthTurnFactor = 0x479;
constexpr int kQuarterTurnFactor = 0x46E;
constexpr int kDiagonalQuarterTurnFactor = 0x648;
constexpr int kWideTurnFactor = 0x200;

bool IsDiagonal(int direction) { return direction == 2 || direction == 4 || direction == 6 || direction == 8; }

int CdqAbs(int v) {
    auto u = static_cast<std::uint32_t>(v);
    auto sign = static_cast<std::uint32_t>(v >> 31);
    return static_cast<int>((u ^ sign) - sign);
}

// The cost of `turn` eighths of a turn, rounded like the original.
int TurnCost(int turn, float scale) { return lib::RoundEven(static_cast<float>(turn) * scale * kTurnCost); }

int Opposite(int direction) { return direction - 4 >= 1 ? direction - 4 : direction + 4; }

// The step from the state `from` to the state `to` that starts where it ends: the move `to` makes, the turn
// between the two moves less one eighth, the cost factor of that turn, and the way back along `to`.
void MakeSmoothStep(SmoothStep* step, const SmoothEdge* from, const SmoothEdge* to) {
    const int fromDirection = DirectionSteps()[(from->y1 - from->y0) * 3 + from->x1 - from->x0];
    const int toDirection = DirectionSteps()[(to->y1 - to->y0) * 3 - to->x0 + to->x1];
    step->from = from->index;
    step->to = to->index;
    step->stepX = to->x1 - to->x0;
    step->stepY = to->y1 - to->y0;
    step->dx = to->x0;
    step->dy = to->y0;
    int turn = CdqAbs(fromDirection - toDirection);
    if (turn > 4) turn = 8 - turn;
    step->turn = turn - 1 < 0 ? 0 : turn - 1;
    switch (turn) {
    case 0: step->factor = IsDiagonal(fromDirection) ? kDiagonalFactor : kStraightFactor; break;
    case 1: step->factor = kEighthTurnFactor; break;
    case 2: step->factor = IsDiagonal(fromDirection) ? kDiagonalQuarterTurnFactor : kQuarterTurnFactor; break;
    case 3: step->factor = kWideTurnFactor; break;
    default: step->factor = 0; break;
    }
    step->direction = toDirection == 0 ? 0 : Opposite(toDirection);
}

} // namespace

// Resizes the SmoothNode array (new nodes zeroed, nothing destroyed).
void SmoothPool::SetSize(int size) { nodes.resize(size); }

void PathSmoother::Relax(const SmoothStep* step, const SmoothState* from, SmoothState* to) {
    if (from->cost == kUnreached) return;
    const AIMapCell& a = map->cells[y + step->dy][x + step->dx];
    const AIMapCell& b = map->cells[y + step->dy + step->stepY][x + step->dx + step->stepX];
    if (b.level > level && b.level >= a.level) return;
    int pass = costs.costs[b.Passability(costs.shift)];
    if (pass < 0) return;
    int climb = costs.climb[static_cast<int>(b.groundLevel) - static_cast<int>(a.groundLevel)];
    if (climb < 0) return;
    int cost = (((climb * pass) >> 10) * step->factor >> 10) + from->cost + TurnCost(step->turn, costs.turnScale);
    if (cost >= to->cost) return;
    to->cost = cost;
    to->node->direction = step->direction;
    to->node->prev = from->node;
}

void PathSmoother::Advance(const SmoothState* from, SmoothState* to, const SmoothTable* table) {
    SmoothPool& pool = g_smoothPool;
    for (int i = 0; i < kStates; ++i) to[i].cost = kUnreached;
    for (int i = 0; i < kStates; ++i) to[i].node = pool.next++;
    for (int i = 0; i < std::ssize(table->copies); ++i) {
        const SmoothCopy& copy = table->copies[i];
        to[copy.to] = from[copy.from];
    }
    for (int i = 0; i < std::ssize(table->steps); ++i) {
        const SmoothStep& step = table->steps[i];
        Relax(&step, &from[step.from], &to[step.to]);
    }
    for (int i = 0; i < std::ssize(table->chained); ++i) {
        const SmoothStep& step = table->chained[i];
        Relax(&step, &to[step.from], &to[step.to]);
    }
}

int PathSmoother::Smooth(CUnit* unit, AIMap* mapArg, PathCells* path, int startDirection, float fraction) {
    if (std::ssize(*path) < 3) return 0;
    map = mapArg;
    mapArg->CostContextFor(unit, &costs);
    sizeClass = unit->sizeClass;
    level = static_cast<std::uint8_t>(StandingLevel(unit));
    int previousBest = kUnreached;
    const int tailX = path->back().x, tailY = path->back().y;
    const int headX = path->front().x, headY = path->front().y;
    SmoothPool& pool = g_smoothPool;
    for (;;) {
        SmoothState first[kStates], second[kStates];
        pool.SetSize((std::ssize(*path) + 5) * 10);
        pool.zeroed = 0;
        for (SmoothState& s : first) s = SmoothState{0, nullptr};
        pool.next = pool.nodes.data();
        const AIMapCell& head = map->cells[headY][headX];
        for (int d = 1; d <= 8; ++d) {
            SmoothState& state = first[d - 1];
            state.node = pool.next++;
            state.node->direction = d - 4 >= 1 ? d - 4 : d + 4;
            state.node->prev = nullptr;
            state.cost = kUnreached;
            PathCell origin{0, 0, 0, 0}, offset;
            origin.Step(&offset, d);
            const AIMapCell& to = map->cells[headY + offset.y][headX + offset.x];
            int pass = costs.costs[to.Passability(costs.shift)];
            if (to.level > level && to.level >= head.level) continue;
            if (pass < 0) continue;
            int climb = costs.climb[static_cast<int>(to.groundLevel) - static_cast<int>(head.groundLevel)];
            if (climb <= 0) continue;
            int cost = (climb * pass) >> 10;
            if (IsDiagonal(d)) cost = (cost * 0x5A8) >> 10;
            int turn = CdqAbs(d - startDirection);
            if (turn > 4) turn = 8 - turn;
            int turns = turn - 1 < 0 ? 0 : turn - 1;
            state.cost = TurnCost(turns, costs.turnScale) + cost;
        }
        // Along the path: each cell's states from the previous one's, through the table of the
        // previous and the next step's directions.
        SmoothState* from = first;
        SmoothState* to = second;
        int previousDirection = 0;
        auto cell = path->begin();
        for (auto next = std::next(cell); next != path->end(); ++next) {
            int direction = DirectionSteps()[(next->y - cell->y) * 3 - cell->x + next->x];
            x = cell->x;
            y = cell->y;
            Advance(from, to, &SmoothTables()[direction + previousDirection * 9]);
            SmoothState* swap = from;
            from = to;
            to = swap;
            previousDirection = direction;
            cell = next;
        }
        x = cell->x;
        y = cell->y;
        Advance(from, to, &SmoothTables()[previousDirection * 9]);
        // The cheapest end state; a diagonal one pays for the target's offset in its cell.
        int best = kUnreached;
        SmoothNode* bestNode = nullptr;
        for (int d = 1; d <= 8; ++d) {
            SmoothState& state = to[d - 1];
            if (state.cost == kUnreached) continue;
            if (IsDiagonal(d)) state.cost += lib::RoundEven((1.0f - fraction) * kDiagonalEndCost);
            if (state.cost < best) {
                best = state.cost;
                bestNode = state.node;
            }
        }
        if (best >= previousBest) return best;
        previousBest = best;
        // Rebuild the cells from the tail back along the chain. The original gives their speed words and terrain bits
        // two stack values it never writes (the tail what the path's builder left: the join's open cost or the
        // planner's list vtable); ours get the plain words.
        path->clear();
        PathCell tail{static_cast<std::int16_t>(tailX), static_cast<std::int16_t>(tailY), kPlainStepSpeed, 0};
        path->push_back(tail);
        for (const SmoothNode* node = bestNode; node; node = node->prev) {
            PathCell stepped{};
            path->back().Step(&stepped, node->direction);
            SetPlainCellWords(&stepped);
            path->push_back(stepped);
        }
        std::ranges::reverse(*path);
    }
}

// ---- The transition tables ----------------------------------------------------------------------

namespace {

// The seven states of a cell crossed by the straight step (mx, my), with (sx, sy) the step to its side: the
// step itself, the same step one cell to either side, the diagonal moves into and out of the cell on either
// side.
std::vector<SmoothEdge> StraightEdges(int mx, int my, int sx, int sy) {
    return {
        SmoothEdge{0, 0, mx, my, 0},
        SmoothEdge{-sx, -sy, mx - sx, my - sy, 0},
        SmoothEdge{sx, sy, mx + sx, my + sy, 0},
        SmoothEdge{-sx, -sy, mx, my, 0},
        SmoothEdge{0, 0, mx + sx, my + sy, 0},
        SmoothEdge{sx, sy, mx, my, 0},
        SmoothEdge{0, 0, mx - sx, my - sy, 0},
    };
}

// The nine states of a cell crossed by the diagonal step (mx, my), with (sx, sy) one of its two straight
// halves and (ox, oy) the other.
std::vector<SmoothEdge> DiagonalEdges(int mx, int my, int sx, int sy) {
    const int ox = mx - sx, oy = my - sy;
    return {
        SmoothEdge{0, 0, mx, my, 0},
        SmoothEdge{sx, sy, mx + sx, my + sy, 0},
        SmoothEdge{sx - mx, sy - my, sx, sy, 0},
        SmoothEdge{ox, oy, mx + ox, my + oy, 0},
        SmoothEdge{ox - mx, oy - my, ox, oy, 0},
        SmoothEdge{0, 0, sx, sy, 0},
        SmoothEdge{0, 0, ox, oy, 0},
        SmoothEdge{sx, sy, mx, my, 0},
        SmoothEdge{ox, oy, mx, my, 0},
    };
}

// The states of a cell the path crosses in `direction` (1..8): seven moves for a straight step, nine for a
// diagonal one. Direction 0 (the path's ends) gives the eight moves from the cell to its neighbours, or from the
// neighbours to it when `incoming`. Each state's index is its place.
std::vector<SmoothEdge> EdgesOf(int direction, bool incoming) {
    std::vector<SmoothEdge> edges;
    switch (direction) {
    case 0:
        for (int d = 1; d <= 8; ++d) {
            PathCell origin{0, 0, 0, 0}, neighbour{0, 0, 0, 0};
            origin.Step(&neighbour, d);
            if (incoming) edges.push_back(SmoothEdge{-neighbour.x, -neighbour.y, 0, 0, 0});
            else edges.push_back(SmoothEdge{0, 0, neighbour.x, neighbour.y, 0});
        }
        break;
    case 1: edges = StraightEdges(0, -1, 1, 0); break;
    case 2: edges = DiagonalEdges(-1, -1, 0, -1); break;
    case 3: edges = StraightEdges(-1, 0, 0, -1); break;
    case 4: edges = DiagonalEdges(-1, 1, -1, 0); break;
    case 5: edges = StraightEdges(0, 1, -1, 0); break;
    case 6: edges = DiagonalEdges(1, 1, 0, 1); break;
    case 7: edges = StraightEdges(1, 0, 0, 1); break;
    case 8: edges = DiagonalEdges(1, -1, 1, 0); break;
    default: break;
    }
    for (int i = 0; i < std::ssize(edges); ++i) edges[i].index = i;
    return edges;
}

} // namespace

void SmoothTable::Build(const std::vector<SmoothEdge>& from, const std::vector<SmoothEdge>& to) {
    copies.clear();
    steps.clear();
    chained.clear();
    for (int i = 0; i < std::ssize(from); ++i) {
        for (int j = 0; j < std::ssize(to); ++j) {
            const SmoothEdge& a = from[i];
            const SmoothEdge& b = to[j];
            if (a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1) copies.push_back(SmoothCopy{i, j});
        }
    }
    for (int i = 0; i < std::ssize(from); ++i) {
        for (int j = 0; j < std::ssize(to); ++j) {
            const SmoothEdge& a = from[i];
            const SmoothEdge& b = to[j];
            if (a.x1 != b.x0 || a.y1 != b.y0) continue;
            SmoothStep step;
            MakeSmoothStep(&step, &a, &b);
            steps.push_back(step);
        }
    }
    for (int i = 0; i < std::ssize(to); ++i) {
        for (int j = 0; j < std::ssize(to); ++j) {
            const SmoothEdge& a = to[i];
            const SmoothEdge& b = to[j];
            if (a.x1 != b.x0 || a.y1 != b.y0) continue;
            SmoothStep step;
            MakeSmoothStep(&step, &a, &b);
            chained.push_back(step);
        }
    }
}

namespace {

using SmoothTableSet = std::array<SmoothTable, kStates * kStates>;

// The tables as the original's static initialiser builds them: for every pair of directions the states of the
// previous step (moved back by that step) against those of the next.
SmoothTableSet BuildSmoothTables() {
    SmoothTableSet tables;
    for (int previous = 0; previous < kStates; ++previous) {
        for (int next = 0; next < kStates; ++next) {
            std::vector<SmoothEdge> from = EdgesOf(previous, false);
            std::vector<SmoothEdge> to = EdgesOf(next, true);
            if (previous != 0) {
                // The previous step's states are relative to the cell it left.
                PathCell origin{0, 0, 0, 0}, back{0, 0, 0, 0};
                origin.Step(&back, Opposite(previous));
                for (int i = 0; i < std::ssize(from); ++i) {
                    SmoothEdge& edge = from[i];
                    edge.x0 += back.x;
                    edge.y0 += back.y;
                    edge.x1 += back.x;
                    edge.y1 += back.y;
                }
            }
            tables[previous * kStates + next].Build(from, to);
        }
    }
    return tables;
}

} // namespace

const SmoothTable* SmoothTables() {
    static const SmoothTableSet tables = BuildSmoothTables();
    return tables.data();
}

// ---- The pool ---------------------------------------------------------------------------------

SmoothPool g_smoothPool;
