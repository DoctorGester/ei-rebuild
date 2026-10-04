// Checks for aligned_arrays.cpp: ours only (they allocate). SetSize leaves the size asked for and
// keeps the elements it keeps; Add returns the old size and stores the value; SetAtGrow stores the
// value and covers the index; the copy equals its source; a new block is 32-byte aligned.
#include "verify.h"

#include <cstdint>
#include <cstring>

namespace aligned {
struct Array {
    std::uint8_t* data;
    int size;
    int capacity;
    int growBy;
};
template <std::size_t K>
struct Blob {
    std::uint8_t bytes[K];
};
void __fastcall DwordArraySetSize(Array* a, void*, int n, int growBy);
void __fastcall Vector3ArraySetSize(Array* a, void*, int n, int growBy);
int __fastcall Vector3ArrayAdd(Array* a, void*, Blob<12> value);
void __fastcall DwordArraySetAtGrowRef(Array* a, void*, int index, const std::uint32_t* value);
void __fastcall Vector3ArrayCopy(Array* a, void*, const Array* other);
std::uint8_t* __fastcall AllocateVectors(int count);
} // namespace aligned

namespace {
using namespace aligned;
void* s_unused;

template <std::size_t K>
void CheckSetSize(const char* name, void(__fastcall* fn)(Array*, void*, int, int), Array* a, int n, int growBy) {
    std::uint8_t before[64 * K];
    int kept = a->size < n ? a->size : n;
    if (kept > 64) kept = 64;
    if (kept > 0) std::memcpy(before, a->data, static_cast<std::size_t>(kept) * K);
    fn(a, nullptr, n, growBy);
    bool ok = a->size == n && a->capacity >= n && (kept <= 0 || std::memcmp(before, a->data, static_cast<std::size_t>(kept) * K) == 0) &&
              (!a->data || (reinterpret_cast<std::uintptr_t>(a->data) & 0x1F) == 0);
    ei::verify::Check(name, true, ok, "");
}

void __fastcall SetSize4Verify(Array* a, void*, int n, int growBy) { CheckSetSize<4>("DwordArraySetSize", DwordArraySetSize, a, n, growBy); }
void __fastcall SetSize12Verify(Array* a, void*, int n, int growBy) { CheckSetSize<12>("Vector3ArraySetSize", Vector3ArraySetSize, a, n, growBy); }
int __fastcall Add12Verify(Array* a, void*, Blob<12> value) {
    int old = a->size;
    int index = Vector3ArrayAdd(a, nullptr, value);
    ei::verify::Check("Vector3ArrayAdd", true, index == old && a->size == old + 1 && std::memcmp(a->data + index * 12, &value, 12) == 0, "");
    return index;
}
void __fastcall SetAtGrowVerify(Array* a, void*, int index, const std::uint32_t* value) {
    std::uint32_t v = *value;
    DwordArraySetAtGrowRef(a, nullptr, index, value);
    ei::verify::Check("DwordArraySetAtGrowRef", true, a->size > index && std::memcmp(a->data + index * 4, &v, 4) == 0, "");
}
void __fastcall CopyVerify(Array* a, void*, const Array* other) {
    Vector3ArrayCopy(a, nullptr, other);
    ei::verify::Check("Vector3ArrayCopy", true, a->size == other->size && (a->size == 0 || std::memcmp(a->data, other->data, static_cast<std::size_t>(a->size) * 12) == 0), "");
}
std::uint8_t* __fastcall AllocVerify(int count) {
    std::uint8_t* p = AllocateVectors(count);
    auto* block = static_cast<std::uint8_t*>(reinterpret_cast<void**>(p)[-1]);
    ei::verify::Check("AllocateVectors", true, (reinterpret_cast<std::uintptr_t>(p) & 0x1F) == 0 && p > block && p - block <= 0x23, "");
    return p;
}
} // namespace

EI_VERIFY(0x0044F540, SetSize4Verify, 0, s_unused);
EI_VERIFY(0x0044F1D0, SetSize12Verify, 0, s_unused);
EI_VERIFY(0x0044F2E0, Add12Verify, 0, s_unused);
EI_VERIFY(0x00452C40, SetAtGrowVerify, 0, s_unused);
EI_VERIFY(0x0044F410, CopyVerify, 0, s_unused);
EI_VERIFY(0x00452C20, AllocVerify, 0, s_unused);
