// The path smoother (PathSmoother, CAIMap + 0x22CC): a dynamic programme over the path's cells
// that may move each cell to a neighbour when that makes the path cheaper, counting step costs and
// turns. CAIMap::FindPath runs it on every new path (through its address). See ai_map.h and
// docs/notes/ai.md.
//
// Its transition tables (SmoothTables, 81 of them) are built once by a static initialiser, before
// the hooks are installed: the builder at the end of this file is registered at the original's
// addresses, but in a running game only its exit half (SmoothTable::Destruct) is ever called. What
// it builds is compared with the original's tables once at start-up (tests/verify/
// verify_ai_static_tables.cpp).
#include "ai_map.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/mfc.h"

#include <cstdint>

namespace {

constexpr int kUnreached = 0x7FFFFFFF;
constexpr int kStates = 9;
constexpr float kTurnCost = 60.0f;          // 0073F090
constexpr float kDiagonalEndCost = 2048.0f; // 0073FE40

// The vtables of the tables' arrays: CArray<SmoothCopy>, CArray<SmoothStep>, CArray<SmoothEdge>.
constexpr std::uintptr_t kCopyArrayVtable = std::uintptr_t(0x0073FE10);
constexpr std::uintptr_t kStepArrayVtable = std::uintptr_t(0x0073FE1C);
constexpr std::uintptr_t kEdgeArrayVtable = std::uintptr_t(0x0073FE28);
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

// |v| as CDQ / XOR / SUB computes it.
int CdqAbs(int v) {
    auto u = static_cast<std::uint32_t>(v);
    auto sign = static_cast<std::uint32_t>(v >> 31);
    return static_cast<int>((u ^ sign) - sign);
}

// The cost of `turn` eighths of a turn, as the original rounds it.
int TurnCost(int turn, float scale) { return x87::fistp(static_cast<float>(static_cast<float>(turn) * scale * kTurnCost)); }


} // namespace

// CArray<SmoothNode>::SetSize (mfc::CArray: new nodes zeroed, nothing destroyed).
void SmoothPool::SetSize(int size, int growByArg) { nodes.SetSize(size, growByArg); }
EI_REPLACE_METHOD(0x005C4C20, SmoothPool::SetSize);

void PathSmoother::Relax(const SmoothStep* step, const SmoothState* from, SmoothState* to) {
    if (from->cost == kUnreached) return;
    const AIMapCell& a = map->cells[y + step->dy][x + step->dx];
    const AIMapCell& b = map->cells[y + step->dy + step->stepY][x + step->dx + step->stepX];
    if (b.level > level && b.level >= a.level) return;
    int pass = costs.costs[(b.passable >> (costs.shift & 31)) & 0xF];
    if (pass < 0) return;
    int climb = costs.climb[static_cast<int>(b.groundLevel) - static_cast<int>(a.groundLevel)];
    if (climb < 0) return;
    int cost = (((climb * pass) >> 10) * step->factor >> 10) + from->cost + TurnCost(step->turn, costs.turnScale);
    if (cost >= to->cost) return;
    to->cost = cost;
    to->node->direction = step->direction;
    to->node->prev = from->node;
}
EI_REPLACE_METHOD(0x005C44D0, PathSmoother::Relax);

void PathSmoother::Advance(const SmoothState* from, SmoothState* to, const SmoothTable* table) {
    SmoothPool& pool = SmoothNodePool();
    for (int i = 0; i < kStates; ++i) to[i].cost = kUnreached;
    for (int i = 0; i < kStates; ++i) to[i].node = pool.next++;
    for (int i = 0; i < table->copies.size; ++i) {
        const SmoothCopy& copy = table->copies.data[i];
        to[copy.to] = from[copy.from];
    }
    for (int i = 0; i < table->steps.size; ++i) {
        const SmoothStep& step = table->steps.data[i];
        Relax(&step, &from[step.from], &to[step.to]);
    }
    for (int i = 0; i < table->chained.size; ++i) {
        const SmoothStep& step = table->chained.data[i];
        Relax(&step, &to[step.from], &to[step.to]);
    }
}
EI_REPLACE_METHOD(0x005C45D0, PathSmoother::Advance);

int PathSmoother::Smooth(CUnit* unit, CAIMap* mapArg, PathCellList* path, int startDirection, float fraction) {
    if (path->count < 3) return 0;
    map = mapArg;
    mapArg->CostContextFor(unit, &costs);
    sizeClass = unit->sizeClass;
    level = static_cast<std::uint8_t>(StandingLevel(unit));
    int previousBest = kUnreached;
    const int tailX = path->tail->data.x, tailY = path->tail->data.y;
    const int headX = path->head->data.x, headY = path->head->data.y;
    SmoothPool& pool = SmoothNodePool();
    for (;;) {
        SmoothState first[kStates], second[kStates];
        pool.SetSize((path->count + 5) * 10, -1);
        pool.zeroed = 0;
        for (SmoothState& s : first) s = SmoothState{0, nullptr};
        // The first cell: a state per direction it is left in.
        pool.next = pool.nodes.data;
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
            int pass = costs.costs[(to.passable >> (costs.shift & 31)) & 0xF];
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
        const PathCellList::Node* cell = path->head;
        for (const PathCellList::Node* next = cell->next; next; next = next->next) {
            int direction = DirectionSteps()[(next->data.y - cell->data.y) * 3 - cell->data.x + next->data.x];
            x = cell->data.x;
            y = cell->data.y;
            Advance(from, to, &SmoothTables()[direction + previousDirection * 9]);
            SmoothState* swap = from;
            from = to;
            to = swap;
            previousDirection = direction;
            cell = next;
        }
        x = cell->data.x;
        y = cell->data.y;
        Advance(from, to, &SmoothTables()[previousDirection * 9]);
        // The cheapest end state; a diagonal one pays for the target's offset in its cell.
        int best = kUnreached;
        SmoothNode* bestNode = nullptr;
        for (int d = 1; d <= 8; ++d) {
            SmoothState& state = to[d - 1];
            if (state.cost == kUnreached) continue;
            if (IsDiagonal(d)) state.cost += x87::fistp(static_cast<float>((1.0f - fraction) * kDiagonalEndCost));
            if (state.cost < best) {
                best = state.cost;
                bestNode = state.node;
            }
        }
        if (best >= previousBest) return best;
        previousBest = best;
        // Rebuild the cells from the tail back along the chain. The original gives their speed
        // words and terrain bits two stack slots it never writes (the tail what the path's builder
        // left: the join's open cost or the planner's list vtable); ours get the plain words (SV7d2).
        path->RemoveAll();
        PathCell tail{static_cast<std::int16_t>(tailX), static_cast<std::int16_t>(tailY), kPlainStepSpeed, 0};
        path->AddHead(tail);
        for (const SmoothNode* node = bestNode; node; node = node->prev) {
            PathCell stepped{};
            path->head->data.Step(&stepped, node->direction);
            SetPlainCellWords(&stepped);
            path->AddHead(stepped);
        }
    }
}
EI_REPLACE_METHOD(0x005C46C0, PathSmoother::Smooth);

// ---- The transition tables ----------------------------------------------------------------------

namespace {

// The direction opposite to `direction` (1..8).
int Opposite(int direction) { return direction - 4 >= 1 ? direction - 4 : direction + 4; }

// 005C3840 (thiscall on the array, RET 0x10). The seven states of a cell crossed by the straight
// step (mx, my), with (sx, sy) the step to its side: the step itself, the same step one cell to
// either side, the diagonal moves into and out of the cell on either side.
void __fastcall AddStraightEdges(SmoothEdgeArray* edges, void*, int mx, int my, int sx, int sy) {
    edges->Add(SmoothEdge{0, 0, mx, my, 0});
    edges->Add(SmoothEdge{-sx, -sy, mx - sx, my - sy, 0});
    edges->Add(SmoothEdge{sx, sy, mx + sx, my + sy, 0});
    edges->Add(SmoothEdge{-sx, -sy, mx, my, 0});
    edges->Add(SmoothEdge{0, 0, mx + sx, my + sy, 0});
    edges->Add(SmoothEdge{sx, sy, mx, my, 0});
    edges->Add(SmoothEdge{0, 0, mx - sx, my - sy, 0});
}
EI_REPLACE(0x005C3840, AddStraightEdges);

// 005C3A00 (thiscall on the array, RET 0x10). The nine states of a cell crossed by the diagonal
// step (mx, my), with (sx, sy) one of its two straight halves and (ox, oy) the other.
void __fastcall AddDiagonalEdges(SmoothEdgeArray* edges, void*, int mx, int my, int sx, int sy) {
    const int ox = mx - sx, oy = my - sy;
    edges->Add(SmoothEdge{0, 0, mx, my, 0});
    edges->Add(SmoothEdge{sx, sy, mx + sx, my + sy, 0});
    edges->Add(SmoothEdge{sx - mx, sy - my, sx, sy, 0});
    edges->Add(SmoothEdge{ox, oy, mx + ox, my + oy, 0});
    edges->Add(SmoothEdge{ox - mx, oy - my, ox, oy, 0});
    edges->Add(SmoothEdge{0, 0, sx, sy, 0});
    edges->Add(SmoothEdge{0, 0, ox, oy, 0});
    edges->Add(SmoothEdge{sx, sy, mx, my, 0});
    edges->Add(SmoothEdge{ox, oy, mx, my, 0});
}
EI_REPLACE(0x005C3A00, AddDiagonalEdges);

// 005C4050 (fastcall: the step in ECX, `from` in EDX; RET 4). The step from the state `from` to the
// state `to` that starts where it ends: the move `to` makes, the turn between the two moves less
// one eighth, the cost factor of that turn, and the way back along `to`.
void __fastcall MakeSmoothStep(SmoothStep* step, const SmoothEdge* from, const SmoothEdge* to) {
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
EI_REPLACE(0x005C4050, MakeSmoothStep);

} // namespace

void __fastcall BuildSmoothEdges(SmoothEdgeArray* edges, void*, int direction, bool incoming) {
    edges->SetSize(0, -1);
    switch (direction) {
    case 0:
        for (int d = 1; d <= 8; ++d) {
            PathCell origin{0, 0, 0, 0}, neighbour{0, 0, 0, 0};
            origin.Step(&neighbour, d);
            if (incoming) edges->Add(SmoothEdge{-neighbour.x, -neighbour.y, 0, 0, 0});
            else edges->Add(SmoothEdge{0, 0, neighbour.x, neighbour.y, 0});
        }
        break;
    case 1: AddStraightEdges(edges, nullptr, 0, -1, 1, 0); break;
    case 2: AddDiagonalEdges(edges, nullptr, -1, -1, 0, -1); break;
    case 3: AddStraightEdges(edges, nullptr, -1, 0, 0, -1); break;
    case 4: AddDiagonalEdges(edges, nullptr, -1, 1, -1, 0); break;
    case 5: AddStraightEdges(edges, nullptr, 0, 1, -1, 0); break;
    case 6: AddDiagonalEdges(edges, nullptr, 1, 1, 0, 1); break;
    case 7: AddStraightEdges(edges, nullptr, 1, 0, 0, 1); break;
    case 8: AddDiagonalEdges(edges, nullptr, 1, -1, 1, 0); break;
    default: break;
    }
    for (int i = 0; i < edges->size; ++i) edges->data[i].index = i;
}
EI_REPLACE(0x005C3690, BuildSmoothEdges);

SmoothTable* SmoothTable::Construct() {
    copies.Construct(kCopyArrayVtable);
    steps.Construct(kStepArrayVtable);
    chained.Construct(kStepArrayVtable);
    return this;
}
EI_REPLACE_METHOD(0x005C4210, SmoothTable::Construct);

void SmoothTable::Destruct() {
    chained.vtable = reinterpret_cast<void*>(kStepArrayVtable);
    chained.FreeData();
    steps.vtable = reinterpret_cast<void*>(kStepArrayVtable);
    steps.FreeData();
    copies.vtable = reinterpret_cast<void*>(kCopyArrayVtable);
    copies.FreeData();
}
EI_REPLACE_METHOD(0x005C4250, SmoothTable::Destruct);

void SmoothTable::Build(const SmoothEdgeArray* from, const SmoothEdgeArray* to) {
    copies.SetSize(0, -1);
    steps.SetSize(0, -1);
    chained.SetSize(0, -1);
    for (int i = 0; i < from->size; ++i) {
        for (int j = 0; j < to->size; ++j) {
            const SmoothEdge& a = from->data[i];
            const SmoothEdge& b = to->data[j];
            if (a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1) copies.Add(SmoothCopy{i, j});
        }
    }
    for (int i = 0; i < from->size; ++i) {
        for (int j = 0; j < to->size; ++j) {
            const SmoothEdge& a = from->data[i];
            const SmoothEdge& b = to->data[j];
            if (a.x1 != b.x0 || a.y1 != b.y0) continue;
            SmoothStep step;
            MakeSmoothStep(&step, &a, &b);
            steps.Add(step);
        }
    }
    for (int i = 0; i < to->size; ++i) {
        for (int j = 0; j < to->size; ++j) {
            const SmoothEdge& a = to->data[i];
            const SmoothEdge& b = to->data[j];
            if (a.x1 != b.x0 || a.y1 != b.y0) continue;
            SmoothStep step;
            MakeSmoothStep(&step, &a, &b);
            chained.Add(step);
        }
    }
}
EI_REPLACE_METHOD(0x005C3E10, SmoothTable::Build);

void BuildSmoothTablesAt(SmoothTable* tables) {
    for (int previous = 0; previous < kStates; ++previous) {
        for (int next = 0; next < kStates; ++next) {
            SmoothEdgeArray from, to;
            from.Construct(kEdgeArrayVtable);
            to.Construct(kEdgeArrayVtable);
            BuildSmoothEdges(&from, nullptr, previous, false);
            BuildSmoothEdges(&to, nullptr, next, true);
            if (previous != 0) {
                // The previous step's states are relative to the cell it left.
                PathCell origin{0, 0, 0, 0}, back{0, 0, 0, 0};
                origin.Step(&back, Opposite(previous));
                for (int i = 0; i < from.size; ++i) {
                    SmoothEdge& edge = from.data[i];
                    edge.x0 += back.x;
                    edge.y0 += back.y;
                    edge.x1 += back.x;
                    edge.y1 += back.y;
                }
            }
            tables[previous * kStates + next].Build(&from, &to);
            to.FreeData();
            from.FreeData();
        }
    }
}

// 005C42B0 (cdecl, no arguments; the static initialiser's second half, after the 81 tables have
// been constructed empty).
void __cdecl BuildSmoothTables() { BuildSmoothTablesAt(const_cast<SmoothTable*>(SmoothTables())); }
EI_REPLACE(0x005C42B0, BuildSmoothTables);
