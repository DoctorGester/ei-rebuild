// Checks for the aligned array copies registered on lib::AlignedArray in A-U10 (renderer_sprites.cpp,
// renderer_strip.cpp, figure_bone_lifetime.cpp, map_object_animation_bones.cpp's 0050DFF0 has its
// own): ours only (they allocate). SetSize leaves the size asked for and keeps the first elements;
// the discarding SetSize leaves the size; Add returns the old size and stores the value; blocks are
// 32-byte aligned. Reached by the sprite draw (particles, effects scenarios), the ribbon draw
// (lightning, trails) and every bone's reset (units).
#include "verify.h"

#include "../../src/lib/containers.h"

#include <cstdint>
#include <cstring>

namespace {

template <std::size_t K>
struct Blob {
    std::uint8_t bytes[K];
};

bool Aligned(const void* p) { return !p || (reinterpret_cast<std::uintptr_t>(p) & 0x1F) == 0; }

template <std::size_t K>
void CheckSetSize(const char* name, lib::AlignedArray<Blob<K>>* a, int n, int growBy) {
    constexpr int kMaxKept = 64;
    std::uint8_t before[kMaxKept * K];
    int kept = a->size < n ? a->size : n;
    if (kept > kMaxKept) kept = kMaxKept;
    if (kept > 0) std::memcpy(before, a->data, static_cast<std::size_t>(kept) * K);
    a->SetSize(n, growBy);
    bool ok = a->size == n && a->capacity >= n && Aligned(a->data) &&
              (kept <= 0 || std::memcmp(before, a->data, static_cast<std::size_t>(kept) * K) == 0);
    ei::verify::Check(name, true, ok, "size %d capacity %d", a->size, a->capacity);
}

template <std::size_t K>
void CheckSetSizeDiscard(const char* name, lib::AlignedArray<Blob<K>>* a, int n, int growBy) {
    a->SetSizeDiscardStep(n, growBy);
    bool ok = a->size == n && a->capacity >= n && a->data && Aligned(a->data);
    ei::verify::Check(name, true, ok, "size %d capacity %d", a->size, a->capacity);
}

void* s_unused32;
void* s_unused32Discard;
void* s_unused16;
void* s_unused4;
void* s_unusedAdd;
void* s_unused64Discard;

} // namespace

static void __fastcall SetSize32Verify(lib::AlignedArray<Blob<32>>* a, void*, int n, int growBy) {
    CheckSetSize("AlignedArray<TLVertex>::SetSize (00504F70)", a, n, growBy);
}
EI_VERIFY(0x00504F70, SetSize32Verify, 0, s_unused32);

static void __fastcall SetSize32DiscardVerify(lib::AlignedArray<Blob<32>>* a, void*, int n, int growBy) {
    CheckSetSizeDiscard("AlignedArray<TLVertex>::SetSizeDiscardStep (00505080)", a, n, growBy);
}
EI_VERIFY(0x00505080, SetSize32DiscardVerify, 0, s_unused32Discard);

static void __fastcall SetSize16Verify(lib::AlignedArray<Blob<16>>* a, void*, int n, int growBy) {
    CheckSetSize("AlignedArray<SpriteSortRecord>::SetSize (00505150)", a, n, growBy);
}
EI_VERIFY(0x00505150, SetSize16Verify, 0, s_unused16);

static void __fastcall SetSize4Verify(lib::AlignedArray<Blob<4>>* a, void*, int n, int growBy) {
    CheckSetSize("AlignedArray<SpriteSortRecord*>::SetSize (00505270)", a, n, growBy);
}
EI_VERIFY(0x00505270, SetSize4Verify, 0, s_unused4);

static int __fastcall AddVerify(lib::AlignedArray<std::uint16_t>* a, void*, std::uint16_t value) {
    int old = a->size;
    int index = a->Add(value);
    bool ok = index == old && a->size == old + 1 && a->data[index] == value && Aligned(a->data);
    ei::verify::Check("AlignedArray<uint16_t>::Add (00504E50)", true, ok, "index %d size %d", index, a->size);
    return index;
}
EI_VERIFY(0x00504E50, AddVerify, 0, s_unusedAdd);

static void __fastcall SetSize64DiscardVerify(lib::AlignedArray<Blob<64>>* a, void*, int n, int growBy) {
    CheckSetSizeDiscard("AlignedArray<Matrix4>::SetSizeDiscardStep (0050DD40)", a, n, growBy);
}
EI_VERIFY(0x0050DD40, SetSize64DiscardVerify, 0, s_unused64Discard);
