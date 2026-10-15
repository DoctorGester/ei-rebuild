#pragma once

// STLport's sort internals (introsort with a final insertion sort, heap sort at depth 0) as the
// exe instantiates them, one comparator per instantiation: the draw lists (world_client_view.cpp),
// the changed variables (world_stamp_sort.cpp) and the instantiations registered at their library
// addresses (game/stl_sorts.cpp). sort() is unstable, so the order of equal elements is part of the
// behaviour: every step follows STLport's (same comparisons in the same order, same moves). Every
// scan the original leaves unguarded is unguarded here too: with a comparator that is not a strict
// weak ordering they run off the array.
//
// Element copies follow STLport's too, for elements whose copies allocate (the save list's entries
// copy CStringArrays) or whose comparison does (the objectives' quest lookups): every by-value
// argument and local of STLport's helpers is a Held copy that lives exactly as long (a by-value
// argument is destroyed when its callee returns), every `*a = *b` an Ops::Assign.

#include <new>

namespace stlp {

// How a sort copies its elements: the element's own copy constructor, assignment and destructor
// (nothing at all for plain data). An element type whose copies are functions of the game's
// (SaveEntry) names them in an Ops of its own.
template <typename T>
struct ValueOps {
    static void Construct(T* at, const T& from) { new (at) T(from); }
    static void Assign(T& to, const T& from) { to = from; }
    static void Destroy(T* p) { p->~T(); }
};

// A copy of an element for as long as the STLport argument or local it stands for.
template <typename T, typename Ops>
class Held {
public:
    explicit Held(const T& from) { Ops::Construct(Get(), from); }
    ~Held() { Ops::Destroy(Get()); }
    Held(const Held&) = delete;
    Held& operator=(const Held&) = delete;
    T& operator*() { return *Get(); }

private:
    T* Get() { return reinterpret_cast<T*>(storage_); }
    alignas(T) unsigned char storage_[sizeof(T)];
};

// copy_backward(first, last, result) (e.g. 00657D50): [first, last) assigned to the elements before
// `result`, from the last; returns the first one written.
template <typename T, typename Ops = ValueOps<T>>
T* CopyBackward(T* first, T* last, T* result) {
    for (int n = static_cast<int>(last - first); n > 0; --n) Ops::Assign(*--result, *--last);
    return result;
}

template <typename T, typename Less, typename Ops = ValueOps<T>>
struct Sort {
    using Value = Held<T, Ops>;

    // __median: the median of three (STLport returns a reference; the callers copy it).
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

    // __unguarded_partition(first, last, pivot); iter_swap through a copy of *first.
    static T* UnguardedPartition(T* first, T* last, const T& pivot, Less less) {
        for (;;) {
            while (less(*first, pivot)) ++first;
            --last;
            while (less(pivot, *last)) --last;
            if (!(first < last)) return first;
            {
                Value swap(*first);
                Ops::Assign(*first, *last);
                Ops::Assign(*last, *swap);
            }
            ++first;
        }
    }

    static void PushHeap(T* first, int hole, int top, const T& value, Less less) {
        int parent = (hole - 1) / 2;
        while (hole > top && less(first[parent], value)) {
            Ops::Assign(first[hole], first[parent]);
            hole = parent;
            parent = (hole - 1) / 2;
        }
        Ops::Assign(first[hole], value);
    }

    // __adjust_heap(first, hole, length, value): __push_heap gets its own copy of the value.
    static void AdjustHeap(T* first, int hole, int length, const T& value, Less less) {
        int top = hole;
        int child = 2 * hole + 2;
        while (child < length) {
            if (less(first[child], first[child - 1])) --child;
            Ops::Assign(first[hole], first[child]);
            hole = child;
            child = 2 * (child + 1);
        }
        if (child == length) {
            Ops::Assign(first[hole], first[child - 1]);
            hole = child - 1;
        }
        Value copy(value);
        PushHeap(first, hole, top, *copy, less);
    }

    static void MakeHeap(T* first, T* last, Less less) {
        int length = static_cast<int>(last - first);
        if (length < 2) return;
        int parent = (length - 2) / 2;
        for (;;) {
            {
                Value value(first[parent]);
                AdjustHeap(first, parent, length, *value, less);
            }
            if (parent == 0) return;
            --parent;
        }
    }

    // pop_heap(first, last) (__pop_heap_aux, then __pop_heap): the top goes to last - 1.
    static void PopHeap(T* first, T* last, Less less) {
        Value value(last[-1]);
        Ops::Assign(last[-1], *first);
        Value copy(*value);
        AdjustHeap(first, 0, static_cast<int>(last - 1 - first), *copy, less);
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
            T* cut;
            {
                Value pivot(Median(*first, first[(last - first) / 2], last[-1], less));
                cut = UnguardedPartition(first, last, *pivot, less);
            }
            IntroLoop(cut, last, depthLimit, less);
            last = cut;
        }
    }

    static void UnguardedLinearInsert(T* last, const T& value, Less less) {
        T* next = last - 1;
        while (less(value, *next)) {
            Ops::Assign(*last, *next);
            last = next;
            --next;
        }
        Ops::Assign(*last, value);
    }

    // __linear_insert(first, last, value): __unguarded_linear_insert gets its own copy.
    static void LinearInsert(T* first, T* last, const T& value, Less less) {
        if (less(value, *first)) {
            CopyBackward<T, Ops>(first, last, last + 1);
            Ops::Assign(*first, value);
        } else {
            Value copy(value);
            UnguardedLinearInsert(last, *copy, less);
        }
    }

    static void InsertionSort(T* first, T* last, Less less) {
        if (first == last) return;
        for (T* i = first + 1; i != last; ++i) {
            Value value(*i);
            LinearInsert(first, i, *value, less);
        }
    }

    // __unguarded_insertion_sort(first, last): every element has a smaller one before the range.
    static void UnguardedInsertionSort(T* first, T* last, Less less) {
        for (T* i = first; i != last; ++i) {
            Value value(*i);
            UnguardedLinearInsert(i, *value, less);
        }
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
