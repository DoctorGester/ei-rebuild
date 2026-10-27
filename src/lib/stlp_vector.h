#pragma once

// STLport's vector<T> as compiled into game.exe (stlp.h includes it; docs/notes/standalone.md, section 2).
// Three pointers: start, finish, end of storage (the allocator proxy's empty byte shares the last word). The
// block comes from the node allocator: its byte size, a multiple of sizeof(T), picks the free list, so every
// member that frees it computes the size from the element size exactly as the instantiation does. The game's
// instantiations of the members below are registered at their own addresses in stlp_vector.cpp.

#include "crt.h"
#include "stlp_alloc.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
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
        T* fresh = allocate_as<T>(len);
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

    void pop_back() {
        --finish_;
        finish_->~T();
    }

    void clear() {
        for (T* p = start_; p != finish_; ++p) p->~T();
        finish_ = start_;
    }

    void ConstructEmpty() {
        start_ = nullptr;
        finish_ = nullptr;
        eos_ = nullptr;
    }

    // ~_Vector_base: the storage back to the pool (the elements are already
    // destroyed); its size is the capacity in whole elements.
    void FreeStorage() {
        if (start_) deallocate(start_, static_cast<std::size_t>(eos_ - start_) * sizeof(T));
    }

    std::uint8_t AllocatorTag() const { return static_cast<std::uint8_t>(reinterpret_cast<std::uintptr_t>(eos_)); }

    // The three pointers by reference, for code that inlines the vector operations
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

    // STLport's members as the game's instantiations compile them, each element copy, assignment and
    // destruction done by the element policy E (PlainElement for plain data). Blocks are taken and given back
    // in the same order (the new block before the old one is freed), so the pool hands out the same blocks.

    std::size_t Capacity() const { return static_cast<std::size_t>(eos_ - start_); }

    // _Vector_base(n, allocator): room for n elements, none constructed.
    void ConstructWithCapacity(std::size_t n) {
        start_ = Allocate(n);
        finish_ = start_;
        eos_ = start_ + n;
    }

    // vector(const vector& x): room for exactly x.size elements (none when empty) holding copies of x's.
    template <typename E>
    void CopyConstruct(const vector& x) {
        ConstructWithCapacity(x.size());
        finish_ = UninitializedCopy<E>(x.start_, x.finish_, start_);
    }

    // _M_allocate_and_copy(n, first, last): a block of n elements holding copies of [first, last).
    template <typename E>
    static T* AllocateAndCopy(std::size_t n, const T* first, const T* last) {
        T* block = Allocate(n);
        UninitializedCopy<E>(first, last, block);
        return block;
    }

    // _M_insert_overflow(pos, x, n): a block of size + max(size, n) elements with
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

    // _M_fill_insert(pos, n, x): n copies of x before pos, in place when the room allows (x copied first by
    // the policy, as it may be an element, and destroyed last), else through InsertOverflow.
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

    // _M_range_insert(pos, first, last): copies of [first, last) before pos.
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

    // erase(first, last): the tail assigned down, the left-over elements destroyed; returns first.
    template <typename E>
    T* EraseRange(T* first, T* last) {
        T* end = Copy<E>(last, finish_, first);
        for (T* p = end; p != finish_; ++p) E::Destroy(p);
        finish_ = end;
        return first;
    }

    // resize(n, x): erase the tail, or append copies of x.
    template <typename E>
    void Resize(std::size_t n, const T& x) {
        if (n < size()) EraseRange<E>(start_ + n, finish_);
        else FillInsert<E>(finish_, n - size(), x);
    }
    // resize(n): resize(n, T) with a temporary the policy makes first and destroys last.
    template <typename E>
    void ResizeDefault(std::size_t n) {
        alignas(T) unsigned char storage[sizeof(T)];
        T* x = reinterpret_cast<T*>(storage);
        E::Default(x);
        Resize<E>(n, *x);
        E::Destroy(x);
    }

    // reserve(n): a block of exactly n elements when the capacity is smaller.
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

    template <typename E>
    void PushBack(const T& x) {
        if (finish_ != eos_) {
            E::Construct(finish_, x);
            ++finish_;
        } else {
            InsertOverflow<E>(finish_, x, 1);
        }
    }

    // push_back(T): the default element made in place when there is room, else a
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
    static T* Allocate(std::size_t n) { return allocate_as<T>(n); }
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

// The game's instantiations are __thiscall members, declared here as __fastcall functions with an unused
// second argument.
template <typename T, typename E = PlainElement<T>>
struct VectorAbi : vector<T> {
    using Vector = vector<T>;
    using Element = E;

    static void __fastcall DestroyBase(Vector* self) { self->FreeStorage(); }
    // The scalar deleting destructor of a _Vector_base.
    Vector* DeleteBase(unsigned flags) {
        this->FreeStorage();
        if (flags & 1) gamecrt::operator_delete(this);
        return this;
    }
    void ConstructWithCapacity(std::size_t n, const void*) {
        Vector::ConstructWithCapacity(n);
    }
    // _M_allocate_and_copy(n, first, last): the new block.
    T* AllocateAndCopy(std::size_t n, const T* first, const T* last) {
        return Vector::template AllocateAndCopy<E>(n, first, last);
    }
    void InsertOverflow(T* pos, const T& x, std::size_t n) {
        Vector::template InsertOverflow<E>(pos, x, n);
    }
    // _M_fill_insert(pos, n, x) / insert(pos, n, x).
    void FillInsert(T* pos, std::size_t n, const T& x) {
        Vector::template FillInsert<E>(pos, n, x);
    }
    // _M_range_insert(pos, first, last, tag) and insert(pos, first, last) (the last argument is not read).
    void RangeInsert(T* pos, const T* first, const T* last, const void*) {
        Vector::template RangeInsert<E>(pos, first, last);
    }
    T* EraseRange(T* first, T* last) {
        return Vector::template EraseRange<E>(first, last);
    }
    static void __fastcall Clear(Vector* self) { self->template EraseRange<E>(self->begin(), self->end()); }
    void Resize(std::size_t n, const T& x) { Vector::template Resize<E>(n, x); }
    void ResizeDefault(std::size_t n) { Vector::template ResizeDefault<E>(n); }
    // ~vector: the elements destroyed in order, then the storage freed.
    static void __fastcall Destruct(Vector* self) {
        self->template EraseRange<E>(self->begin(), self->end());
        self->FreeStorage();
    }
    void Reserve(std::size_t n) { Vector::template Reserve<E>(n); }
    void PushBack(const T& x) { Vector::template PushBack<E>(x); }
    // push_back of a default-constructed element, out of line.
    static void __fastcall PushDefault(Vector* self) { self->template PushDefault<E>(); }
    Vector* Assign(const Vector& x) {
        Vector::template AssignVector<E>(x);
        return this;
    }
    // _STLP_alloc_proxy::deallocate(p, n): the proxy is not read. Also the
    // list nodes' and hash buckets' allocators.
    void Deallocate(T* p, std::size_t n) { deallocate(p, n * sizeof(T)); }
    // The same compiled as a function of p and n.
    static void __fastcall DeallocateFast(T* p, std::size_t n) { deallocate(p, n * sizeof(T)); }
    // uninitialized_copy(first, last, result) (the tag argument is not read): the end of the copies.
    static T* __fastcall UninitializedCopy(const T* first, const T* last, T* result, const void*) {
        for (; first != last; ++first, ++result) E::Construct(result, *first);
        return result;
    }
    // uninitialized_fill_n(first, n, x): the end of the copies.
    static T* __fastcall UninitializedFill(T* first, std::size_t n, const T* x, const void*) {
        for (; n > 0; --n, ++first) E::Construct(first, *x);
        return first;
    }
};

// Helpers of class instantiations compiled out of line, the element done by the policy E:
// - _Construct(p) : E::Default, nothing for a null p.
template <typename T, typename E>
void __fastcall ConstructDefaultAt(T* p) {
    if (p) E::Default(p);
}
// - _Construct(p, x) : E::Construct, nothing for a null p.
template <typename T, typename E>
void __fastcall ConstructCopyAt(T* p, const T* x) {
    if (p) E::Construct(p, *x);
}
// - __copy(first, last, out, random_access_iterator_tag, distance*) (the last two arguments
//   are not read): each element assigned by E::Assign;
//   nothing when last is not after first; returns the end of the copy.
template <typename T, typename E>
T* __fastcall CopyAssignTagged(const T* first, const T* last, T* out, const void*, const void*) {
    for (std::ptrdiff_t n = last - first; n > 0; --n, ++first, ++out) E::Assign(*out, *first);
    return out;
}

// Helpers of the plain-data instantiations compiled out of line (the first two
// arguments):
// - _Construct(p, x) : placement new, which skips a null p.
template <typename T>
void __fastcall ConstructAt(T* p, const T* x) {
    if (p) new (p) T(*x);
}
// - copy(first, last, out) as __copy_trivial: one memmove of the whole range;
//   returns the end of the copy.
template <typename T>
T* __fastcall CopyTrivial(const T* first, const T* last, T* out) {
    std::size_t bytes = static_cast<std::size_t>(last - first) * sizeof(T);
    std::memmove(out, first, bytes);
    return out + (last - first);
}
// - copy(first, last, out) as the element loop: nothing when last is not after
//   first; returns the end of the copy.
template <typename T>
T* __fastcall CopyElements(const T* first, const T* last, T* out) {
    for (std::ptrdiff_t n = last - first; n > 0; --n, ++first, ++out) *out = *first;
    return out;
}
// - the same with the element policy's assignment (counted_ptr<T>).
template <typename T, typename E>
T* __fastcall CopyAssigned(const T* first, const T* last, T* out) {
    for (std::ptrdiff_t n = last - first; n > 0; --n, ++first, ++out) E::Assign(*out, *first);
    return out;
}

} // namespace stlp
