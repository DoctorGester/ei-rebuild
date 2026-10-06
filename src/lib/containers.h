#pragma once

// The engine's own containers (not MFC's): the growable arrays in 32-byte aligned blocks that the
// renderer, the figure and animation loaders and the interface use. MFC's CArray / CList /
// CTypedPtrList are in mfc.h, STLport's string / vector / hash_map in stlp.h. Which original
// instantiation each address belongs to: docs/notes/core.md, "Shared containers".

#include "crt.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace lib {

// A block for `bytes` bytes of elements: (bytes + 0x24) from operator new, the data at the next
// 32-byte boundary after the stored block pointer. The game has one copy per element size
// (00406B30 / 00406B50, 00452C20, 00452D80, 00452DA0, 00452E70, 00476EF0, 00477250, 00600A80,
// 004F65B0, ...); they differ only in the multiplication.
inline void* AllocAligned32(std::size_t bytes) {
    auto* block = static_cast<char*>(gamecrt::operator_new(bytes + 0x24));
    auto* data = reinterpret_cast<char*>((reinterpret_cast<std::uintptr_t>(block) + 0x23) & ~std::uintptr_t{0x1F});
    reinterpret_cast<void**>(data)[-1] = block;
    return data;
}
// Frees a block from AllocAligned32 through the pointer stored before the data.
inline void FreeAligned32(void* data) { gamecrt::operator_delete(static_cast<void**>(data)[-1]); }

// The engine's aligned array (0x10 bytes): data, size, capacity, growth step. CArray's growth
// rules without the vtable, the element construction or the destruction: new slots keep whatever
// the block held. Instantiated per element type all over the engine (the animation and figure
// loaders 0044F1B0-00454A30 and 005A6E70, the interface's pointer arrays 006003D0-00600950,
// the renderer's work buffers 007AEDE0-007AF040, ...).
template <typename T>
struct AlignedArray {
    T* data;        // +0x00
    int size;       // +0x04
    int capacity;   // +0x08
    int growBy;     // +0x0C 0 = size / 8 clamped to 4..1024

    T& operator[](int index) { return data[index]; }
    const T& operator[](int index) const { return data[index]; }

    static T* Allocate(int count) {
        return static_cast<T*>(AllocAligned32(static_cast<std::size_t>(count * static_cast<int>(sizeof(T)))));
    }
    static void Free(T* p) { FreeAligned32(p); }

    // The step a reallocation adds: growBy, else size / 8 clamped to 4..1024.
    int GrowStep() const {
        if (growBy != 0) return growBy;
        int step = size / 8;
        return step < 4 ? 4 : step > 0x400 ? 0x400 : step;
    }

    // SetSize(count, growBy): 0 frees the block; the first allocation is exact; growing past the
    // capacity reallocates to max(count, capacity + step) and copies the old elements with memcpy.
    // growBy -1 keeps the step. The out-of-line copies (RET 8) are registered on this member.
    void SetSize(int count, int newGrowBy = -1) {
        if (newGrowBy != -1) growBy = newGrowBy;
        if (count == 0) {
            if (data) {
                Free(data);
                data = nullptr;
            }
            capacity = 0;
            size = 0;
            return;
        }
        if (!data) {
            data = Allocate(count);
            capacity = count;
            size = count;
            return;
        }
        if (count <= capacity) {
            size = count;
            return;
        }
        int newCapacity = GrowStep() + capacity;
        if (count >= newCapacity) newCapacity = count;
        T* fresh = Allocate(newCapacity);
        std::memcpy(static_cast<void*>(fresh), data, static_cast<std::size_t>(size) * sizeof(T));
        Free(data);
        data = fresh;
        size = count;
        capacity = newCapacity;
    }

    // The renderer's work buffers inline a SetSize that does not keep the contents: no 0 branch,
    // and growing past the capacity frees the old block without copying it.
    void SetSizeDiscard(int count) {
        if (!data) {
            data = Allocate(count);
            capacity = count;
            size = count;
            return;
        }
        if (count <= capacity) {
            size = count;
            return;
        }
        int newCapacity = GrowStep() + capacity;
        if (count >= newCapacity) newCapacity = count;
        T* fresh = Allocate(newCapacity);
        Free(data);
        data = fresh;
        size = count;
        capacity = newCapacity;
    }

    void SetAtGrow(int index, const T& value) {
        if (index >= size) SetSize(index + 1);
        data[index] = value;
    }

    // Add (inlined at its call sites): SetSize(size + 1), then the new element; its index.
    int Add(const T& value) {
        int index = size;
        SetSize(index + 1);
        data[index] = value;
        return index;
    }

    // RemoveAt (inlined): moves the following elements down one by one (REP MOVSD); the storage stays.
    void RemoveAt(int index) {
        int count = size - index - 1;
        for (int i = 0; i < count; ++i) data[index + i] = data[index + i + 1];
        --size;
    }

    // SetSize(0) inlined: frees the block and empties the array.
    void RemoveAll() {
        if (data) {
            Free(data);
            data = nullptr;
        }
        capacity = 0;
        size = 0;
    }

    // The destructors (0044F1B0, ...): the block freed, the fields left as they were.
    void FreeData() {
        if (data) Free(data);
    }
};
static_assert(sizeof(AlignedArray<int>) == 0x10);

} // namespace lib
