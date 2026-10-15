#pragma once

// STLport's vector<T> as compiled into game.exe (stlp.h includes it; docs/notes/standalone.md,
// section 2).
//
// Three pointers: start, finish, end of storage (the allocator proxy's empty byte shares the last
// word). The block comes from the node allocator: its byte size, a multiple of sizeof(T), picks the
// free list, so every member that frees it computes the size from the element size exactly as the
// instantiation does. The game's instantiations of the members below are registered at their own
// addresses in stlp_vector.cpp through the VectorAbi shims.

#include "crt.h"
#include "stlp_alloc.h"

#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>

namespace stlp {

// vector<T>: grows to size + max(size, 1) elements on overflow, copying elements into the new block
// (_M_insert_overflow). T must provide copy construction and AssignFrom.
template <typename T>
class vector {
public:
    using value_type = T;

    vector() = default;
    vector(const vector&) = delete;

    T* begin() const { return start_; }
    T* end() const { return finish_; }
    std::size_t size() const { return static_cast<std::size_t>(finish_ - start_); }
    T& back() const { return finish_[-1]; }
    T& operator[](std::size_t i) const { return start_[i]; }

    void push_back(const T& x) {
        if (finish_ != eos_) {
            new (finish_) T(x);
            ++finish_;
            return;
        }
        std::size_t old = size();
        std::size_t len = old + (old > 1 ? old : 1);
        T* fresh = static_cast<T*>(allocate(len * sizeof(T)));
        T* out = fresh;
        for (T* p = start_; p != finish_; ++p, ++out) new (out) T(*p);
        new (out) T(x);
        ++out;
        for (T* p = start_; p != finish_; ++p) p->~T();
        if (start_) deallocate(start_, static_cast<std::size_t>(eos_ - start_) * sizeof(T));
        start_ = fresh;
        finish_ = out;
        eos_ = fresh + len;
    }

    // pop_back(): destroy the last element (a trivial one: the finish pointer steps back).
    void pop_back() {
        --finish_;
        finish_->~T();
    }

    // clear(): destroy every element in order; keep the storage.
    void clear() {
        for (T* p = start_; p != finish_; ++p) p->~T();
        finish_ = start_;
    }

    // _Vector_base(const allocator_type&) (e.g. 00406240): no storage.
    void ConstructEmpty() {
        start_ = nullptr;
        finish_ = nullptr;
        eos_ = nullptr;
    }

    // ~_Vector_base() (e.g. 00405BF0): the storage back to the pool (the elements are already
    // destroyed); its size is the capacity in whole elements.
    void FreeStorage() {
        if (start_) deallocate(start_, static_cast<std::size_t>(eos_ - start_) * sizeof(T));
    }

    // get_allocator() (e.g. 00421EB0): the allocator's tag byte, the low byte of the last word.
    std::uint8_t AllocatorTag() const { return static_cast<std::uint8_t>(reinterpret_cast<std::uintptr_t>(eos_)); }

    // The three pointers by reference, for code that inlines the original's own vector operations
    // (reserve, push_back through the instantiation's _M_insert_overflow) on the same object.
    T*& Start() { return start_; }
    T*& Finish() { return finish_; }
    T*& EndOfStorage() { return eos_; }

    // erase(pos): shift the tail down by assignment (AssignFrom, or a plain copy for scalars such as
    // pointers), destroy the last element.
    void erase(T* pos) {
        for (T* p = pos; p + 1 != finish_; ++p) {
            if constexpr (std::is_class_v<T>) p->AssignFrom(p[1]);
            else *p = p[1];
        }
        --finish_;
        finish_->~T();
    }

    // STLport's members as the game's instantiations compile them, each element copy, assignment
    // and destruction done by the element policy E (PlainElement for plain data). Blocks are taken
    // and given back in the original's order (the new block before the old one is freed), so the
    // pool hands out the same blocks.

    std::size_t Capacity() const { return static_cast<std::size_t>(eos_ - start_); }

    // _Vector_base(n, allocator) (e.g. 00421F60): room for n elements, none constructed.
    void ConstructWithCapacity(std::size_t n) {
        start_ = Allocate(n);
        finish_ = start_;
        eos_ = start_ + n;
    }

    // vector(const vector& x) (e.g. 004FDDB0): room for exactly x.size() elements (none when
    // empty) holding copies of x's.
    template <typename E>
    void CopyConstruct(const vector& x) {
        ConstructWithCapacity(x.size());
        finish_ = UninitializedCopy<E>(x.start_, x.finish_, start_);
    }

    // _M_allocate_and_copy(n, first, last) (e.g. 0069DB00): a block of n elements holding copies
    // of [first, last).
    template <typename E>
    static T* AllocateAndCopy(std::size_t n, const T* first, const T* last) {
        T* block = Allocate(n);
        UninitializedCopy<E>(first, last, block);
        return block;
    }

    // _M_insert_overflow(pos, x, n) (e.g. 0043B6C0): a block of size + max(size, n) elements with
    // [start, pos), n copies of x and [pos, finish); the old elements destroyed, the old block freed.
    template <typename E>
    void InsertOverflow(T* pos, const T& x, std::size_t n) {
        std::size_t old = size();
        std::size_t len = old + (old > n ? old : n);
        T* fresh = Allocate(len);
        T* out = UninitializedCopy<E>(start_, pos, fresh);
        out = UninitializedFill<E>(out, n, x);
        out = UninitializedCopy<E>(pos, finish_, out);
        Release<E>();
        start_ = fresh;
        finish_ = out;
        eos_ = fresh + len;
    }

    // _M_fill_insert(pos, n, x) (e.g. 0040B150): n copies of x before pos, in place when the room
    // allows (x copied first by the policy, as it may be an element, and destroyed last), else
    // through InsertOverflow.
    template <typename E>
    void FillInsert(T* pos, std::size_t n, const T& x) {
        if (n == 0) return;
        if (static_cast<std::size_t>(eos_ - finish_) < n) {
            InsertOverflow<E>(pos, x, n);
            return;
        }
        alignas(T) unsigned char storage[sizeof(T)];
        T& copy = *reinterpret_cast<T*>(storage);
        E::Construct(&copy, x);
        std::size_t after = static_cast<std::size_t>(finish_ - pos);
        T* oldFinish = finish_;
        if (after > n) {
            UninitializedCopy<E>(finish_ - n, finish_, finish_);
            finish_ += n;
            CopyBackward<E>(pos, oldFinish - n, oldFinish);
            Fill<E>(pos, pos + n, copy);
        } else {
            UninitializedFill<E>(finish_, n - after, copy);
            finish_ += n - after;
            UninitializedCopy<E>(pos, oldFinish, finish_);
            finish_ += after;
            Fill<E>(pos, oldFinish, copy);
        }
        E::Destroy(&copy);
    }

    // _M_range_insert(pos, first, last) (e.g. 0069ECD0): copies of [first, last) before pos.
    template <typename E>
    void RangeInsert(T* pos, const T* first, const T* last) {
        if (first == last) return;
        std::size_t n = static_cast<std::size_t>(last - first);
        if (static_cast<std::size_t>(eos_ - finish_) >= n) {
            std::size_t after = static_cast<std::size_t>(finish_ - pos);
            T* oldFinish = finish_;
            if (after > n) {
                UninitializedCopy<E>(finish_ - n, finish_, finish_);
                finish_ += n;
                CopyBackward<E>(pos, oldFinish - n, oldFinish);
                Copy<E>(first, last, pos);
            } else {
                const T* mid = first + after;
                UninitializedCopy<E>(mid, last, finish_);
                finish_ += n - after;
                UninitializedCopy<E>(pos, oldFinish, finish_);
                finish_ += after;
                Copy<E>(first, mid, pos);
            }
            return;
        }
        std::size_t old = size();
        std::size_t len = old + (old > n ? old : n);
        T* fresh = Allocate(len);
        T* out = UninitializedCopy<E>(start_, pos, fresh);
        out = UninitializedCopy<E>(first, last, out);
        out = UninitializedCopy<E>(pos, finish_, out);
        Release<E>();
        start_ = fresh;
        finish_ = out;
        eos_ = fresh + len;
    }

    // erase(first, last) (e.g. 0056F5E0): the tail assigned down, the left-over elements destroyed;
    // returns first.
    template <typename E>
    T* EraseRange(T* first, T* last) {
        T* end = Copy<E>(last, finish_, first);
        for (T* p = end; p != finish_; ++p) E::Destroy(p);
        finish_ = end;
        return first;
    }

    // resize(n, x) (e.g. 004AE3A0): erase the tail, or append copies of x.
    template <typename E>
    void Resize(std::size_t n, const T& x) {
        if (n < size()) EraseRange<E>(start_ + n, finish_);
        else FillInsert<E>(finish_, n - size(), x);
    }

    // reserve(n) (e.g. 0040AF00): a block of exactly n elements when the capacity is smaller.
    template <typename E>
    void Reserve(std::size_t n) {
        if (Capacity() >= n) return;
        std::size_t old = size();
        T* fresh;
        if (start_) {
            fresh = AllocateAndCopy<E>(n, start_, finish_);
            Release<E>();
        } else {
            fresh = Allocate(n);
        }
        start_ = fresh;
        finish_ = fresh + old;
        eos_ = fresh + n;
    }

    // push_back(x) (e.g. 0069BD30).
    template <typename E>
    void PushBack(const T& x) {
        if (finish_ != eos_) {
            E::Construct(finish_, x);
            ++finish_;
        } else {
            InsertOverflow<E>(finish_, x, 1);
        }
    }

    // push_back(T()) (e.g. 0064AB20): the default element made in place when there is room, else a
    // temporary the policy makes, inserted through InsertOverflow and destroyed.
    template <typename E>
    void PushDefault() {
        if (finish_ != eos_) {
            E::Default(finish_);
            ++finish_;
            return;
        }
        alignas(T) unsigned char storage[sizeof(T)];
        T* x = reinterpret_cast<T*>(storage);
        E::Default(x);
        InsertOverflow<E>(finish_, *x, 1);
        E::Destroy(x);
    }

    // operator=(x) (e.g. 0064AF30).
    template <typename E>
    void AssignVector(const vector& x) {
        if (&x == this) return;
        std::size_t n = x.size();
        if (n > Capacity()) {
            T* fresh = AllocateAndCopy<E>(n, x.start_, x.finish_);
            Release<E>();
            start_ = fresh;
            eos_ = fresh + n;
        } else if (size() >= n) {
            T* end = Copy<E>(x.start_, x.finish_, start_);
            for (T* p = end; p != finish_; ++p) E::Destroy(p);
        } else {
            Copy<E>(x.start_, x.start_ + size(), start_);
            UninitializedCopy<E>(x.start_ + size(), x.finish_, finish_);
        }
        finish_ = start_ + n;
    }

private:
    static T* Allocate(std::size_t n) { return static_cast<T*>(allocate(n * sizeof(T))); }
    template <typename E>
    void Release() {
        for (T* p = start_; p != finish_; ++p) E::Destroy(p);
        FreeStorage();
    }
    template <typename E>
    static T* UninitializedCopy(const T* first, const T* last, T* out) {
        for (; first != last; ++first, ++out) E::Construct(out, *first);
        return out;
    }
    template <typename E>
    static T* UninitializedFill(T* out, std::size_t n, const T& x) {
        for (; n > 0; --n, ++out) E::Construct(out, x);
        return out;
    }
    template <typename E>
    static T* Copy(const T* first, const T* last, T* out) {
        for (; first != last; ++first, ++out) E::Assign(*out, *first);
        return out;
    }
    template <typename E>
    static void CopyBackward(const T* first, const T* last, T* outLast) {
        while (first != last) E::Assign(*--outLast, *--last);
    }
    template <typename E>
    static void Fill(T* first, T* last, const T& x) {
        for (; first != last; ++first) E::Assign(*first, x);
    }

    T* start_ = nullptr;
    T* finish_ = nullptr;
    T* eos_ = nullptr;
};
static_assert(sizeof(vector<int>) == 12);

// The element policy of plain data: bytes copied, nothing destroyed.
template <typename T>
struct PlainElement {
    static_assert(std::is_trivially_copyable_v<T>);
    static void Construct(T* to, const T& from) { new (to) T(from); }
    static void Assign(T& to, const T& from) { to = from; }
    static void Destroy(T*) {}
    static void Default(T* p) { new (p) T(); }
};

// The element policy of a class with its own copy constructor, assignment and destructor (CString).
template <typename T>
struct ObjectElement {
    static void Construct(T* to, const T& from) { new (to) T(from); }
    static void Assign(T& to, const T& from) { to = from; }
    static void Destroy(T* p) { p->~T(); }
    static void Default(T* p) { new (p) T(); }
};

// _STLP_alloc_proxy<T*, ...>: the pointer it holds (the end of storage, its allocator an empty byte
// in the same word).
struct AllocProxy {
    void* data;
};

// The game's instantiations are __thiscall members, declared here as __fastcall functions with the
// unused EDX second.
template <typename T, typename E = PlainElement<T>>
struct VectorAbi {
    using Vector = vector<T>;
    using Element = E;

    // ~_Vector_base() (e.g. 00405BF0).
    static void __fastcall DestroyBase(Vector* self) { self->FreeStorage(); }
    // The scalar deleting destructor of a _Vector_base (e.g. 0053D970; RET 4): returns this.
    static Vector* __fastcall DeleteBase(Vector* self, void*, unsigned flags) {
        self->FreeStorage();
        if (flags & 1) gamecrt::operator_delete(self);
        return self;
    }
    // _Vector_base(n, allocator) (e.g. 00421F60; RET 8).
    static void __fastcall ConstructWithCapacity(Vector* self, void*, std::size_t n, const void*) {
        self->ConstructWithCapacity(n);
    }
    // _M_allocate_and_copy(n, first, last) (e.g. 0069DB00; RET 0xC): the new block.
    static T* __fastcall AllocateAndCopy(Vector*, void*, std::size_t n, const T* first, const T* last) {
        return Vector::template AllocateAndCopy<E>(n, first, last);
    }
    // _M_insert_overflow(pos, x, n) (e.g. 0043B6C0; RET 0xC).
    static void __fastcall InsertOverflow(Vector* self, void*, T* pos, const T* x, std::size_t n) {
        self->template InsertOverflow<E>(pos, *x, n);
    }
    // _M_fill_insert(pos, n, x) / insert(pos, n, x) (e.g. 0040B150; RET 0xC).
    static void __fastcall FillInsert(Vector* self, void*, T* pos, std::size_t n, const T* x) {
        self->template FillInsert<E>(pos, n, *x);
    }
    // _M_range_insert(pos, first, last, tag) and insert(pos, first, last) (e.g. 0069ECD0,
    // 0069E3F0; RET 0x10: the last argument is not read).
    static void __fastcall RangeInsert(Vector* self, void*, T* pos, const T* first, const T* last, const void*) {
        self->template RangeInsert<E>(pos, first, last);
    }
    // erase(first, last) (e.g. 0056F5E0; RET 8): returns first.
    static T* __fastcall EraseRange(Vector* self, void*, T* first, T* last) {
        return self->template EraseRange<E>(first, last);
    }
    // clear() (e.g. 0056E870).
    static void __fastcall Clear(Vector* self) { self->template EraseRange<E>(self->begin(), self->end()); }
    // resize(n, x) (e.g. 004AE3A0; RET 8).
    static void __fastcall Resize(Vector* self, void*, std::size_t n, const T* x) { self->template Resize<E>(n, *x); }
    // resize(n) (e.g. 0051F5B0; RET 4): resize(n, T()) with a temporary the policy makes first and
    // destroys last.
    static void __fastcall ResizeDefault(Vector* self, void*, std::size_t n) {
        alignas(T) unsigned char storage[sizeof(T)];
        T* x = reinterpret_cast<T*>(storage);
        E::Default(x);
        self->template Resize<E>(n, *x);
        E::Destroy(x);
    }
    // ~vector() (e.g. 0053F020): the elements destroyed in order, then the storage freed.
    static void __fastcall Destruct(Vector* self) {
        self->template EraseRange<E>(self->begin(), self->end());
        self->FreeStorage();
    }
    // reserve(n) (e.g. 0040AF00; RET 4).
    static void __fastcall Reserve(Vector* self, void*, std::size_t n) { self->template Reserve<E>(n); }
    // push_back(x) (e.g. 0069BD30; RET 4).
    static void __fastcall PushBack(Vector* self, void*, const T* x) { self->template PushBack<E>(*x); }
    // push_back(T()) out of line (e.g. 0064AB20; plain RET).
    static void __fastcall PushDefault(Vector* self) { self->template PushDefault<E>(); }
    // operator=(x) (e.g. 0064AF30; RET 4): returns this.
    static Vector* __fastcall Assign(Vector* self, void*, const Vector* x) {
        self->template AssignVector<E>(*x);
        return self;
    }
    // _STLP_alloc_proxy::deallocate(p, n) (e.g. 00404D10; RET 8): the proxy is not read. Also the
    // list nodes' and hash buckets' allocators.
    static void __fastcall Deallocate(const AllocProxy*, void*, T* p, std::size_t n) { deallocate(p, n * sizeof(T)); }
    // The same compiled as a function of p and n (e.g. 00426860: ECX p, EDX n, plain RET).
    static void __fastcall DeallocateFast(T* p, std::size_t n) { deallocate(p, n * sizeof(T)); }
};

// The members that never touch an element, one body for all instantiations (stlp_vector.cpp).
using AnyVector = vector<Opaque<1>>;
// _Vector_base(const allocator_type&) (e.g. 00406240; RET 4): returns this.
AnyVector* __fastcall VectorConstructBase(AnyVector* self, void*, const void* allocator);
// _STLP_alloc_proxy(const allocator_type&, const pointer&) (e.g. 00406260; RET 8): returns this.
AllocProxy* __fastcall AllocProxyConstruct(AllocProxy* self, void*, const void* allocator, void* const* data);
// get_allocator() (e.g. 00421EB0; RET 4): the tag byte into *out; returns out.
std::uint8_t* __fastcall VectorGetAllocator(const AnyVector* self, void*, std::uint8_t* out);

} // namespace stlp
