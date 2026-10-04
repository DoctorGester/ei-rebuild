// CAIMap::JoinLocalSearches (005B9690, RET 8, AL): the bidirectional local search's meeting. The
// forward (+0x2594) and backward (+0x2614) searches expand in turn; after each round the best open
// cell of one side is looked up in the other, first forward then backward, until a cell reached by
// both is found (or a side has nothing left: false). Then the forward search's path to the cell
// goes into `path`, the backward one's into a scratch list, which is reversed and appended
// (005C5E60 / 005C5DA0) and the result fixed up (005C6000); `cost` (optional) gets the summed
// cost. Both searches are reset either way. See ai_map.h and docs/notes/ai.md.
#include "ai_map.h"

#include "ei/hook.h"
#include "lib/mfc.h"

#include <cstdint>

namespace {

// The local search (thiscall): expand one round, the best open cell (x, y, cost; RET 0xC), the
// cost of a cell (RET 8; 0x7FFFFFFF unreached), the path to a cell into a list (RET 0xC), reset.
__declspec(naked) void __fastcall SearchExpand(void*, void*) EI_ORIGINAL(0x005C2D40)
__declspec(naked) void __fastcall SearchBest(void*, void*, int*, int*, int*) EI_ORIGINAL(0x005C3050)
__declspec(naked) int __fastcall SearchCostAt(void*, void*, int, int) EI_ORIGINAL(0x005C3090)
__declspec(naked) void __fastcall SearchPathTo(void*, void*, int, int, PathCellList*) EI_ORIGINAL(0x005C3200)
__declspec(naked) void __fastcall SearchResetAt(void*, void*) EI_ORIGINAL(0x005C35F0)
// The path cell list: a reversed copy into `out` (RET 4, returns out), append a list (RET 4),
// and the fix-up of the joined path.
__declspec(naked) PathCellList* __fastcall ListReversed(PathCellList*, void*, PathCellList*) EI_ORIGINAL(0x005C5E60)
__declspec(naked) void __fastcall ListAppend(PathCellList*, void*, PathCellList*) EI_ORIGINAL(0x005C5DA0)
__declspec(naked) void __fastcall ListFixUp(PathCellList*, void*) EI_ORIGINAL(0x005C6000)

constexpr int kUnreached = 0x7FFFFFFF;
constexpr std::uintptr_t kListVtable = 0x0073EDB0;

// The inlined list destructor (the nodes need no destruction).
void DestroyList(PathCellList& list) {
    list.vtable = reinterpret_cast<void*>(kListVtable);
    list.count = 0;
    list.free = nullptr;
    list.tail = nullptr;
    list.head = nullptr;
    mfc::PlexFreeDataChain(static_cast<mfc::CPlex*>(list.blocks));
}

} // namespace

bool CAIMap::JoinLocalSearches(PathCellList* path, int* cost) {
    auto* self = reinterpret_cast<std::uint8_t*>(this);
    void* forward = self + 0x2594;
    void* backward = self + 0x2614;
    bool ok = true;
    int x = 0, y = 0, open = 0, other = 0;
    for (;;) {
        SearchExpand(forward, nullptr);
        SearchExpand(backward, nullptr);
        SearchBest(forward, nullptr, &x, &y, &open);
        if (open == kUnreached) {
            ok = false;
            break;
        }
        other = SearchCostAt(backward, nullptr, x, y);
        if (other < kUnreached) break;
        SearchBest(backward, nullptr, &x, &y, &open);
        if (open == kUnreached) {
            ok = false;
            break;
        }
        other = SearchCostAt(forward, nullptr, x, y);
        if (other < kUnreached) break;
    }
    PathCellList scratch{reinterpret_cast<void*>(kListVtable), nullptr, nullptr, 0, nullptr, nullptr, 0x80};
    if (ok) {
        if (cost) *cost = other + open;
        SearchPathTo(forward, nullptr, x, y, path);
        SearchPathTo(backward, nullptr, x, y, &scratch);
        PathCellList reversed;
        ListAppend(path, nullptr, ListReversed(&scratch, nullptr, &reversed));
        DestroyList(reversed);
        ListFixUp(path, nullptr);
    }
    SearchResetAt(backward, nullptr);
    SearchResetAt(forward, nullptr);
    DestroyList(scratch);
    return ok;
}

EI_REPLACE_METHOD(0x005B9690, CAIMap::JoinLocalSearches);
