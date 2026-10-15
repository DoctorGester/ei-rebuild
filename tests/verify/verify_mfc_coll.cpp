// Checks for MFC's CStringArray and plain arrays (src/lib/mfc_coll.cpp, ticket S5). The plain arrays
// hold plain data: SetSize and SetAtGrow run as the original on a twin (its own copy of the
// elements, the same sizes) and as ours on the caller's array, and the arrays are compared. The
// constructors, destructors and serialisers, and CStringArray (its elements are reference-counted
// strings), run ours only, with checks of the state they leave.
#include "verify.h"

#include "../../src/lib/mfc_coll.h"

#include <cstring>

using mfc::impl::CString;
using mfc::impl::CStringArray;
using mfc::impl::PlainArray;

namespace {

using ei::verify::Check;
void* s_unused;

// ---- The plain arrays ---------------------------------------------------------------------------

// A copy with its own storage of the same allocated size.
template <typename Array>
Array Twin(const Array& a) {
    Array twin = a;
    if (a.data) {
        using T = std::remove_pointer_t<decltype(a.data)>;
        twin.data = static_cast<T*>(gamecrt::operator_new(static_cast<std::size_t>(a.maxSize) * sizeof(T)));
        std::memcpy(twin.data, a.data, static_cast<std::size_t>(a.size) * sizeof(T));
    }
    return twin;
}

template <typename Array>
bool Same(const Array& expected, const Array& actual) {
    using T = std::remove_pointer_t<decltype(actual.data)>;
    return expected.size == actual.size && expected.maxSize == actual.maxSize && expected.growBy == actual.growBy &&
           expected.vtable == actual.vtable && (expected.data == nullptr) == (actual.data == nullptr) &&
           (actual.size == 0 || std::memcmp(expected.data, actual.data, static_cast<std::size_t>(actual.size) * sizeof(T)) == 0);
}

template <typename Array>
struct Checks {
    using T = std::remove_pointer_t<decltype(Array::data)>;
    using SetSizeFn = void(__fastcall*)(Array*, void*, int, int);
    using SetAtGrowFn = void(__fastcall*)(Array*, void*, int, T);
    static inline SetSizeFn setSize;
    static inline SetAtGrowFn setAtGrow;
    static inline const char* name;

    static void __fastcall SetSize(Array* self, void*, int newSize, int newGrowBy) {
        Array twin = Twin(*self);
        setSize(&twin, nullptr, newSize, newGrowBy);
        self->SetSize(newSize, newGrowBy);
        Check(name, true, Same(twin, *self), "SetSize %d, %d: %d / %d (original %d / %d)", newSize, newGrowBy, self->size,
              self->maxSize, twin.size, twin.maxSize);
        gamecrt::operator_delete(twin.data);
    }

    static void __fastcall SetAtGrow(Array* self, void*, int index, T value) {
        Array twin = Twin(*self);
        setAtGrow(&twin, nullptr, index, value);
        self->SetAtGrow(index, value);
        Check(name, true, Same(twin, *self), "SetAtGrow %d: %d / %d", index, self->size, self->maxSize);
        gamecrt::operator_delete(twin.data);
    }

    // Ours only: an empty array with the class's vtable.
    static Array* __fastcall Construct(Array* self) {
        Array* result = self->Construct();
        Check(name, true, result == self && self->size == 0 && self->data == nullptr, "constructor");
        return result;
    }

    static void __fastcall Destruct(Array* self) {
        self->Destruct();
        Check(name, true, true, "destructor");
    }

    static Array* __fastcall DeletingDestructor(Array* self, void*, unsigned flags) {
        Array* result = self->DeletingDestructor(flags);
        Check(name, true, result == self, "deleting destructor");
        return result;
    }

    // Ours only: the size after loading is consistent with the storage.
    static void __fastcall Serialize(Array* self, void*, mfc::CArchive* ar) {
        self->Serialize(ar);
        Check(name, true, self->size <= self->maxSize && (self->size == 0 || self->data), "Serialize: %d", self->size);
    }
};

using Word = Checks<mfc::impl::CWordArray>;
using DWord = Checks<mfc::impl::CDWordArray>;
using Byte = Checks<mfc::impl::CByteArray>;
using UInt = Checks<mfc::impl::CUIntArray>;

void NameArrays() {
    Word::name = "CWordArray";
    DWord::name = "CDWordArray";
    Byte::name = "CByteArray";
    UInt::name = "CUIntArray";
}
struct Names {
    Names() { NameArrays(); }
} s_names;

// ---- CStringArray -------------------------------------------------------------------------------

bool Live(const CStringArray& a, int from, int to) {
    for (int i = from; i < to; ++i)
        if (a.data[i].GetData()->refs == 0) return false;
    return true;
}

bool Equal(const CString& a, const CString& b) {
    return a.GetLength() == b.GetLength() &&
           std::memcmp(static_cast<const char*>(a), static_cast<const char*>(b), static_cast<std::size_t>(a.GetLength())) == 0;
}

CStringArray* __fastcall StringArrayConstructVerify(CStringArray* self) {
    CStringArray* result = self->Construct();
    Check("CStringArray::CStringArray", true, result == self && self->size == 0 && self->data == nullptr, "");
    return result;
}

void __fastcall StringArrayDestructVerify(CStringArray* self) {
    bool ok = Live(*self, 0, self->size);
    self->Destruct();
    Check("CStringArray::~CStringArray", true, ok, "");
}

CStringArray* __fastcall StringArrayDeletingVerify(CStringArray* self, void*, unsigned flags) {
    bool ok = Live(*self, 0, self->size);
    CStringArray* result = self->DeletingDestructor(flags);
    Check("CStringArray deleting destructor", true, ok && result == self, "");
    return result;
}

// Ours only: the new size, every element a live string.
void __fastcall StringArraySetSizeVerify(CStringArray* self, void*, int newSize, int newGrowBy) {
    self->SetSize(newSize, newGrowBy);
    bool ok = self->size == newSize && self->maxSize >= newSize && Live(*self, 0, self->size);
    Check("CStringArray::SetSize", true, ok, "%d: %d / %d", newSize, self->size, self->maxSize);
}

int __fastcall StringArrayAppendVerify(CStringArray* self, void*, const CStringArray& source) {
    int sourceSize = source.size;
    int result = self->Append(source);
    bool ok = self->size == result + sourceSize;
    for (int i = 0; ok && i < sourceSize; ++i) ok = Equal(self->data[result + i], source.data[i]);
    Check("CStringArray::Append", true, ok, "%d + %d", result, sourceSize);
    return result;
}

void __fastcall StringArrayCopyVerify(CStringArray* self, void*, const CStringArray& source) {
    self->Copy(source);
    bool ok = self->size == source.size;
    for (int i = 0; ok && i < self->size; ++i) ok = Equal(self->data[i], source.data[i]);
    Check("CStringArray::Copy", true, ok, "%d", self->size);
}

void __fastcall StringArraySetAtGrowTextVerify(CStringArray* self, void*, int index, const char* text) {
    self->SetAtGrow(index, text);
    bool ok = index < self->size && std::strcmp(self->data[index], text ? text : "") == 0 && Live(*self, 0, self->size);
    Check("CStringArray::SetAtGrow(int, const char*)", true, ok, "%d", index);
}

void __fastcall StringArraySetAtGrowVerify(CStringArray* self, void*, int index, const CString& value) {
    self->SetAtGrow(index, value);
    bool ok = index < self->size && Equal(self->data[index], value) && Live(*self, 0, self->size);
    Check("CStringArray::SetAtGrow(int, const CString&)", true, ok, "%d", index);
}

void __fastcall StringArrayInsertEmptyVerify(CStringArray* self, void*, int index, int count) {
    int oldSize = self->size;
    self->InsertEmpty(index, count);
    bool ok = self->size == (index >= oldSize ? index + count : oldSize + count) && Live(*self, 0, self->size);
    Check("CStringArray::InsertEmpty", true, ok, "%d at %d", count, index);
}

void __fastcall StringArrayInsertAtVerify(CStringArray* self, void*, int index, const CString& value, int count) {
    CString copy(value);   // `value` may be an element that moves
    self->InsertAt(index, value, count);
    bool ok = Live(*self, 0, self->size);
    for (int i = index; ok && i < index + count; ++i) ok = Equal(self->data[i], copy);
    Check("CStringArray::InsertAt", true, ok, "%d at %d", count, index);
}

void __fastcall StringArrayRemoveAtVerify(CStringArray* self, void*, int index, int count) {
    int oldSize = self->size;
    self->RemoveAt(index, count);
    Check("CStringArray::RemoveAt", true, self->size == oldSize - count && Live(*self, 0, self->size), "%d at %d",
          count, index);
}

void __fastcall StringArraySerializeVerify(CStringArray* self, void*, mfc::CArchive* ar) {
    self->Serialize(ar);
    Check("CStringArray::Serialize", true, Live(*self, 0, self->size), "%d", self->size);
}

void __cdecl ConstructElementsVerify(CString* elements, int count) {
    CStringArray::ConstructElements(elements, count);
    bool ok = true;
    for (int i = 0; i < count; ++i) ok = ok && static_cast<const char*>(elements[i]) == mfc::impl::PchNil();
    Check("CStringArray ConstructElements", true, ok, "%d", count);
}

void __cdecl DestructElementsVerify(CString* elements, int count) {
    bool ok = true;
    for (int i = 0; i < count; ++i) ok = ok && elements[i].GetData()->refs != 0;
    CStringArray::DestructElements(elements, count);
    Check("CStringArray DestructElements", true, ok, "%d", count);
}

void __cdecl CopyElementsVerify(CString* dest, const CString* source, int count) {
    CStringArray::CopyElements(dest, source, count);
    bool ok = true;
    for (int i = 0; i < count; ++i) ok = ok && Equal(dest[i], source[i]);
    Check("CStringArray CopyElements", true, ok, "%d", count);
}

} // namespace

EI_VERIFY(0x006FEE9A, Word::Construct, 0, s_unused);
EI_VERIFY(0x006FEEB1, Word::DeletingDestructor, 0, s_unused);
EI_VERIFY(0x006FEECD, Word::Destruct, 0, s_unused);
EI_VERIFY(0x006FEEFA, Word::SetSize, 6, Word::setSize);
EI_VERIFY(0x006FF0B4, Word::SetAtGrow, 5, Word::setAtGrow);
EI_VERIFY(0x006FF1E7, Word::Serialize, 0, s_unused);

EI_VERIFY(0x006FF23A, DWord::Construct, 0, s_unused);
EI_VERIFY(0x006FF251, DWord::DeletingDestructor, 0, s_unused);
EI_VERIFY(0x006FF26D, DWord::Destruct, 0, s_unused);
EI_VERIFY(0x006FF29A, DWord::SetSize, 6, DWord::setSize);
EI_VERIFY(0x006FF45E, DWord::SetAtGrow, 5, DWord::setAtGrow);
EI_VERIFY(0x006FF595, DWord::Serialize, 0, s_unused);

EI_VERIFY(0x006FF5EA, Byte::Construct, 0, s_unused);
EI_VERIFY(0x006FF601, Byte::DeletingDestructor, 0, s_unused);
EI_VERIFY(0x006FF61D, Byte::Destruct, 0, s_unused);
EI_VERIFY(0x006FF64A, Byte::SetSize, 6, Byte::setSize);
EI_VERIFY(0x006FF7DC, Byte::SetAtGrow, 5, Byte::setAtGrow);
EI_VERIFY(0x006FF8E6, Byte::Serialize, 0, s_unused);

EI_VERIFY(0x006FF933, UInt::Construct, 0, s_unused);
EI_VERIFY(0x006FF94A, UInt::DeletingDestructor, 0, s_unused);
EI_VERIFY(0x006FF966, UInt::Destruct, 0, s_unused);
EI_VERIFY(0x006FF993, UInt::SetSize, 6, UInt::setSize);
EI_VERIFY(0x006FFB57, UInt::SetAtGrow, 5, UInt::setAtGrow);

EI_VERIFY(0x006FE925, StringArrayConstructVerify, 0, s_unused);
EI_VERIFY(0x006FE93C, StringArrayDeletingVerify, 0, s_unused);
EI_VERIFY(0x006FE958, StringArrayDestructVerify, 0, s_unused);
EI_VERIFY(0x006FE9B8, StringArraySetSizeVerify, 0, s_unused);
EI_VERIFY(0x006FEB01, StringArrayAppendVerify, 0, s_unused);
EI_VERIFY(0x006FEB61, StringArrayCopyVerify, 0, s_unused);
EI_VERIFY(0x006FEBCE, StringArraySetAtGrowTextVerify, 0, s_unused);
EI_VERIFY(0x006FEBFA, StringArraySetAtGrowVerify, 0, s_unused);
EI_VERIFY(0x006FEC26, StringArrayInsertEmptyVerify, 0, s_unused);
EI_VERIFY(0x006FED03, StringArrayInsertAtVerify, 0, s_unused);
EI_VERIFY(0x006FED3C, StringArrayRemoveAtVerify, 0, s_unused);
EI_VERIFY(0x006FEE31, StringArraySerializeVerify, 0, s_unused);
EI_VERIFY(0x006FEAD5, ConstructElementsVerify, 0, s_unused);
EI_VERIFY(0x006FE996, DestructElementsVerify, 0, s_unused);
EI_VERIFY(0x006FEB37, CopyElementsVerify, 0, s_unused);
