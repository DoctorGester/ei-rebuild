#pragma once

// STLport's sort internals (introsort with a final insertion sort, heap sort at depth 0) as the
// exe instantiates them, one comparator per instantiation: the draw lists (world_client_view.cpp),
// the changed variables (world_stamp_sort.cpp) and the instantiations registered at their library
// addresses (game/stl_sorts.cpp). sort() is unstable, so the order of equal elements is part of the
// behaviour: every step follows STLport's (same comparisons in the same order, same moves). Every
// scan the original leaves unguarded is unguarded here too: with a comparator that is not a strict
// weak ordering they run off the array.

namespace stlp {

template <typename T, typename Less>
struct Sort {
    // __median: the value of the median of three (STLport returns a reference; the callers copy it).
    static const T& Median(const T& a, const T& b, const T& c, Less less) {
        if (less(a, b)) {
            if (less(b, c)) return b;
            if (less(a, c)) return c;
            return a;
        }
        if (less(a, c)) return a;
        if (less(b, c)) return c;
        return b;
    }

    static T* UnguardedPartition(T* first, T* last, T pivot, Less less) {
        for (;;) {
            while (less(*first, pivot)) ++first;
            --last;
            while (less(pivot, *last)) --last;
            if (!(first < last)) return first;
            T swap = *first;
            *first = *last;
            *last = swap;
            ++first;
        }
    }

    static void PushHeap(T* first, int hole, int top, T value, Less less) {
        int parent = (hole - 1) / 2;
        while (hole > top && less(first[parent], value)) {
            first[hole] = first[parent];
            hole = parent;
            parent = (hole - 1) / 2;
        }
        first[hole] = value;
    }

    static void AdjustHeap(T* first, int hole, int length, T value, Less less) {
        int top = hole;
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
        PushHeap(first, hole, top, value, less);
    }

    static void MakeHeap(T* first, T* last, Less less) {
        int length = static_cast<int>(last - first);
        if (length < 2) return;
        int parent = (length - 2) / 2;
        for (;;) {
            AdjustHeap(first, parent, length, first[parent], less);
            if (parent == 0) return;
            --parent;
        }
    }

    // pop_heap(first, last): the top goes to last - 1.
    static void PopHeap(T* first, T* last, Less less) {
        T value = last[-1];
        last[-1] = *first;
        AdjustHeap(first, 0, static_cast<int>(last - 1 - first), value, less);
    }

    // partial_sort(first, last, last): heap sort.
    static void HeapSort(T* first, T* last, Less less) {
        MakeHeap(first, last, less);
        while (last - first > 1) {
            PopHeap(first, last, less);
            --last;
        }
    }

    static void IntroLoop(T* first, T* last, int depthLimit, Less less) {
        while (last - first > 16) {
            if (depthLimit == 0) {
                HeapSort(first, last, less);
                return;
            }
            --depthLimit;
            T* cut = UnguardedPartition(first, last, T(Median(*first, first[(last - first) / 2], last[-1], less)), less);
            IntroLoop(cut, last, depthLimit, less);
            last = cut;
        }
    }

    static void UnguardedLinearInsert(T* last, T value, Less less) {
        T* next = last - 1;
        while (less(value, *next)) {
            *last = *next;
            last = next;
            --next;
        }
        *last = value;
    }

    static void InsertionSort(T* first, T* last, Less less) {
        if (first == last) return;
        for (T* i = first + 1; i != last; ++i) {
            T value = *i;
            if (less(value, *first)) {
                for (T* p = i; p != first; --p) *p = p[-1];   // copy_backward
                *first = value;
            } else {
                UnguardedLinearInsert(i, value, less);
            }
        }
    }

    // __unguarded_insertion_sort(first, last): every element has a smaller one before the range.
    static void UnguardedInsertionSort(T* first, T* last, Less less) {
        for (T* i = first; i != last; ++i) UnguardedLinearInsert(i, *i, less);
    }

    static void FinalInsertionSort(T* first, T* last, Less less) {
        if (last - first > 16) {
            InsertionSort(first, first + 16, less);
            UnguardedInsertionSort(first + 16, last, less);
        } else {
            InsertionSort(first, last, less);
        }
    }
};

} // namespace stlp
