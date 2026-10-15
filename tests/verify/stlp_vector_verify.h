#pragma once

// The model check of vector<T> instantiations whose elements have their own copy, assignment and
// destruction (verify_stlp_vector_objects.cpp, verify_record_vectors.cpp). Ours only: the copies
// allocate and free buffers the original's run could not hand back in order. The elements are
// described as text before the call (D::Of), the same change is made to that model, and afterwards
// the vector must hold the model's texts with its pointers ordered; a freed block must be on top of
// its free list.

#include "verify.h"

#include "stlp_list_verify.h"

#include "../../src/lib/stlp_vector.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace stlp_verify {

template <std::uintptr_t A, typename Abi, typename D>
struct ModelCheck {
    using Vector = typename Abi::Vector;
    using T = typename Vector::value_type;
    using Model = std::vector<std::string>;

    static Model Read(const Vector& v) {
        Model m;
        for (const T* p = v.begin(); p != v.end(); ++p) m.push_back(D::Of(*p));
        return m;
    }
    // The text of the element resize(n) appends (made and destroyed by the policy).
    static std::string DefaultText() {
        alignas(T) unsigned char storage[sizeof(T)];
        T* x = reinterpret_cast<T*>(storage);
        Abi::Element::Default(x);
        std::string text = D::Of(*x);
        Abi::Element::Destroy(x);
        return text;
    }
    static void Compare(const char* kind, Vector* self, const Model& expected, bool extra = true) {
        bool ok = extra && self->Start() <= self->Finish() && self->Finish() <= self->EndOfStorage() &&
                  Read(*self) == expected;
        char detail[96] = "";
        if (!ok)
            std::snprintf(detail, sizeof(detail), "%u elements (expected %u), capacity %u",
                          static_cast<unsigned>(self->size()), static_cast<unsigned>(expected.size()),
                          static_cast<unsigned>(self->Capacity()));
        ei::verify::Report(Name<A>(kind), ok, detail);
    }

    static void __fastcall InsertOverflow(Vector* self, void*, T* pos, const T* x, std::size_t n) {
        Model m = Read(*self);
        m.insert(m.begin() + (pos - self->begin()), n, D::Of(*x));
        Abi::InsertOverflow(self, nullptr, pos, x, n);
        Compare("stlp::vector::_M_insert_overflow", self, m);
    }
    static void __fastcall FillInsert(Vector* self, void*, T* pos, std::size_t n, const T* x) {
        Model m = Read(*self);
        m.insert(m.begin() + (pos - self->begin()), n, D::Of(*x));
        Abi::FillInsert(self, nullptr, pos, n, x);
        Compare("stlp::vector::_M_fill_insert", self, m);
    }
    static void __fastcall RangeInsert(Vector* self, void*, T* pos, const T* first, const T* last, const void* tag) {
        Model m = Read(*self);
        Model inserted;
        for (const T* p = first; p != last; ++p) inserted.push_back(D::Of(*p));
        m.insert(m.begin() + (pos - self->begin()), inserted.begin(), inserted.end());
        Abi::RangeInsert(self, nullptr, pos, first, last, tag);
        Compare("stlp::vector::_M_range_insert", self, m);
    }
    static T* __fastcall EraseRange(Vector* self, void*, T* first, T* last) {
        Model m = Read(*self);
        m.erase(m.begin() + (first - self->begin()), m.begin() + (last - self->begin()));
        T* result = Abi::EraseRange(self, nullptr, first, last);
        Compare("stlp::vector::erase", self, m, result == first);
        return result;
    }
    static void __fastcall Clear(Vector* self) {
        Abi::Clear(self);
        Compare("stlp::vector::clear", self, Model());
    }
    static void __fastcall Resize(Vector* self, void*, std::size_t n, const T* x) {
        Model m = Read(*self);
        m.resize(n, D::Of(*x));
        Abi::Resize(self, nullptr, n, x);
        Compare("stlp::vector::resize", self, m);
    }
    static void __fastcall ResizeDefault(Vector* self, void*, std::size_t n) {
        Model m = Read(*self);
        if (n > m.size()) m.resize(n, DefaultText());
        else m.resize(n);
        Abi::ResizeDefault(self, nullptr, n);
        Compare("stlp::vector::resize", self, m);
    }
    static void __fastcall Reserve(Vector* self, void*, std::size_t n) {
        Model m = Read(*self);
        Abi::Reserve(self, nullptr, n);
        Compare("stlp::vector::reserve", self, m, self->Capacity() >= n);
    }
    static void __fastcall PushBack(Vector* self, void*, const T* x) {
        Model m = Read(*self);
        m.push_back(D::Of(*x));
        Abi::PushBack(self, nullptr, x);
        Compare("stlp::vector::push_back", self, m);
    }
    static void __fastcall PushDefault(Vector* self) {
        Model m = Read(*self);
        m.push_back(DefaultText());
        Abi::PushDefault(self);
        Compare("stlp::vector::push_back(T())", self, m);
    }
    static Vector* __fastcall Assign(Vector* self, void*, const Vector* x) {
        Model m = Read(*x);
        Vector* result = Abi::Assign(self, nullptr, x);
        Compare("stlp::vector::operator=", self, m, result == self);
        return result;
    }
    static void __fastcall Destruct(Vector* self) {
        T* start = self->Start();
        std::size_t bytes = self->Capacity() * sizeof(T);
        void** list = start ? FreeList(bytes) : nullptr;
        Abi::Destruct(self);
        bool ok = !list || *list == start;
        ei::verify::Report(Name<A>("stlp::vector::~vector"), ok, ok ? "" : "the storage is not on top of its free list");
    }
};

} // namespace stlp_verify
