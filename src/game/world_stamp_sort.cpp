// The sort of the changed variables by their stamp: STLport's introsort
// instantiated for node pointers with EntryIdLess.
#include "world.h"

#include "lib/stlp_algo.h"

#include <cstdint>

namespace {
using Node = stlp::list<RefPtr>::Node;
struct ByStamp {
    bool operator()(Node* a, Node* b) const {
        EntryIdLess less;
        return less.Less(a, b) != 0;
    }
};
using StampSort = stlp::Sort<Node*, ByStamp>;
} // namespace

void StampIntroSort(Node** first, Node** last, int, int depthLimit, const void*) {
    StampSort::IntroLoop(first, last, depthLimit, ByStamp());
}

void StampInsertionSort(Node** first, Node** last, const void*) {
    StampSort::FinalInsertionSort(first, last, ByStamp());
}
