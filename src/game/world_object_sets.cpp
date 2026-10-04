// The object sets a player is sent (the server's SyncPlayer, world_sync.cpp): the set collected
// this frame is sorted (a byte-wise radix sort, duplicates dropped) and compared with the set sent
// before (0047B1F0), giving the objects new to the player (referenced once more), the objects gone
// (released through slot 4) and the objects still there; the sent set then becomes the new one.
// The arrays are the original's aligned pointer arrays ({data, size, capacity, growBy}, blocks
// from 0047B940) and their two SetSize variants. See docs/notes/world.md ("Object sets").
#include "core_streams.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"

#include <cstdint>
#include <cstring>

// {data, size, capacity, growBy}; the elements are compared as unsigned words (the objects'
// addresses).
struct ObjectSet {
    std::uint32_t* data;
    int size;
    int capacity;
    int growBy;
};
static_assert(sizeof(ObjectSet) == 0x10);

namespace {

std::uint32_t* AllocWords(int count) {
    return reinterpret_cast<std::uint32_t*>(AlignedPointerArrayAlloc(count));
}
void FreeWords(std::uint32_t* data) { gamecrt::operator_delete(reinterpret_cast<void**>(data)[-1]); }

int GrowStep(const ObjectSet& a) {
    if (a.growBy != 0) return a.growBy;
    int grow = a.size / 8;
    return grow < 4 ? 4 : grow > 0x400 ? 0x400 : grow;
}

// SetSize(n) as the callers inline it (the growth step kept as it is).
void InlineSetSize(ObjectSet& a, int n) {
    if (n == 0) {
        if (a.data) {
            FreeWords(a.data);
            a.data = nullptr;
        }
        a.capacity = 0;
        a.size = 0;
    } else if (!a.data) {
        a.data = AllocWords(n);
        a.capacity = n;
        a.size = n;
    } else if (n <= a.capacity) {
        a.size = n;
    } else {
        int capacity = a.capacity + GrowStep(a);
        if (n >= capacity) capacity = n;
        std::uint32_t* fresh = AllocWords(capacity);
        std::memcpy(fresh, a.data, static_cast<std::size_t>(a.size) * 4);
        FreeWords(a.data);
        a.data = fresh;
        a.size = n;
        a.capacity = capacity;
    }
}

// The sort's scratch array (a function static at 007A9738, guard bit 1 of 007A9748, freed at exit
// by 0047B1D0).
ObjectSet& Scratch() { return *reinterpret_cast<ObjectSet*>(0x007A9738); }
std::uint8_t& ScratchGuard() { return *reinterpret_cast<std::uint8_t*>(0x007A9748); }
__declspec(naked) int __cdecl GameAtExit(void (__cdecl*)()) EI_ORIGINAL(0x006E727B)

// One counting pass by the byte at `shift`: `src` scattered into `dst` (stable).
void CountingPass(int shift, const ObjectSet& src, ObjectSet& dst) {
    int counts[256] = {};
    for (int i = 0; i < src.size; ++i) ++counts[(src.data[i] >> shift) & 0xFF];
    std::uint32_t* next[256];
    std::uint32_t* at = dst.data;
    for (int b = 0; b < 256; ++b) {
        next[b] = at;
        at += counts[b];
    }
    for (int i = 0; i < src.size; ++i) {
        std::uint32_t value = src.data[i];
        *next[(value >> shift) & 0xFF]++ = value;
    }
}

} // namespace

// 0047B770 (RET 8): SetSize(n, growBy): 0 frees the block; growing copies the elements.
void __fastcall ObjectSetSetSize(ObjectSet* self, void*, int n, int growBy) {
    if (growBy != -1) self->growBy = growBy;
    InlineSetSize(*self, n);
}
EI_REPLACE(0x0047B770, ObjectSetSetSize);

// 0047B880 (RET 8): the sort's SetSize(n, growBy): no special case for 0 (an empty block is
// allocated), and growing does not keep the elements.
void __fastcall ObjectSetResize(ObjectSet* self, void*, int n, int growBy) {
    if (growBy != -1) self->growBy = growBy;
    if (!self->data) {
        self->data = AllocWords(n);
        self->capacity = n;
        self->size = n;
    } else if (n <= self->capacity) {
        self->size = n;
    } else {
        int capacity = self->capacity + GrowStep(*self);
        if (n >= capacity) capacity = n;
        std::uint32_t* fresh = AllocWords(capacity);
        FreeWords(self->data);
        self->capacity = capacity;
        self->data = fresh;
        self->size = n;
    }
}
EI_REPLACE(0x0047B880, ObjectSetResize);

// 0047B120 (ECX shift, EDX source, RET 4): one counting pass of the radix sort.
void __fastcall ObjectSetRadixPass(int shift, ObjectSet* src, ObjectSet* dst) { CountingPass(shift, *src, *dst); }
EI_REPLACE(0x0047B120, ObjectSetRadixPass);

// 0047ADF0: sorts the set (four byte passes, low byte first, through the scratch array) and drops
// the duplicates.
void __fastcall ObjectSetSortUnique(ObjectSet* self) {
    if (self->size < 2) return;
    if (!(ScratchGuard() & 1)) {
        ScratchGuard() |= 1;
        ObjectSet& s = Scratch();
        s.data = nullptr;
        s.growBy = 0;
        s.capacity = 0;
        s.size = 0;
        GameAtExit(reinterpret_cast<void (__cdecl*)()>(0x0047B1D0));
    }
    ObjectSet& scratch = Scratch();
    InlineSetSize(scratch, self->size);
    CountingPass(0, *self, scratch);
    CountingPass(8, scratch, *self);
    CountingPass(16, *self, scratch);
    ObjectSetRadixPass(24, &scratch, self);
    // Each element compared with the one before it in place, as the original does.
    std::uint32_t* end = self->data + self->size;
    std::uint32_t* out = self->data + 1;
    for (std::uint32_t* p = self->data + 1; p < end; ++p) {
        std::uint32_t value = *p;
        if (value != p[-1]) *out++ = value;
    }
    ObjectSetResize(self, nullptr, static_cast<int>(out - self->data), -1);
}
EI_REPLACE(0x0047ADF0, ObjectSetSortUnique);

// 0047B1F0 (ECX sent, EDX current, RET 0xC). `current` is sorted first; `sent` (sorted by an
// earlier call) is walked beside it. Objects only in `current` go to `added` (referenced once
// more, +4), objects only in `sent` to `gone` (slot 4 called on them), objects in both to `same`.
// When anything differs `sent` becomes a copy of `current`.
void __fastcall DiffObjectSets(ObjectSet* sent, ObjectSet* current, ObjectSet* added, ObjectSet* gone, ObjectSet* same) {
    ObjectSetSortUnique(current);
    int most = current->size > sent->size ? current->size : sent->size;
    InlineSetSize(*added, most);
    InlineSetSize(*gone, most);
    InlineSetSize(*same, most);

    const std::uint32_t* s = sent->data;
    const std::uint32_t* c = current->data;
    int sentLeft = sent->size;
    int currentLeft = current->size;
    int addedCount = 0, goneCount = 0, sameCount = 0;
    bool changed = false;
    while (sentLeft != 0 && currentLeft != 0) {
        std::uint32_t old = *s;
        std::uint32_t now = *c;
        if (old == now) {
            same->data[sameCount++] = now;
            ++s;
            ++c;
            --sentLeft;
            --currentLeft;
            continue;
        }
        changed = true;
        if (old < now) {
            gone->data[goneCount++] = old;
            ++s;
            ei::VCall<void>(reinterpret_cast<void*>(old), 4);
            --sentLeft;
        } else {
            added->data[addedCount++] = now;
            ++c;
            ++*reinterpret_cast<int*>(now + 4);
            --currentLeft;
        }
    }
    if (changed || sentLeft != 0 || currentLeft != 0) {
        for (; sentLeft != 0; --sentLeft) {
            gone->data[goneCount++] = *s++;
            ei::VCall<void>(reinterpret_cast<void*>(s[-1]), 4);
        }
        for (; currentLeft != 0; --currentLeft) {
            added->data[addedCount++] = *c++;
            ++*reinterpret_cast<int*>(c[-1] + 4);
        }
        ObjectSetSetSize(sent, nullptr, current->size, -1);
        if (current->size != 0) std::memcpy(sent->data, current->data, static_cast<std::size_t>(current->size) * 4);
    }
    ObjectSetSetSize(added, nullptr, addedCount, -1);
    ObjectSetSetSize(gone, nullptr, goneCount, -1);
    ObjectSetSetSize(same, nullptr, sameCount, -1);
}
EI_REPLACE(0x0047B1F0, DiffObjectSets);
