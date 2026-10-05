// The sort of the changed variables by their stamp (00589160, 00589240): STLport's introsort
// instantiated for node pointers with EntryIdLess.
#include "world.h"

#include "stl_sort.h"

#include "ei/hook.h"

#include <cstdint>

namespace {
struct ByStamp {
    bool operator()(void* a, void* b) const {
        EntryIdLess less;
        return less.Less(static_cast<const std::uint8_t*>(a), static_cast<const std::uint8_t*>(b)) != 0;
    }
};
using StampSort = StlSort<void*, ByStamp>;
} // namespace

void __fastcall StampIntroSort(void** first, void** last, int, int depthLimit, const void*) {
    StampSort::IntroLoop(first, last, depthLimit, ByStamp());
}
EI_REPLACE(0x00589160, StampIntroSort);

void __fastcall StampInsertionSort(void** first, void** last, const void*) {
    StampSort::FinalInsertionSort(first, last, ByStamp());
}
EI_REPLACE(0x00589240, StampInsertionSort);
