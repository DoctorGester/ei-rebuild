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

struct SearchState {
    unsigned char header[sizeof(LocalSearch)];
    std::vector<SearchNode> nodes;
};

void Capture(const LocalSearch* search, SearchState& state) {
    std::memcpy(state.header, search, sizeof(LocalSearch));
    int count = search->nodes.data ? search->nodes.width * search->nodes.height : 0;
    state.nodes.assign(search->nodes.data, search->nodes.data + count);
}

void Restore(LocalSearch* search, const SearchState& state) {
    std::memcpy(static_cast<void*>(search), state.header, sizeof(LocalSearch));
    if (!state.nodes.empty()) std::memcpy(search->nodes.data, state.nodes.data(), state.nodes.size() * sizeof(SearchNode));
}

bool Same(const SearchState& a, const SearchState& b) {
    return std::memcmp(a.header, b.header, sizeof(a.header)) == 0 && a.nodes.size() == b.nodes.size() &&
           std::memcmp(a.nodes.data(), b.nodes.data(), a.nodes.size() * sizeof(SearchNode)) == 0;
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
template <typename Original, typename Ours>
void InPlace(const char* name, LocalSearch* search, Original original, Ours ours) {
    SearchState before, expected, actual;
    Capture(search, before);
    original();
    Capture(search, expected);
    Restore(search, before);
    ours();
    Capture(search, actual);
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
