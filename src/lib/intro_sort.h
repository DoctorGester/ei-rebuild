#pragma once

// The game's sort: game.exe's std::sort (introsort: median of three, an unguarded partition, heap sort past the
// depth limit, then an insertion sort that is guarded over the first 16 elements only). It is not stable, so where
// elements can compare equal and still differ (the draw lists, the particles, the picking hits, the server list,
// the save list, the objectives' quests, the briefing topics) the order it leaves them in is the game's, comparison
// for comparison; sorts whose equal elements are identical use std::sort. The scans the game leaves unguarded are
// unguarded here too: with a comparator that is not a strict weak ordering (the save list's !(a < b), the quests'
// "no record before all") they can run off the range, as in the game. libcrt::Qsort (crt.h) is the game CRT's
// quicksort, another algorithm; nothing is shared.

#include <utility>

namespace lib {

namespace intro_sort {

// The median of three (the first of equals by `less`'s tests).
template <typename T, typename Less>
const T& Median(const T& a, const T& b, const T& c, Less& less) {
    if (less(a, b)) {
        if (less(b, c)) return b;
        if (less(a, c)) return c;
        return a;
    }
    if (less(a, c)) return a;
    if (less(b, c)) return c;
    return b;
}

// Partitions around `pivot` (a copy); the scans stop at the pivot's equals, with no bounds checks.
template <typename T, typename Less>
T* UnguardedPartition(T* first, T* last, const T& pivot, Less& less) {
    for (;;) {
        while (less(*first, pivot)) ++first;
        --last;
        while (less(pivot, *last)) --last;
        if (!(first < last)) return first;
        std::swap(*first, *last);
        ++first;
    }
}

// Sifts the hole at `hole` down to a leaf, then pushes `value` up from there (not above `hole`).
template <typename T, typename Less>
void AdjustHeap(T* first, int hole, int length, T value, Less& less) {
    const int top = hole;
    int child = 2 * hole + 2;
    while (child < length) {
        if (less(first[child], first[child - 1])) --child;
        first[hole] = first[child];
        hole = child;
        child = 2 * (child + 1);
    }
    if (child == length) {
        first[hole] = first[child - 1];
        hole = child - 1;
    }
    int parent = (hole - 1) / 2;
    while (hole > top && less(first[parent], value)) {
        first[hole] = first[parent];
        hole = parent;
        parent = (hole - 1) / 2;
    }
    first[hole] = value;
}

// make_heap, then pop_heap to the end.
template <typename T, typename Less>
void HeapSort(T* first, T* last, Less& less) {
    const int length = static_cast<int>(last - first);
    if (length >= 2) {
        for (int parent = (length - 2) / 2;; --parent) {
            AdjustHeap(first, parent, length, T(first[parent]), less);
            if (parent == 0) break;
        }
    }
    for (; last - first > 1; --last) {
        T value = last[-1];
        last[-1] = *first;
        AdjustHeap(first, 0, static_cast<int>(last - 1 - first), value, less);
    }
}

// Moves `value` down from `last` to after the first element not greater: no lower bound check.
template <typename T, typename Less>
void UnguardedLinearInsert(T* last, T value, Less& less) {
    T* next = last - 1;
    while (less(value, *next)) {
        *last = *next;
        last = next;
        --next;
    }
    *last = value;
}

template <typename T, typename Less>
void InsertionSort(T* first, T* last, Less& less) {
    if (first == last) return;
    for (T* i = first + 1; i != last; ++i) {
        T value = *i;
        if (less(value, *first)) {
            for (T* p = i; p != first; --p) *p = p[-1];
            *first = value;
        } else {
            UnguardedLinearInsert(i, value, less);
        }
    }
}

constexpr int kThreshold = 16;

} // namespace intro_sort

// The introsort loop: partitions [first, last) until every part is at most 16 elements; a part still longer past
// `depthLimit` levels is heap sorted.
template <typename T, typename Less>
void IntroSortLoop(T* first, T* last, int depthLimit, Less less) {
    while (last - first > intro_sort::kThreshold) {
        if (depthLimit == 0) {
            intro_sort::HeapSort(first, last, less);
            return;
        }
        --depthLimit;
        T* cut;
        {
            const T pivot = intro_sort::Median(*first, first[(last - first) / 2], last[-1], less);
            cut = intro_sort::UnguardedPartition(first, last, pivot, less);
        }
        IntroSortLoop(cut, last, depthLimit, less);
        last = cut;
    }
}

// The final insertion sort: guarded over the first 16 elements, unguarded after them (the loop left every
// element at most 16 places from a smaller one before it).
template <typename T, typename Less>
void FinalInsertionSort(T* first, T* last, Less less) {
    if (last - first > intro_sort::kThreshold) {
        intro_sort::InsertionSort(first, first + intro_sort::kThreshold, less);
        for (T* i = first + intro_sort::kThreshold; i != last; ++i) intro_sort::UnguardedLinearInsert(i, T(*i), less);
    } else {
        intro_sort::InsertionSort(first, last, less);
    }
}

// Sorts [first, last) by `less` as the game's std::sort: the loop with twice floor(log2(n)) levels, then the final
// insertion sort.
template <typename T, typename Less>
void IntroSort(T* first, T* last, Less less) {
    if (first == last) return;
    int depth = 0;
    for (auto n = last - first; n != 1; n >>= 1) ++depth;
    IntroSortLoop(first, last, depth * 2, less);
    FinalInsertionSort(first, last, less);
}

} // namespace lib
