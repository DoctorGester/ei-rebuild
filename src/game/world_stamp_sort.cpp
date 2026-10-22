// The sort of the changed variables by their stamp (00589160, 00589240): STLport's introsort
// instantiated for node pointers with EntryIdLess.
#include "world.h"

#include "lib/stlp_algo.h"

#include "ei/hook.h"

#include <cstdint>

namespace {
struct ByStamp {
    bool operator()(void* a, void* b) const {
        EntryIdLess less;
        using Node = stlp::list<RefPtr>::Node;
        return less.Less(static_cast<const Node*>(a), static_cast<const Node*>(b)) != 0;
    }
};
using StampSort = stlp::Sort<void*, ByStamp>;
} // namespace

void __fastcall StampIntroSort(void** first, void** last, int, int depthLimit, const void*) {
    StampSort::IntroLoop(first, last, depthLimit, ByStamp());
}
EI_REWRITTEN(0x00589160, StampIntroSort);

void __fastcall StampInsertionSort(void** first, void** last, const void*) {
    StampSort::FinalInsertionSort(first, last, ByStamp());
}
EI_REWRITTEN(0x00589240, StampInsertionSort);
