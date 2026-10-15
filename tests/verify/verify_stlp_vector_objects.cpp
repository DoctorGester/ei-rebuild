// Checks for the vector<CString> and vector<stlp::string> instantiations (src/lib/stlp_vector.cpp).
//
// vector<RefPtr> (counted references): the pointers as a model, and an insert must add exactly
// one reference per new copy (the moved elements' copies and releases cancel out).
//
// Ours only (the copies allocate and free string buffers the original's run could not hand back in
// order): the model check of stlp_vector_verify.h over the elements' texts.
#include "verify.h"

#include "stlp_list_verify.h"
#include "stlp_vector_verify.h"

#include "../../src/lib/mfc.h"
#include "../../src/lib/stlp.h"
#include "../../src/lib/stlp_vector_refs.h"

#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace {

void* s_unused;

using stlp_verify::FreeList;
using stlp_verify::Name;

using CStringVector = stlp::VectorAbi<mfc::CString, stlp::ObjectElement<mfc::CString>>;
using StringVector = stlp::VectorAbi<stlp::string, stlp::StringElement>;

struct CStringText {
    static std::string Of(const mfc::CString& s) { return static_cast<const char*>(s); }
};
struct StringText {
    static std::string Of(const stlp::string& s) { return std::string(s.data(), s.size()); }
};

using CountedRefVector = stlp::VectorAbi<RefPtr, stlp::CountedRefElement>;

template <std::uintptr_t A>
struct RefCheck {
    using Vector = CountedRefVector::Vector;
    using Model = std::vector<CRefObject*>;

    static Model Read(const Vector& v) {
        Model m;
        for (const RefPtr* p = v.begin(); p != v.end(); ++p) m.push_back(p->ptr);
        return m;
    }
    static void __fastcall InsertOverflow(Vector* self, void*, RefPtr* pos, const RefPtr* x, std::size_t n) {
        Model m = Read(*self);
        CRefObject* inserted = x->ptr;
        std::map<CRefObject*, int> refs;
        for (CRefObject* o : m)
            if (o) refs[o] = o->refs_;
        if (inserted) refs[inserted] = inserted->refs_ + static_cast<int>(n);
        m.insert(m.begin() + (pos - self->begin()), n, inserted);
        CountedRefVector::InsertOverflow(self, nullptr, pos, x, n);
        bool ok = Read(*self) == m && self->Capacity() >= m.size();
        for (const auto& entry : refs) ok = ok && entry.first->refs_ == entry.second;
        char detail[64] = "";
        if (!ok)
            std::snprintf(detail, sizeof(detail), "%u elements, inserted %p x%u", static_cast<unsigned>(self->size()),
                          inserted, static_cast<unsigned>(n));
        ei::verify::Report(Name<A>("stlp::vector::_M_insert_overflow"), ok, detail);
    }
    static RefPtr* __fastcall EraseRange(Vector* self, void*, RefPtr* first, RefPtr* last) {
        Model m = Read(*self);
        m.erase(m.begin() + (first - self->begin()), m.begin() + (last - self->begin()));
        RefPtr* result = CountedRefVector::EraseRange(self, nullptr, first, last);
        bool ok = result == first && Read(*self) == m;
        ei::verify::Report(Name<A>("stlp::vector::erase"), ok, ok ? "" : "the elements differ from the model");
        return result;
    }
    // resize(n): the pointers kept, nulls appended (the released objects may be gone).
    static void __fastcall ResizeDefault(Vector* self, void*, std::size_t n) {
        Model m = Read(*self);
        m.resize(n, nullptr);
        CountedRefVector::ResizeDefault(self, nullptr, n);
        bool ok = Read(*self) == m;
        ei::verify::Report(Name<A>("stlp::vector::resize"), ok, ok ? "" : "the elements differ from the model");
    }
};

template <std::uintptr_t A> using CStrings = stlp_verify::ModelCheck<A, CStringVector, CStringText>;
template <std::uintptr_t A> using Strings = stlp_verify::ModelCheck<A, StringVector, StringText>;

} // namespace

// vector<CString>.
EI_VERIFY(0x0051F5B0, CStrings<0x0051F5B0>::ResizeDefault, 0, s_unused);
EI_VERIFY(0x0051F790, CStrings<0x0051F790>::InsertOverflow, 0, s_unused);
EI_VERIFY(0x0052E010, CStrings<0x0052E010>::Clear, 0, s_unused);
EI_VERIFY(0x0053F020, CStrings<0x0053F020>::Destruct, 0, s_unused);
EI_VERIFY(0x006AF790, CStrings<0x006AF790>::PushBack, 0, s_unused);
EI_VERIFY(0x006AFB60, CStrings<0x006AFB60>::EraseRange, 0, s_unused);
EI_VERIFY(0x006B02B0, CStrings<0x006B02B0>::Reserve, 0, s_unused);

// vector<stlp::string>.
EI_VERIFY(0x004DC900, Strings<0x004DC900>::ResizeDefault, 0, s_unused);
EI_VERIFY(0x004DCE90, Strings<0x004DCE90>::InsertOverflow, 0, s_unused);
EI_VERIFY(0x00575E50, Strings<0x00575E50>::RangeInsert, 0, s_unused);
EI_VERIFY(0x00581860, Strings<0x00581860>::Clear, 0, s_unused);
EI_VERIFY(0x00583CB0, Strings<0x00583CB0>::EraseRange, 0, s_unused);

// vector<RefPtr> / vector<counted_ptr<T>>.
EI_VERIFY(0x00426F00, RefCheck<0x00426F00>::InsertOverflow, 0, s_unused);
EI_VERIFY(0x004AB320, RefCheck<0x004AB320>::InsertOverflow, 0, s_unused);
EI_VERIFY(0x004AB520, RefCheck<0x004AB520>::InsertOverflow, 0, s_unused);
EI_VERIFY(0x0060A120, RefCheck<0x0060A120>::InsertOverflow, 0, s_unused);
EI_VERIFY(0x00421ED0, RefCheck<0x00421ED0>::EraseRange, 0, s_unused);
EI_VERIFY(0x004A9A00, RefCheck<0x004A9A00>::EraseRange, 0, s_unused);
EI_VERIFY(0x004200C0, RefCheck<0x004200C0>::ResizeDefault, 0, s_unused);
EI_VERIFY(0x004AD700, RefCheck<0x004AD700>::ResizeDefault, 0, s_unused);
EI_VERIFY(0x004AD8D0, RefCheck<0x004AD8D0>::ResizeDefault, 0, s_unused);
