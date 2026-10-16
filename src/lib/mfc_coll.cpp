// MFC 4.2's CStringArray and plain arrays, registered at their library addresses (mfc_coll.h).
#include "mfc_archive.h"
#include "mfc_coll.h"

#include "ei/hook.h"

#include <cstring>
#include <new>

#pragma warning(disable : 4740) // naked forwarders: no optimisation to suppress

namespace mfc::impl {

namespace {

// CArchive's CString operators (MFC's arccore.cpp; S8).

// The growth when the allocation is outgrown: the step, or size / 8 clamped to 4..1024.
int GrowthStep(int growBy, int size) {
    if (growBy != 0) return growBy;
    int step = size / 8;
    return step < 4 ? 4 : step > 1024 ? 1024 : step;
}

} // namespace

// ---- The plain arrays ---------------------------------------------------------------------------

template <typename T, typename Class>
PlainArray<T, Class>* PlainArray<T, Class>::Construct() {
    vtable = Class::Vtable();
    data = nullptr;
    growBy = 0;
    maxSize = 0;
    size = 0;
    return this;
}

template <typename T, typename Class>
void PlainArray<T, Class>::Destruct() {
    vtable = Class::Vtable();
    gamecrt::operator_delete(data);
}

template <typename T, typename Class>
PlainArray<T, Class>* PlainArray<T, Class>::DeletingDestructor(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}

// Growing past the allocation reallocates to max(newSize, maxSize + step) and copies; new elements
// are zeroed; shrinking keeps the allocation; 0 frees it. newGrowBy -1 keeps the step.
template <typename T, typename Class>
void PlainArray<T, Class>::SetSize(int newSize, int newGrowBy) {
    if (newGrowBy != -1) growBy = newGrowBy;
    if (newSize == 0) {
        gamecrt::operator_delete(data);
        data = nullptr;
        size = maxSize = 0;
    } else if (!data) {
        data = static_cast<T*>(gamecrt::operator_new(static_cast<std::size_t>(newSize) * sizeof(T)));
        std::memset(data, 0, static_cast<std::size_t>(newSize) * sizeof(T));
        size = maxSize = newSize;
    } else if (newSize <= maxSize) {
        if (newSize > size) std::memset(data + size, 0, static_cast<std::size_t>(newSize - size) * sizeof(T));
        size = newSize;
    } else {
        int step = GrowthStep(growBy, size);
        int newMax = newSize < maxSize + step ? maxSize + step : newSize;
        T* fresh = static_cast<T*>(gamecrt::operator_new(static_cast<std::size_t>(newMax) * sizeof(T)));
        std::memcpy(fresh, data, static_cast<std::size_t>(size) * sizeof(T));
        std::memset(fresh + size, 0, static_cast<std::size_t>(newSize - size) * sizeof(T));
        gamecrt::operator_delete(data);
        data = fresh;
        size = newSize;
        maxSize = newMax;
    }
}

template <typename T, typename Class>
void PlainArray<T, Class>::SetAtGrow(int index, T value) {
    if (index >= size) SetSize(index + 1, -1);
    data[index] = value;
}

// The count, then the elements as raw bytes.
template <typename T, typename Class>
void PlainArray<T, Class>::Serialize(CArchive* ar) {
    if (!ar->IsLoading()) {
        ar->WriteCount(static_cast<unsigned long>(size));
        ar->Write(data, static_cast<unsigned>(size) * sizeof(T));
    } else {
        SetSize(static_cast<int>(ar->ReadCount()), -1);
        ar->Read(data, static_cast<unsigned>(size) * sizeof(T));
    }
}

template struct PlainArray<std::uint16_t, WordArrayClass>;
template struct PlainArray<std::uint32_t, DWordArrayClass>;
template struct PlainArray<std::uint8_t, ByteArrayClass>;
template struct PlainArray<unsigned, UIntArrayClass>;

// ---- CStringArray -------------------------------------------------------------------------------

void __cdecl CStringArray::ConstructElements(CString* elements, int count) {
    for (; count--; ++elements) ::new (static_cast<void*>(elements)) CString;
}

void __cdecl CStringArray::DestructElements(CString* elements, int count) {
    for (; count--; ++elements) elements->~CString();
}

void __cdecl CStringArray::CopyElements(CString* dest, const CString* source, int count) {
    while (count--) *dest++ = *source++;
}

CStringArray* CStringArray::Construct() {
    vtable = Vtable();
    data = nullptr;
    growBy = 0;
    maxSize = 0;
    size = 0;
    return this;
}

void CStringArray::Destruct() {
    vtable = Vtable();
    DestructElements(data, size);
    gamecrt::operator_delete(data);
}

CStringArray* CStringArray::DeletingDestructor(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}

// As the plain arrays, with the elements constructed as empty strings and released when cut off;
// a reallocation moves the strings bytewise.
void CStringArray::SetSize(int newSize, int newGrowBy) {
    if (newGrowBy != -1) growBy = newGrowBy;
    if (newSize == 0) {
        DestructElements(data, size);
        gamecrt::operator_delete(data);
        data = nullptr;
        size = maxSize = 0;
    } else if (!data) {
        data = static_cast<CString*>(gamecrt::operator_new(static_cast<std::size_t>(newSize) * sizeof(CString)));
        ConstructElements(data, newSize);
        size = maxSize = newSize;
    } else if (newSize <= maxSize) {
        if (newSize > size) ConstructElements(data + size, newSize - size);
        else if (size > newSize) DestructElements(data + newSize, size - newSize);
        size = newSize;
    } else {
        int step = GrowthStep(growBy, size);
        int newMax = newSize < maxSize + step ? maxSize + step : newSize;
        auto* fresh = static_cast<CString*>(gamecrt::operator_new(static_cast<std::size_t>(newMax) * sizeof(CString)));
        std::memcpy(static_cast<void*>(fresh), data, static_cast<std::size_t>(size) * sizeof(CString));
        ConstructElements(fresh + size, newSize - size);
        gamecrt::operator_delete(data);
        data = fresh;
        size = newSize;
        maxSize = newMax;
    }
}

int CStringArray::Append(const CStringArray& source) {
    int oldSize = size;
    SetSize(size + source.size, -1);
    CopyElements(data + oldSize, source.data, source.size);
    return oldSize;
}

void CStringArray::Copy(const CStringArray& source) {
    SetSize(source.size, -1);
    CopyElements(data, source.data, source.size);
}

void CStringArray::SetAtGrow(int index, const char* text) {
    if (index >= size) SetSize(index + 1, -1);
    data[index] = text;
}

void CStringArray::SetAtGrow(int index, const CString& value) {
    if (index >= size) SetSize(index + 1, -1);
    data[index] = value;
}

// `count` empty strings at `index` (the tail moved up bytewise).
void CStringArray::InsertEmpty(int index, int count) {
    if (index >= size) {
        SetSize(index + count, -1);
    } else {
        int oldSize = size;
        SetSize(size + count, -1);
        std::memmove(static_cast<void*>(data + index + count), data + index,
                     static_cast<std::size_t>(oldSize - index) * sizeof(CString));
        ConstructElements(data + index, count);
    }
}

void CStringArray::InsertAt(int index, const CString& value, int count) {
    InsertEmpty(index, count);
    while (count--) data[index++] = value;
}

void CStringArray::RemoveAt(int index, int count) {
    int moveCount = size - (index + count);
    DestructElements(data + index, count);
    if (moveCount)
        std::memmove(static_cast<void*>(data + index), data + index + count,
                     static_cast<std::size_t>(moveCount) * sizeof(CString));
    size -= count;
}

void CStringArray::Serialize(CArchive* ar) {
    if (!ar->IsLoading()) {
        ar->WriteCount(static_cast<unsigned long>(size));
        for (int i = 0; i < size; ++i) mfc::impl::StoreText(*ar, data[i]);
    } else {
        SetSize(static_cast<int>(ar->ReadCount()), -1);
        for (int i = 0; i < size; ++i) mfc::impl::LoadText(*ar, data[i]);
    }
}

// ---- Registrations ------------------------------------------------------------------------------

namespace shim {
void __fastcall SetAtGrowText(CStringArray* self, void*, int index, const char* text) { self->SetAtGrow(index, text); }
void __fastcall SetAtGrowString(CStringArray* self, void*, int index, const CString& value) {
    self->SetAtGrow(index, value);
}
} // namespace shim

EI_REPLACE_METHOD(0x006FEE9A, CWordArray::Construct);
EI_REPLACE_METHOD(0x006FEEB1, CWordArray::DeletingDestructor);
EI_REPLACE_METHOD(0x006FEECD, CWordArray::Destruct);
EI_REPLACE_METHOD(0x006FEEFA, CWordArray::SetSize);
EI_REPLACE_METHOD(0x006FF0B4, CWordArray::SetAtGrow);
EI_REPLACE_METHOD(0x006FF1E7, CWordArray::Serialize);

EI_REPLACE_METHOD(0x006FF23A, CDWordArray::Construct);
EI_REPLACE_METHOD(0x006FF251, CDWordArray::DeletingDestructor);
EI_REPLACE_METHOD(0x006FF26D, CDWordArray::Destruct);
EI_REPLACE_METHOD(0x006FF29A, CDWordArray::SetSize);
EI_REPLACE_METHOD(0x006FF45E, CDWordArray::SetAtGrow);
EI_REPLACE_METHOD(0x006FF595, CDWordArray::Serialize);

EI_REPLACE_METHOD(0x006FF5EA, CByteArray::Construct);
EI_REPLACE_METHOD(0x006FF601, CByteArray::DeletingDestructor);
EI_REPLACE_METHOD(0x006FF61D, CByteArray::Destruct);
EI_REPLACE_METHOD(0x006FF64A, CByteArray::SetSize);
EI_REPLACE_METHOD(0x006FF7DC, CByteArray::SetAtGrow);
EI_REPLACE_METHOD(0x006FF8E6, CByteArray::Serialize);

EI_REPLACE_METHOD(0x006FF933, CUIntArray::Construct);
EI_REPLACE_METHOD(0x006FF94A, CUIntArray::DeletingDestructor);
EI_REPLACE_METHOD(0x006FF966, CUIntArray::Destruct);
EI_REPLACE_METHOD(0x006FF993, CUIntArray::SetSize);
EI_REPLACE_METHOD(0x006FFB57, CUIntArray::SetAtGrow);

EI_REPLACE_METHOD(0x006FE925, CStringArray::Construct);
EI_REPLACE_METHOD(0x006FE93C, CStringArray::DeletingDestructor);
EI_REPLACE_METHOD(0x006FE958, CStringArray::Destruct);
EI_REPLACE_METHOD(0x006FE9B8, CStringArray::SetSize);
EI_REPLACE_METHOD(0x006FEB01, CStringArray::Append);
EI_REPLACE_METHOD(0x006FEB61, CStringArray::Copy);
EI_REPLACE(0x006FEBCE, shim::SetAtGrowText);
EI_REPLACE(0x006FEBFA, shim::SetAtGrowString);
EI_REPLACE_METHOD(0x006FEC26, CStringArray::InsertEmpty);
EI_REPLACE_METHOD(0x006FED03, CStringArray::InsertAt);
EI_REPLACE_METHOD(0x006FED3C, CStringArray::RemoveAt);
EI_REPLACE_METHOD(0x006FEE31, CStringArray::Serialize);
EI_REPLACE(0x006FEAD5, CStringArray::ConstructElements);
EI_REPLACE(0x006FE996, CStringArray::DestructElements);
EI_REPLACE(0x006FEB37, CStringArray::CopyElements);

} // namespace mfc::impl
