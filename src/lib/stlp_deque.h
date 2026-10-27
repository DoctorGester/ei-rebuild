#pragma once

// STLport's deque<T> as compiled into game.exe, for trivially destructible T only. Layout and
// instantiations: docs/notes/standalone.md, "The image's source files".

#include "stlp_alloc.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace stlp {

template <typename T>
class deque {
public:
    static constexpr std::size_t kPerNode = sizeof(T) < 512 ? 512 / sizeof(T) : 1;
    static constexpr std::size_t kNodeBytes = kPerNode * sizeof(T);
    static_assert(std::is_trivially_destructible_v<T>);

    struct Iterator {
        T* cur;
        T* first;
        T* last;
        T** node;
        void SetNode(T** n) {
            node = n;
            first = *n;
            last = first + kPerNode;
        }
    };

    bool empty() const { return finish_.cur == start_.cur; }
    std::size_t size() const {
        return static_cast<std::size_t>(finish_.node - start_.node - 1) * kPerNode +
               static_cast<std::size_t>(finish_.cur - finish_.first) + static_cast<std::size_t>(start_.last - start_.cur);
    }
    const Iterator& Start() const { return start_; }
    const Iterator& Finish() const { return finish_; }
    T** Map() const { return map_; }
    std::size_t MapSize() const { return mapSize_; }

    // deque(const allocator_type&, n): every word zeroed, then _M_initialize_map(n).
    void Construct(std::size_t n = 0) {
        start_ = Iterator{};
        finish_ = Iterator{};
        map_ = nullptr;
        mapSize_ = 0;
        InitializeMap(n);
    }

    // _M_initialize_map(n): a map of max(8, nodes + 2) slots from the pool,
    // the nodes for `n` elements in its middle (_M_create_nodes), the start at the first node's
    // beginning, the finish n % kPerNode into the last.
    void InitializeMap(std::size_t n) {
        const std::size_t nodes = n / kPerNode + 1;
        mapSize_ = nodes + 2 > 8 ? nodes + 2 : 8;
        map_ = allocate_as<T*>(mapSize_);
        T** nstart = map_ + (mapSize_ - nodes) / 2;
        T** nfinish = nstart + nodes;
        CreateNodes(nstart, nfinish);
        start_.SetNode(nstart);
        finish_.SetNode(nfinish - 1);
        start_.cur = start_.first;
        finish_.cur = finish_.first + n % kPerNode;
    }

    // _M_create_nodes(nstart, nfinish): a node for each map slot, in order
    // (the nodes are above 128 bytes: operator new).
    static void CreateNodes(T** nstart, T** nfinish) {
        for (T** p = nstart; p < nfinish; ++p) *p = static_cast<T*>(allocate(kNodeBytes));
    }

    // pop_back(): the finish steps back; leaving an emptied node frees it (_M_pop_back_aux).
    void pop_back() {
        if (finish_.cur != finish_.first) {
            --finish_.cur;
            return;
        }
        deallocate(finish_.first, kNodeBytes);
        finish_.SetNode(finish_.node - 1);
        finish_.cur = finish_.last - 1;
    }

    // pop_front(): the start steps on; leaving its node's last element frees the node (_M_pop_front_aux).
    void pop_front() {
        if (start_.cur != start_.last - 1) {
            ++start_.cur;
            return;
        }
        deallocate(start_.first, kNodeBytes);
        start_.SetNode(start_.node + 1);
        start_.cur = start_.first;
    }

    // back(): the element before the finish (in the node before it when the finish opens a node).
    T& back() {
        T* p = finish_.cur != finish_.first ? finish_.cur : *(finish_.node - 1) + kPerNode;
        return p[-1];
    }

    // push_back(x): constructed at the finish while the node has room for another after it; else
    // _M_push_back_aux.
    void push_back(const T& x) {
        if (finish_.cur != finish_.last - 1) {
            if (finish_.cur) *finish_.cur = x;
            ++finish_.cur;
            return;
        }
        PushBackAux(x);
    }

    // _M_push_back_aux(x): x copied first; the map makes room for one more node pointer (_M_reallocate_map(1,
    // false)) when fewer than two slots follow the finish's node, a new node follows, x goes into the last
    // slot of the finish's node and the finish opens the new one.
    void PushBackAux(const T& x) {
        T value = x;
        if (mapSize_ - static_cast<std::size_t>(finish_.node - map_) < 2) ReallocateMap(1, false);
        finish_.node[1] = static_cast<T*>(allocate(kNodeBytes));
        if (finish_.cur) *finish_.cur = value;
        finish_.SetNode(finish_.node + 1);
        finish_.cur = finish_.first;
    }

    // _M_reallocate_map(add, atFront): room for `add` more node pointers at
    // the back (or the front): the used pointers are recentred in a map more than twice as large as
    // needed, else moved to a new map of size + max(size, add) + 2 slots (the old one freed).
    void ReallocateMap(std::size_t add, bool atFront) {
        const std::size_t oldNodes = static_cast<std::size_t>(finish_.node - start_.node) + 1;
        const std::size_t newNodes = oldNodes + add;
        T** newStart;
        if (mapSize_ > 2 * newNodes) {
            newStart = map_ + (mapSize_ - newNodes) / 2 + (atFront ? add : 0);
            std::memmove(newStart, start_.node, oldNodes * sizeof(T*));
        } else {
            const std::size_t newSize = mapSize_ + (mapSize_ < add ? add : mapSize_) + 2;
            T** newMap = allocate_as<T*>(newSize);
            newStart = newMap + (newSize - newNodes) / 2 + (atFront ? add : 0);
            std::memcpy(newStart, start_.node, oldNodes * sizeof(T*));
            if (map_) deallocate(map_, mapSize_ * sizeof(T*));
            map_ = newMap;
            mapSize_ = newSize;
        }
        start_.SetNode(newStart);
        finish_.SetNode(newStart + oldNodes - 1);
    }

    // `while (!empty()) pop_back();`, as the game inlines it to empty a deque.
    void PopAll() {
        while (!empty()) pop_back();
    }

    // ~deque: the elements' destructors (nothing, _Destroy), then every node
    // from start to finish and the map back to the pool.
    void Destroy() {
        if (!map_) return;
        for (T** p = start_.node; p < finish_.node + 1; ++p) deallocate(*p, kNodeBytes);
        deallocate(map_, mapSize_ * sizeof(T*));
    }

private:
    Iterator start_;
    Iterator finish_;
    T** map_;
    std::size_t mapSize_;
};

// The game's instantiations are __thiscall members, declared here as __fastcall functions with the
// unused EDX second; the destroy helpers are __stdcall functions of two iterators by value.
template <typename T>
struct DequeAbi : deque<T> {
    using Deque = deque<T>;
    using Iterator = typename Deque::Iterator;

    // deque(const allocator_type&, n): returns this.
    Deque* Construct(const void* /*allocator*/, std::size_t n) {
        Deque::Construct(n);
        return this;
    }
    // _M_initialize_map(n), _M_push_back_aux(x) and _M_reallocate_map(add, atFront)
    // (the flag is the low byte of its slot) are deque's own members, registered through this type.
    // _M_create_nodes(nstart, nfinish): the deque itself is not read.
    void CreateNodes(T** nstart, T** nfinish) { Deque::CreateNodes(nstart, nfinish); }
    // ~deque().
    static void __fastcall Destruct(Deque* self) { self->Destroy(); }
};

} // namespace stlp
