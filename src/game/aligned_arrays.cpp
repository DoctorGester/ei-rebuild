// The engine's arrays of plain elements in 32-byte aligned blocks (the block operator new returned
// is kept just before the data and freed through it), as instantiated around the animation and
// figure loaders (0044F1B0-00454A30): {data, size, capacity, growBy}. One template per operation;
// each instantiation is registered under its own name with its element size. The growth step is
// MFC's (growBy, else size / 8 clamped to 4 .. 1024). See docs/notes/objects.md ("Sixth round").
#include "ei/hook.h"
#include "lib/crt.h"

#include <cstdint>
#include <cstring>

namespace aligned {

struct Array {
    std::uint8_t* data;
    int size;
    int capacity;
    int growBy;
};
static_assert(sizeof(Array) == 0x10);

template <std::size_t K>
struct Blob {
    std::uint8_t bytes[K];
};

namespace {

// A block for `count` elements of K bytes: (count * K + 0x24) bytes from operator new, the data
// at the next 32-byte boundary after the stored block pointer.
template <std::size_t K>
std::uint8_t* Allocate(int count) {
    auto* block = static_cast<std::uint8_t*>(gamecrt::operator_new(static_cast<std::size_t>(count) * K + 0x24));
    auto* data = reinterpret_cast<std::uint8_t*>((reinterpret_cast<std::uintptr_t>(block) + 0x23) & ~std::uintptr_t{0x1F});
    reinterpret_cast<void**>(data)[-1] = block;
    return data;
}
void Release(std::uint8_t* data) { gamecrt::operator_delete(reinterpret_cast<void**>(data)[-1]); }

int GrowStep(const Array& a) {
    if (a.growBy != 0) return a.growBy;
    int grow = a.size / 8;
    return grow < 4 ? 4 : grow > 0x400 ? 0x400 : grow;
}

// SetSize(n) without the growth argument: 0 frees the block, growing copies the elements.
template <std::size_t K>
void Resize(Array& a, int n) {
    if (n == 0) {
        if (a.data) {
            Release(a.data);
            a.data = nullptr;
        }
        a.capacity = 0;
        a.size = 0;
    } else if (!a.data) {
        a.data = Allocate<K>(n);
        a.capacity = n;
        a.size = n;
    } else if (n <= a.capacity) {
        a.size = n;
    } else {
        int capacity = a.capacity + GrowStep(a);
        if (n >= capacity) capacity = n;
        std::uint8_t* data = Allocate<K>(capacity);
        std::memcpy(data, a.data, static_cast<std::size_t>(a.size) * K);
        Release(a.data);
        a.data = data;
        a.size = n;
        a.capacity = capacity;
    }
}

template <std::size_t K>
void SetSizeT(Array* a, int n, int growBy) {
    if (growBy != -1) a->growBy = growBy;
    Resize<K>(*a, n);
}

} // namespace

// ---- SetSize(n, growBy) ------------------------------------------------------------------------
void __fastcall ShortArraySetSize(Array* a, void*, int n, int growBy) { SetSizeT<2>(a, n, growBy); }
EI_REPLACE(0x0044FA90, ShortArraySetSize);
void __fastcall DwordArraySetSize(Array* a, void*, int n, int growBy) { SetSizeT<4>(a, n, growBy); }
EI_REPLACE(0x0044F540, DwordArraySetSize);
void __fastcall DwordArraySetSize2(Array* a, void*, int n, int growBy) { SetSizeT<4>(a, n, growBy); }
EI_REPLACE(0x004513A0, DwordArraySetSize2);
void __fastcall DwordArraySetSize3(Array* a, void*, int n, int growBy) { SetSizeT<4>(a, n, growBy); }
EI_REPLACE(0x00452EB0, DwordArraySetSize3);
void __fastcall QwordArraySetSize(Array* a, void*, int n, int growBy) { SetSizeT<8>(a, n, growBy); }
EI_REPLACE(0x00453100, QwordArraySetSize);
void __fastcall Vector3ArraySetSize(Array* a, void*, int n, int growBy) { SetSizeT<12>(a, n, growBy); }
EI_REPLACE(0x0044F1D0, Vector3ArraySetSize);
void __fastcall MatrixArraySetSize(Array* a, void*, int n, int growBy) { SetSizeT<64>(a, n, growBy); }
EI_REPLACE(0x0044F960, MatrixArraySetSize);

// ---- Add(value) (the new element's index) ------------------------------------------------------
int __fastcall QwordArrayAdd(Array* a, void*, Blob<8> value) {
    int index = a->size;
    Resize<8>(*a, index + 1);
    std::memcpy(a->data + index * 8, &value, 8);
    return index;
}
EI_REPLACE(0x0044FBB0, QwordArrayAdd);
int __fastcall Vector3ArrayAdd(Array* a, void*, Blob<12> value) {
    int index = a->size;
    Resize<12>(*a, index + 1);
    std::memcpy(a->data + index * 12, &value, 12);
    return index;
}
EI_REPLACE(0x0044F2E0, Vector3ArrayAdd);

// ---- SetAtGrow(index, value) -------------------------------------------------------------------
void __fastcall ShortArraySetAtGrow(Array* a, void*, int index, std::uint16_t value) {
    if (index >= a->size) Resize<2>(*a, index + 1);
    std::memcpy(a->data + index * 2, &value, 2);
}
EI_REPLACE(0x00452FC0, ShortArraySetAtGrow);
void __fastcall DwordArraySetAtGrowRef(Array* a, void*, int index, const std::uint32_t* value) {
    if (index >= a->size) Resize<4>(*a, index + 1);
    std::memcpy(a->data + index * 4, value, 4);
}
EI_REPLACE(0x00452C40, DwordArraySetAtGrowRef);
void __fastcall DwordArraySetAtGrow(Array* a, void*, int index, std::uint32_t value) {
    if (index >= a->size) Resize<4>(*a, index + 1);
    std::memcpy(a->data + index * 4, &value, 4);
}
EI_REPLACE(0x004538D0, DwordArraySetAtGrow);
void __fastcall Vector3ArraySetAtGrow(Array* a, void*, int index, Blob<12> value) {
    if (index >= a->size) Resize<12>(*a, index + 1);
    std::memcpy(a->data + index * 12, &value, 12);
}
EI_REPLACE(0x00452AF0, Vector3ArraySetAtGrow);

// ---- operator=(other) --------------------------------------------------------------------------
void __fastcall Vector3ArrayCopy(Array* a, void*, const Array* other) {
    Resize<12>(*a, other->size);
    std::memcpy(a->data, other->data, static_cast<std::size_t>(other->size) * 12);
}
EI_REPLACE(0x0044F410, Vector3ArrayCopy);

// ---- Allocate(count) (the aligned data pointer) -------------------------------------------------
std::uint8_t* __fastcall AllocateDwords(int count) { return Allocate<4>(count); }
EI_REPLACE(0x00453A10, AllocateDwords);
std::uint8_t* __fastcall AllocateWords6(int count) { return Allocate<6>(count); }
EI_REPLACE(0x00452E90, AllocateWords6);
std::uint8_t* __fastcall AllocateQwords(int count) { return Allocate<8>(count); }
EI_REPLACE(0x004545A0, AllocateQwords);
std::uint8_t* __fastcall AllocateVectors(int count) { return Allocate<12>(count); }
EI_REPLACE(0x00452C20, AllocateVectors);
std::uint8_t* __fastcall AllocateBlocks32(int count) { return Allocate<32>(count); }
EI_REPLACE(0x00452DA0, AllocateBlocks32);
std::uint8_t* __fastcall AllocateBlocks40(int count) { return Allocate<40>(count); }
EI_REPLACE(0x00452D80, AllocateBlocks40);
std::uint8_t* __fastcall AllocateMatrices(int count) { return Allocate<64>(count); }
EI_REPLACE(0x00452E70, AllocateMatrices);

// ---- Destructors: the block freed (the pointer is left as it was) -------------------------------
namespace {
void FreeAt(void* self, std::size_t offset) {
    std::uint8_t* data = *reinterpret_cast<std::uint8_t**>(static_cast<std::uint8_t*>(self) + offset);
    if (data) Release(data);
}
} // namespace
void __fastcall ArrayFree1(void* self) { FreeAt(self, 0); }
EI_REPLACE(0x0044F1B0, ArrayFree1);
void __fastcall ArrayFree2(void* self) { FreeAt(self, 0); }
EI_REPLACE(0x0044FA70, ArrayFree2);
void __fastcall ArrayFree3(void* self) { FreeAt(self, 0); }
EI_REPLACE(0x0044FB90, ArrayFree3);
void __fastcall ArrayFree4(void* self) { FreeAt(self, 0); }
EI_REPLACE(0x00451380, ArrayFree4);
void __fastcall ArrayFree5(void* self) { FreeAt(self, 0); }
EI_REPLACE(0x004534A0, ArrayFree5);
void __fastcall ArrayFree6(void* self) { FreeAt(self, 0); }
EI_REPLACE(0x004549D0, ArrayFree6);
void __fastcall ArrayFree7(void* self) { FreeAt(self, 0); }
EI_REPLACE(0x004549F0, ArrayFree7);
void __fastcall ArrayFree8(void* self) { FreeAt(self, 0); }
EI_REPLACE(0x00454A10, ArrayFree8);
void __fastcall ArrayFree9(void* self) { FreeAt(self, 0); }
EI_REPLACE(0x00454A30, ArrayFree9);
// A record holding such an array at +8 / +4.
void __fastcall ArrayAt8Free(void* self) { FreeAt(self, 8); }
EI_REPLACE(0x00454380, ArrayAt8Free);
void __fastcall ArrayAt4Free(void* self) { FreeAt(self, 4); }
EI_REPLACE(0x004543C0, ArrayAt4Free);

} // namespace aligned
