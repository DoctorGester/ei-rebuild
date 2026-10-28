// MFC 4.2's plain arrays (mfc_coll.h).
#include "mfc_archive.h"
#include "mfc_coll.h"
#include "mfc_runtime_class.h"
#include "mfc_state.h"

#include "compiled_vtable.h"

#include <cstring>
#include <new>

namespace mfc::impl {

namespace {

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
        data = gamecrt::operator_new_array<T>(static_cast<std::size_t>(newSize));
        std::memset(data, 0, static_cast<std::size_t>(newSize) * sizeof(T));
        size = maxSize = newSize;
    } else if (newSize <= maxSize) {
        if (newSize > size) std::memset(data + size, 0, static_cast<std::size_t>(newSize - size) * sizeof(T));
        size = newSize;
    } else {
        int step = GrowthStep(growBy, size);
        int newMax = newSize < maxSize + step ? maxSize + step : newSize;
        T* fresh = gamecrt::operator_new_array<T>(static_cast<std::size_t>(newMax));
        std::memcpy(fresh, data, static_cast<std::size_t>(size) * sizeof(T));
        std::memset(fresh + size, 0, static_cast<std::size_t>(newSize - size) * sizeof(T));
        gamecrt::operator_delete(data);
        data = fresh;
        size = newSize;
        maxSize = newMax;
    }
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


// ---- The serializable classes' runtime classes ----------------------------------------------------

// The create functions: a new array from operator new, constructed; null when out of memory.
void* __stdcall CreateCWordArray() {
    auto* array = gamecrt::operator_new_as<CWordArray>();
    return array ? array->Construct() : nullptr;
}

void* __stdcall CreateCDWordArray() {
    auto* array = gamecrt::operator_new_as<CDWordArray>();
    return array ? array->Construct() : nullptr;
}

void* __stdcall CreateCByteArray() {
    auto* array = gamecrt::operator_new_as<CByteArray>();
    return array ? array->Construct() : nullptr;
}

// mfc_slots.h's compiled collections' slot 0.
const CRuntimeClass* ObjectRuntimeClass() { return &kClassCObject; }

namespace {

// IMPLEMENT_SERIAL (schema 0): the record is linked into the module state's class list by the class's static
// initialiser (AfxClassInit writes m_pNextClass).
constinit CRuntimeClass g_classCWordArray = /*00796F20*/ {"CWordArray", 0x14, 0, &CreateCWordArray, &kClassCObject, nullptr};
constinit CRuntimeClass g_classCDWordArray = /*00797140*/ {"CDWordArray", 0x14, 0, &CreateCDWordArray, &kClassCObject, nullptr};
constinit CRuntimeClass g_classCByteArray = /*00797198*/ {"CByteArray", 0x14, 0, &CreateCByteArray, &kClassCObject, nullptr};

// The arrays' compiled tables in place (CWordArray, CDWordArray and CByteArray): 0 GetRuntimeClass
// (the class's record), 1 the deleting destructor, 2 Serialize.
template <typename Self, typename Array, const CRuntimeClass* kClass>
struct ArrayTable : ei::CompiledTag<Self> {
    virtual const CRuntimeClass* GetRuntimeClass() const { return kClass; }                   // 0
    virtual void* DeleteScalar(unsigned flags) { return This()->DeletingDestructor(flags); }  // 1
    virtual void Serialize(CArchive& archive) { This()->Serialize(&archive); }                // 2

private:
    Array* This() { return this->template ObjectAs<Array>(); }
};
struct WordArrayTable final : ArrayTable<WordArrayTable, CWordArray, &g_classCWordArray> {};
struct DWordArrayTable final : ArrayTable<DWordArrayTable, CDWordArray, &g_classCDWordArray> {};
struct ByteArrayTable final : ArrayTable<ByteArrayTable, CByteArray, &g_classCByteArray> {};

} // namespace

// The static initialisers (the exe's list holds the first instruction of each: a jump to the body).
void LinkWordArrayClass() { AfxClassInit(&g_classCWordArray); }
void LinkDWordArrayClass() { AfxClassInit(&g_classCDWordArray); }
void LinkByteArrayClass() { AfxClassInit(&g_classCByteArray); }

void* WordArrayClass::Vtable() { return WordArrayTable::Vtable(); }
void* DWordArrayClass::Vtable() { return DWordArrayTable::Vtable(); }
void* ByteArrayClass::Vtable() { return ByteArrayTable::Vtable(); }

} // namespace mfc::impl
