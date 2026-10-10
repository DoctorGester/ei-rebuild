// Checks for aligned_arrays.cpp: ours only (they allocate). SetSize leaves the size asked for and
// keeps the elements it keeps; Add returns the old size and stores the value; SetAtGrow stores the
// value and covers the index; the copy equals its source; a new block is 32-byte aligned.
#include "verify.h"

#include "../../src/lib/containers.h"

#include <cstdint>
#include <cstring>

namespace aligned {
template <std::size_t K>
struct Blob {
    std::uint8_t bytes[K];
};
using Dwords = lib::AlignedArray<std::uint32_t>;
using Vectors = lib::AlignedArray<Blob<12>>;
void __fastcall DwordArraySetSize(Dwords* a, void*, int n, int growBy);
void __fastcall Vector3ArraySetSize(Vectors* a, void*, int n, int growBy);
int __fastcall Vector3ArrayAdd(Vectors* a, void*, Blob<12> value);
void __fastcall DwordArraySetAtGrowRef(Dwords* a, void*, int index, const std::uint32_t* value);
void __fastcall DwordArraySetAtGrow(Dwords* a, void*, int index, std::uint32_t value);
void __fastcall Vector3ArrayCopy(Vectors* a, void*, const Vectors* other);
Blob<12>* __fastcall AllocateVectors(int count);
std::uint32_t* __fastcall AllocateDwords(int count);
void __fastcall ArrayFree1(lib::AlignedArray<std::uint8_t>* a);
} // namespace aligned

namespace {
using namespace aligned;
void* s_unused;

// The arrays seen as bytes (the checks compare element bytes).
using Array = lib::AlignedArray<std::uint8_t>;

template <std::size_t K, typename A>
void CheckSetSize(const char* name, void(__fastcall* fn)(A*, void*, int, int), Array* a, int n, int growBy) {
    std::uint8_t before[64 * K];
    int kept = a->size < n ? a->size : n;
    if (kept > 64) kept = 64;
    if (kept > 0) std::memcpy(before, a->data, static_cast<std::size_t>(kept) * K);
    fn(reinterpret_cast<A*>(a), nullptr, n, growBy);
    bool ok = a->size == n && a->capacity >= n && (kept <= 0 || std::memcmp(before, a->data, static_cast<std::size_t>(kept) * K) == 0) &&
              (!a->data || (reinterpret_cast<std::uintptr_t>(a->data) & 0x1F) == 0);
    ei::verify::Check(name, true, ok, "");
}

void __fastcall SetSize4Verify(Array* a, void*, int n, int growBy) { CheckSetSize<4>("DwordArraySetSize", DwordArraySetSize, a, n, growBy); }
void __fastcall SetSize12Verify(Array* a, void*, int n, int growBy) { CheckSetSize<12>("Vector3ArraySetSize", Vector3ArraySetSize, a, n, growBy); }
int __fastcall Add12Verify(Array* a, void*, Blob<12> value) {
    int old = a->size;
    int index = Vector3ArrayAdd(reinterpret_cast<Vectors*>(a), nullptr, value);
    ei::verify::Check("Vector3ArrayAdd", true, index == old && a->size == old + 1 && std::memcmp(a->data + index * 12, &value, 12) == 0, "");
    return index;
}
void __fastcall SetAtGrowVerify(Array* a, void*, int index, const std::uint32_t* value) {
    std::uint32_t v = *value;
    DwordArraySetAtGrowRef(reinterpret_cast<Dwords*>(a), nullptr, index, value);
    ei::verify::Check("DwordArraySetAtGrowRef", true, a->size > index && std::memcmp(a->data + index * 4, &v, 4) == 0, "");
}
// A-U11: the by-value SetAtGrow copy.
void __fastcall SetAtGrowValueVerify(Array* a, void*, int index, std::uint32_t value) {
    DwordArraySetAtGrow(reinterpret_cast<Dwords*>(a), nullptr, index, value);
    ei::verify::Check("DwordArraySetAtGrow", true, a->size > index && std::memcmp(a->data + index * 4, &value, 4) == 0, "");
}
void __fastcall CopyVerify(Array* a, void*, const Array* other) {
    Vector3ArrayCopy(reinterpret_cast<Vectors*>(a), nullptr, reinterpret_cast<const Vectors*>(other));
    ei::verify::Check("Vector3ArrayCopy", true, a->size == other->size && (a->size == 0 || std::memcmp(a->data, other->data, static_cast<std::size_t>(a->size) * 12) == 0), "");
}
std::uint8_t* __fastcall AllocVerify(int count) {
    auto* p = reinterpret_cast<std::uint8_t*>(AllocateVectors(count));
    auto* block = static_cast<std::uint8_t*>(reinterpret_cast<void**>(p)[-1]);
    ei::verify::Check("AllocateVectors", true, (reinterpret_cast<std::uintptr_t>(p) & 0x1F) == 0 && p > block && p - block <= 0x23, "");
    return p;
}
// The interface's copies (A-I15; every window's arrays): the allocation is 32-byte aligned with
// its block before it; the free leaves the array's fields as they were.
std::uint32_t* __fastcall AllocDwordsVerify(int count) {
    auto* p = reinterpret_cast<std::uint8_t*>(AllocateDwords(count));
    auto* block = static_cast<std::uint8_t*>(reinterpret_cast<void**>(p)[-1]);
    ei::verify::Check("AllocateDwords", true, (reinterpret_cast<std::uintptr_t>(p) & 0x1F) == 0 && p > block && p - block <= 0x23, "");
    return reinterpret_cast<std::uint32_t*>(p);
}
void __fastcall FreeVerify(lib::AlignedArray<std::uint8_t>* a) {
    lib::AlignedArray<std::uint8_t> before = *a;
    ArrayFree1(a);
    ei::verify::Check("ArrayFree1", true, std::memcmp(&before, a, sizeof(before)) == 0, "");
}
} // namespace

EI_VERIFY(0x00600A80, AllocDwordsVerify, 0, s_unused);
EI_VERIFY(0x00600330, FreeVerify, 0, s_unused);
EI_VERIFY(0x00600350, FreeVerify, 0, s_unused);
EI_VERIFY(0x00600370, FreeVerify, 0, s_unused);
EI_VERIFY(0x00600390, FreeVerify, 0, s_unused);
EI_VERIFY(0x006003B0, FreeVerify, 0, s_unused);
EI_VERIFY(0x00601B50, FreeVerify, 0, s_unused);
EI_VERIFY(0x0044F540, SetSize4Verify, 0, s_unused);
EI_VERIFY(0x005E6810, SetSize4Verify, 0, s_unused);
EI_VERIFY(0x0044F1D0, SetSize12Verify, 0, s_unused);
EI_VERIFY(0x0044F2E0, Add12Verify, 0, s_unused);
EI_VERIFY(0x00452C40, SetAtGrowVerify, 0, s_unused);
EI_VERIFY(0x0044F410, CopyVerify, 0, s_unused);
EI_VERIFY(0x00452C20, AllocVerify, 0, s_unused);
// A-U11: the byte-identical copies in the units range.
EI_VERIFY(0x00505380, FreeVerify, 0, s_unused);
EI_VERIFY(0x00509F40, FreeVerify, 0, s_unused);
EI_VERIFY(0x00509F60, FreeVerify, 0, s_unused);
EI_VERIFY(0x0050A090, FreeVerify, 0, s_unused);
EI_VERIFY(0x0050A3F0, FreeVerify, 0, s_unused);
EI_VERIFY(0x0050DAF0, FreeVerify, 0, s_unused);
EI_VERIFY(0x005199F0, FreeVerify, 0, s_unused);
EI_VERIFY(0x00519D10, FreeVerify, 0, s_unused);
EI_VERIFY(0x0051E5B0, FreeVerify, 0, s_unused);
EI_VERIFY(0x0051E8D0, FreeVerify, 0, s_unused);
EI_VERIFY(0x0051E8F0, FreeVerify, 0, s_unused);
EI_VERIFY(0x0051E910, FreeVerify, 0, s_unused);
EI_VERIFY(0x0051E930, FreeVerify, 0, s_unused);
EI_VERIFY(0x0051E950, FreeVerify, 0, s_unused);
EI_VERIFY(0x0050E100, AllocDwordsVerify, 0, s_unused);
EI_VERIFY(0x00514D30, AllocDwordsVerify, 0, s_unused);
EI_VERIFY(0x0051A170, AllocDwordsVerify, 0, s_unused);
EI_VERIFY(0x0051EF90, AllocDwordsVerify, 0, s_unused);
EI_VERIFY(0x0051EA10, SetSize4Verify, 0, s_unused);
EI_VERIFY(0x0051EB20, SetSize4Verify, 0, s_unused);
EI_VERIFY(0x0051EC30, SetSize4Verify, 0, s_unused);
EI_VERIFY(0x0051ED40, SetSize4Verify, 0, s_unused);
EI_VERIFY(0x0051EE50, SetAtGrowValueVerify, 0, s_unused);
