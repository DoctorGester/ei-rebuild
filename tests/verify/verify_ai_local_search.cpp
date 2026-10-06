// Checks for the local search (src/game/ai_local_search.cpp). The search lives in the AI map, its
// nodes in one block of 34 x 34 records linked by pointers into that block, so the state functions
// are compared in place: the search's 0x80 bytes and its node block are recorded, the original
// runs, its result is recorded, the first state is put back and ours runs; both results compared.
// The hottest (Expand, Requeue) compare the first 512 calls and then every 32nd. Pure lookups run
// side by side. Reached by every short path (movement, ui_order_move, unit_ai) and the region map's
// block measurements while a map loads (every scenario).
#include "verify.h"

#include "../../src/game/ai_map.h"
#include "../../src/lib/mfc.h"

#include <cstring>
#include <vector>

namespace {

// The search's bytes, its node block, its row table and (when `cells`) the map bytes under the
// window that the search may write: the cells' levels and the direction marks.
struct SearchState {
    unsigned char header[sizeof(LocalSearch)];
    std::vector<SearchNode> nodes;
    std::vector<AIMapCell*> rows;
    std::vector<unsigned char> cells;
};

// Calls fn(level byte, mark byte or null) for every map cell under the window.
template <typename Fn>
void ForWindowCells(const LocalSearch* search, Fn fn) {
    CAIMap* map = search->map;
    if (!map || !map->cells) return;
    for (int wy = 0; wy < 34; ++wy) {
        int y = search->originY + wy;
        if (y < 0 || y >= map->height) continue;
        for (int wx = 0; wx < 34; ++wx) {
            int x = search->originX + wx;
            if (x < 0 || x >= map->width) continue;
            fn(map->cells[y][x].level, map->marks.rows ? &map->marks.rows[y][x] : nullptr);
        }
    }
}

void Capture(const LocalSearch* search, SearchState& state, bool cells) {
    std::memcpy(state.header, search, sizeof(LocalSearch));
    int count = search->nodes.data ? search->nodes.width * search->nodes.height : 0;
    state.nodes.assign(search->nodes.data, search->nodes.data + count);
    int rowCount = search->cellRows ? search->cellRowCount : 0;
    state.rows.assign(search->cellRows, search->cellRows + rowCount);
    state.cells.clear();
    if (cells) {
        ForWindowCells(search, [&](std::uint8_t& level, std::int8_t* mark) {
            state.cells.push_back(level);
            state.cells.push_back(mark ? static_cast<unsigned char>(*mark) : 0);
        });
    }
}

// Puts the state back; the map bytes go to the window recorded in `state` (its origin).
void Restore(LocalSearch* search, const SearchState& state) {
    std::memcpy(static_cast<void*>(search), state.header, sizeof(LocalSearch));
    if (!state.nodes.empty()) std::memcpy(search->nodes.data, state.nodes.data(), state.nodes.size() * sizeof(SearchNode));
    if (!state.rows.empty()) std::memcpy(search->cellRows, state.rows.data(), state.rows.size() * sizeof(AIMapCell*));
    if (state.cells.empty()) return;
    std::size_t i = 0;
    ForWindowCells(search, [&](std::uint8_t& level, std::int8_t* mark) {
        level = state.cells[i++];
        auto m = static_cast<std::int8_t>(state.cells[i++]);
        if (mark) *mark = m;
    });
}

bool Same(const SearchState& a, const SearchState& b) {
    return std::memcmp(a.header, b.header, sizeof(a.header)) == 0 && a.nodes.size() == b.nodes.size() &&
           std::memcmp(a.nodes.data(), b.nodes.data(), a.nodes.size() * sizeof(SearchNode)) == 0 && a.rows == b.rows &&
           a.cells == b.cells;
}

// The index of the first differing node (or -1 for the header), for the log.
int FirstDifference(const SearchState& a, const SearchState& b) {
    for (std::size_t i = 0; i < a.nodes.size() && i < b.nodes.size(); ++i) {
        if (std::memcmp(&a.nodes[i], &b.nodes[i], sizeof(SearchNode)) != 0) return static_cast<int>(i);
    }
    return -1;
}

bool Sample(unsigned& calls) { return calls++ < 512 || (calls & 31) == 0; }

// Runs `original` and `ours` from the same state of `search` and compares the states after.
// With `cells` the map bytes under the window are included (for functions that keep the window).
template <typename Original, typename Ours>
void InPlace(const char* name, LocalSearch* search, Original original, Ours ours, bool cells = false) {
    SearchState before, expected, actual;
    Capture(search, before, cells);
    original();
    Capture(search, expected, cells);
    Restore(search, before);
    ours();
    Capture(search, actual, cells);
    ei::verify::Check(name, true, Same(expected, actual), "search %p: first differing node %d", search,
                      FirstDifference(expected, actual));
}

std::vector<int> Cells(const PathCellList* list) {
    std::vector<int> out;
    for (const PathCellList::Node* n = list->head; n; n = n->next) {
        out.push_back(n->data.x);
        out.push_back(n->data.y);
    }
    return out;
}

using VoidFn = void(__fastcall*)(LocalSearch*, void*);
VoidFn s_reset, s_pick, s_expand, s_expandAll;
using RequeueFn = void(__fastcall*)(LocalSearch*, void*, SearchNode*, int);
RequeueFn s_requeue;
using BestFn = void(__fastcall*)(LocalSearch*, void*, int*, int*, int*);
BestFn s_best;
using CostAtFn = int(__fastcall*)(LocalSearch*, void*, int, int);
CostAtFn s_costAt;
using StepCostFn = int(__fastcall*)(LocalSearch*, void*, const SearchNode*, int);
StepCostFn s_stepCost;
using UntilFn = bool(__fastcall*)(LocalSearch*, void*, int, int);
UntilFn s_until;
using PathToFn = void(__fastcall*)(LocalSearch*, void*, int, int, PathCellList*);
PathToFn s_pathTo;

} // namespace

// 005C35F0.
static void __fastcall ResetVerify(LocalSearch* self, void*) {
    InPlace("LocalSearch::Reset", self, [&] { s_reset(self, nullptr); }, [&] { self->Reset(); });
}
EI_VERIFY(0x005C35F0, ResetVerify, 6, s_reset);

// 005C2F50.
static void __fastcall PickCurrentVerify(LocalSearch* self, void*) {
    SearchNode* before = self->current;
    s_pick(self, nullptr);
    SearchNode* expected = self->current;
    self->current = before;
    self->PickCurrent();
    ei::verify::Check("LocalSearch::PickCurrent", expected, self->current, "search %p", self);
}
EI_VERIFY(0x005C2F50, PickCurrentVerify, 6, s_pick);

// 005C2FB0.
static void __fastcall RequeueVerify(LocalSearch* self, void*, SearchNode* node, int cost) {
    static unsigned calls;
    if (!Sample(calls)) return self->Requeue(node, cost);
    InPlace("LocalSearch::Requeue", self, [&] { s_requeue(self, nullptr, node, cost); },
            [&] { self->Requeue(node, cost); });
}
EI_VERIFY(0x005C2FB0, RequeueVerify, 5, s_requeue);

// 005C3050: outputs only (x and y are written only when there is a current node).
static void __fastcall BestVerify(LocalSearch* self, void*, int* x, int* y, int* cost) {
    int ex = *x, ey = *y, ec = 0;
    s_best(self, nullptr, &ex, &ey, &ec);
    self->Best(x, y, cost);
    int expected[3] = {ex, ey, ec}, actual[3] = {*x, *y, *cost};
    ei::verify::Check("LocalSearch::Best", true, std::memcmp(expected, actual, sizeof(expected)) == 0,
                      "(%d,%d,%d) original (%d,%d,%d)", *x, *y, *cost, ex, ey, ec);
}
EI_VERIFY(0x005C3050, BestVerify, 7, s_best);

// 005C3090: pure.
static int __fastcall CostAtVerify(LocalSearch* self, void*, int x, int y) {
    int expected = s_costAt(self, nullptr, x, y);
    int actual = self->CostAt(x, y);
    ei::verify::Check("LocalSearch::CostAt", expected, actual, "(%d,%d)", x, y);
    return expected;
}
EI_VERIFY(0x005C3090, CostAtVerify, 7, s_costAt);

// 005C1D50: pure.
static int __fastcall StepCostVerify(LocalSearch* self, void*, const SearchNode* node, int factor) {
    int expected = s_stepCost(self, nullptr, node, factor);
    int actual = self->StepCost(node, factor);
    ei::verify::Check("LocalSearch::StepCost", expected, actual, "node (%d,%d) factor %d backward %d", node->x,
                      node->y, factor, self->backward);
    return expected;
}
EI_VERIFY(0x005C1D50, StepCostVerify, 7, s_stepCost);

// 005C2D40.
static void __fastcall ExpandVerify(LocalSearch* self, void*) {
    static unsigned calls;
    if (!Sample(calls)) return self->Expand();
    InPlace("LocalSearch::Expand", self, [&] { s_expand(self, nullptr); }, [&] { self->Expand(); });
}
EI_VERIFY(0x005C2D40, ExpandVerify, 7, s_expand);

// 005C30E0.
static void __fastcall ExpandAllVerify(LocalSearch* self, void*) {
    InPlace("LocalSearch::ExpandAll", self, [&] { s_expandAll(self, nullptr); }, [&] { self->ExpandAll(); });
}
EI_VERIFY(0x005C30E0, ExpandAllVerify, 6, s_expandAll);

// 005C31A0: the state and AL compared.
static bool __fastcall ExpandUntilVerify(LocalSearch* self, void*, int x, int y) {
    bool expected = false, actual = false;
    InPlace("LocalSearch::ExpandUntil", self, [&] { expected = s_until(self, nullptr, x, y); },
            [&] { actual = self->ExpandUntil(x, y); });
    ei::verify::Check("LocalSearch::ExpandUntil result", expected, actual, "(%d,%d)", x, y);
    return actual;
}
EI_VERIFY(0x005C31A0, ExpandUntilVerify, 6, s_until);

// 005C3200: the original fills a scratch list, ours the caller's; the cells' x, y compared in order
// (the second dword is a stale stack slot in the original, see PathTo).
static void __fastcall PathToVerify(LocalSearch* self, void*, int x, int y, PathCellList* out) {
    PathCellList scratch{out->vtable, nullptr, nullptr, 0, nullptr, nullptr, out->blockSize};
    s_pathTo(self, nullptr, x, y, &scratch);
    std::vector<int> expected = Cells(&scratch);
    mfc::PlexFreeDataChain(static_cast<mfc::CPlex*>(scratch.blocks));
    self->PathTo(x, y, out);
    std::vector<int> actual = Cells(out);
    ei::verify::Check("LocalSearch::PathTo", true, actual == expected, "(%d,%d): %d cells (original %d)", x, y,
                      static_cast<int>(actual.size() / 2), static_cast<int>(expected.size() / 2));
}
EI_VERIFY(0x005C3200, PathToVerify, 5, s_pathTo);

namespace {
using PlaceFn = bool(__fastcall*)(LocalSearch*, void*, int, int, int, int);
PlaceFn s_place;
using SetUnitFn = void(__fastcall*)(LocalSearch*, void*, CUnit*);
SetUnitFn s_setUnit;
using BeginFn = bool(__fastcall*)(LocalSearch*, void*, CUnit*, int, int, int, int);
BeginFn s_begin;
using BeginClassFn = bool(__fastcall*)(LocalSearch*, void*, int, int, int, int, int);
BeginClassFn s_beginClass;
using RectFn = bool(__fastcall*)(LocalSearch*, void*, int, int, int, int, int, int);
RectFn s_rect;
using RectUnitFn = bool(__fastcall*)(LocalSearch*, void*, CUnit*, int, int, int, int, int, int);
RectUnitFn s_rectUnit;
using RectClassFn = bool(__fastcall*)(LocalSearch*, void*, int, int, int, int, int, int, int);
RectClassFn s_rectClass;
using SaveLevelsFn = void(__fastcall*)(LocalSearch*, void*, bool);
SaveLevelsFn s_saveLevels;
VoidFn s_commit, s_restoreLevels;
using RecenterFn = bool(__fastcall*)(LocalSearch*, void*, int, int, int, int, bool);
RecenterFn s_recenter;
using CostForFn = void(__fastcall*)(CAIMap*, void*, CUnit*, PassCostContext*);
CostForFn s_costFor;
void* s_unusedAttach;

template <typename R, typename Original, typename Ours>
R InPlaceResult(const char* name, LocalSearch* search, Original original, Ours ours, bool cells = false) {
    R expected{}, actual{};
    InPlace(name, search, [&] { expected = original(); }, [&] { actual = ours(); }, cells);
    ei::verify::Check(name, expected, actual, "result");
    return actual;
}
} // namespace

// 005C22A0.
static bool __fastcall PlaceWindowVerify(LocalSearch* self, void*, int x0, int y0, int x1, int y1) {
    return InPlaceResult<bool>("LocalSearch::PlaceWindow", self, [&] { return s_place(self, nullptr, x0, y0, x1, y1); },
                               [&] { return self->PlaceWindow(x0, y0, x1, y1); });
}
EI_VERIFY(0x005C22A0, PlaceWindowVerify, 5, s_place);

// 005C21A0.
static void __fastcall SetUnitVerify(LocalSearch* self, void*, CUnit* unit) {
    InPlace("LocalSearch::SetUnit", self, [&] { s_setUnit(self, nullptr, unit); }, [&] { self->SetUnit(unit); });
}
EI_VERIFY(0x005C21A0, SetUnitVerify, 8, s_setUnit);

// 005C2420.
static bool __fastcall BeginVerify(LocalSearch* self, void*, CUnit* unit, int x0, int y0, int x1, int y1) {
    return InPlaceResult<bool>("LocalSearch::Begin", self, [&] { return s_begin(self, nullptr, unit, x0, y0, x1, y1); },
                               [&] { return self->Begin(unit, x0, y0, x1, y1); });
}
EI_VERIFY(0x005C2420, BeginVerify, 5, s_begin);

// 005C24F0.
static bool __fastcall BeginForClassVerify(LocalSearch* self, void*, int sizeClass, int x0, int y0, int x1, int y1) {
    return InPlaceResult<bool>("LocalSearch::BeginForClass", self,
                               [&] { return s_beginClass(self, nullptr, sizeClass, x0, y0, x1, y1); },
                               [&] { return self->BeginForClass(sizeClass, x0, y0, x1, y1); });
}
EI_VERIFY(0x005C24F0, BeginForClassVerify, 5, s_beginClass);

// 005C2B10.
static bool __fastcall BeginInRectVerify(LocalSearch* self, void*, int x0, int y0, int x1, int y1, int sx, int sy) {
    return InPlaceResult<bool>("LocalSearch::BeginInRect", self,
                               [&] { return s_rect(self, nullptr, x0, y0, x1, y1, sx, sy); },
                               [&] { return self->BeginInRect(x0, y0, x1, y1, sx, sy); });
}
EI_VERIFY(0x005C2B10, BeginInRectVerify, 5, s_rect);

// 005C2C90.
static bool __fastcall BeginInRectForVerify(LocalSearch* self, void*, CUnit* unit, int x0, int y0, int x1, int y1, int sx,
                                            int sy) {
    return InPlaceResult<bool>("LocalSearch::BeginInRectFor", self,
                               [&] { return s_rectUnit(self, nullptr, unit, x0, y0, x1, y1, sx, sy); },
                               [&] { return self->BeginInRectFor(unit, x0, y0, x1, y1, sx, sy); });
}
EI_VERIFY(0x005C2C90, BeginInRectForVerify, 8, s_rectUnit);

// 005C2CF0.
static bool __fastcall BeginInRectForClassVerify(LocalSearch* self, void*, int sizeClass, int x0, int y0, int x1, int y1,
                                                 int sx, int sy) {
    return InPlaceResult<bool>("LocalSearch::BeginInRectForClass", self,
                               [&] { return s_rectClass(self, nullptr, sizeClass, x0, y0, x1, y1, sx, sy); },
                               [&] { return self->BeginInRectForClass(sizeClass, x0, y0, x1, y1, sx, sy); });
}
EI_VERIFY(0x005C2CF0, BeginInRectForClassVerify, 8, s_rectClass);

// 005C2A30: the map's marks under the window included.
static void __fastcall CommitDirectionsVerify(LocalSearch* self, void*) {
    InPlace("LocalSearch::CommitDirections", self, [&] { s_commit(self, nullptr); }, [&] { self->CommitDirections(); },
            true);
}
EI_VERIFY(0x005C2A30, CommitDirectionsVerify, 5, s_commit);

// 005C33F0: the cell levels under the window and the saved grid compared. The first call
// allocates the grid: ours only, then the saved levels must be there.
static void __fastcall SaveLevelsVerify(LocalSearch* self, void*, bool clear) {
    if (self->levels.width != 34 || self->levels.height != 34) {
        self->SaveLevels(clear);
        bool ok = self->levelsSaved && self->levels.data != nullptr;
        ei::verify::Check("LocalSearch::SaveLevels (allocating)", true, ok, "search %p", self);
        return;
    }
    std::vector<unsigned char> expectedLevels, actualLevels;
    InPlace("LocalSearch::SaveLevels", self,
            [&] {
                s_saveLevels(self, nullptr, clear);
                expectedLevels.assign(self->levels.data, self->levels.data + 34 * 34);
            },
            [&] {
                self->SaveLevels(clear);
                actualLevels.assign(self->levels.data, self->levels.data + 34 * 34);
            },
            true);
    ei::verify::Check("LocalSearch::SaveLevels grid", true, expectedLevels == actualLevels, "clear %d", clear);
}
EI_VERIFY(0x005C33F0, SaveLevelsVerify, 5, s_saveLevels);

// 005C3530.
static void __fastcall RestoreLevelsVerify(LocalSearch* self, void*) {
    InPlace("LocalSearch::RestoreLevels", self, [&] { s_restoreLevels(self, nullptr); }, [&] { self->RestoreLevels(); },
            true);
}
EI_VERIFY(0x005C3530, RestoreLevelsVerify, 8, s_restoreLevels);

// 005C1EA0: allocates (ours only): every node unreached, unqueued, at its own window cell, the
// border unusable and the inside usable; 34 rows in the table; inactive.
static void __fastcall AttachVerify(LocalSearch* self, void*, CAIMap* map, bool backward) {
    self->Attach(map, backward);
    bool ok = self->map == map && self->backward == backward && !self->active && !self->settledList &&
              self->cellRowCount == 34 && self->nodes.width == 34 && self->nodes.height == 34;
    for (int y = 0; ok && y < 34; ++y) {
        for (int x = 0; ok && x < 34; ++x) {
            const SearchNode& n = self->nodes.rows[y][x];
            bool border = x == 0 || y == 0 || x == 33 || y == 33;
            ok = n.cost == 0x7FFFFFFF && !n.next && !n.prev && n.x == x && n.y == y && n.usable == !border &&
                 !n.settled && n.direction == 0;
        }
    }
    ei::verify::Check("LocalSearch::Attach", true, ok, "search %p", self);
}
EI_VERIFY(0x005C1EA0, AttachVerify, 0, s_unusedAttach);

// 005C25D0: both allocate and free a copy of the node grid; state and AL compared.
static bool __fastcall RecenterVerify(LocalSearch* self, void*, int x0, int y0, int x1, int y1, bool settleInside) {
    return InPlaceResult<bool>("LocalSearch::Recenter", self,
                               [&] { return s_recenter(self, nullptr, x0, y0, x1, y1, settleInside); },
                               [&] { return self->Recenter(x0, y0, x1, y1, settleInside); });
}
EI_VERIFY(0x005C25D0, RecenterVerify, 5, s_recenter);

// 005B9580: output (the original into a scratch context).
static void __fastcall CostContextForVerify(CAIMap* self, void*, CUnit* unit, PassCostContext* out) {
    PassCostContext expected = *out;
    s_costFor(self, nullptr, unit, &expected);
    self->CostContextFor(unit, out);
    ei::verify::Check("CAIMap::CostContextFor", true, std::memcmp(&expected, out, sizeof(expected)) == 0,
                      "unit class %d", unit->sizeClass);
}
EI_VERIFY(0x005B9580, CostContextForVerify, 6, s_costFor);
