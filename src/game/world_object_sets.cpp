// The object sets a player is sent (the server's SyncPlayer, world_sync.cpp): the set collected
// this frame is sorted (a byte-wise radix sort, duplicates dropped) and compared with the set sent
// before (0047B1F0), giving the objects new to the player (referenced once more), the objects gone
// (released through slot 4) and the objects still there; the sent set then becomes the new one.
// The arrays are the original's aligned pointer arrays ({data, size, capacity, growBy}, blocks
// from 0047B940) and their two SetSize variants. See docs/notes/world.md. The game orders the sets
// by address; harness runs and the standalone by object id (address_order.h, T2f).
#include "address_order.h"
#include "core_streams.h"
#include "engine_object.h"

#include "ei/hook.h"
#include "lib/containers.h"
#include "lib/crt.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include "originals_library.h"

// {data, size, capacity, growBy}; the elements are compared as unsigned words (the objects'
// addresses).
using ObjectSet = lib::AlignedArray<std::uint32_t>;
static_assert(sizeof(ObjectSet) == 0x10);

namespace {

// The objects behind the words (world objects: CEngineObject).
CEngineObject* ObjectOf(std::uint32_t word) { return reinterpret_cast<CEngineObject*>(word); }

// The sort's scratch array (a function static at 007A9738, guard bit 1 of 007A9748, freed at exit
// by 0047B1D0).
ObjectSet& Scratch() { return *reinterpret_cast<ObjectSet*>(0x007A9738); }
std::uint8_t& ScratchGuard() { return *reinterpret_cast<std::uint8_t*>(0x007A9748); }

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

bool g_byId = false;   // address_order::UseIds

} // namespace

void address_order::UseIds() { g_byId = true; }
bool address_order::ById() { return g_byId || ei::Standalone(); }

bool address_order::ObjectBefore(std::uint32_t a, std::uint32_t b) {
    if (ById()) {
        int ia = ObjectOf(a)->id_;
        int ib = ObjectOf(b)->id_;
        if (ia != ib) return ia < ib;
    }
    return a < b;
}

// 0047B770 (RET 8): SetSize(n, growBy): 0 frees the block; growing copies the elements.
EI_REPLACE_METHOD(0x0047B770, ObjectSet::SetSize);

// 0047B880 (RET 8): the sort's SetSize(n, growBy): no special case for 0 (an empty block is
// allocated), and growing does not keep the elements.
EI_REPLACE_METHOD(0x0047B880, ObjectSet::SetSizeDiscardStep);

// 0047B120 (ECX shift, EDX source, RET 4): one counting pass of the radix sort.
void __fastcall ObjectSetRadixPass(int shift, ObjectSet* src, ObjectSet* dst) { CountingPass(shift, *src, *dst); }
EI_REPLACE(0x0047B120, ObjectSetRadixPass);

// 0047ADF0: sorts the set (four byte passes, low byte first, through the scratch array) and drops
// the duplicates. By id (address_order::ById) a comparison sort, without the scratch array.
void __fastcall ObjectSetSortUnique(ObjectSet* self) {
    if (self->size < 2) return;
    if (address_order::ById()) {
        std::sort(self->data, self->data + self->size, address_order::ObjectBefore);
    } else {
        if (!(ScratchGuard() & 1)) {
            ScratchGuard() |= 1;
            Scratch().Construct();
            GameAtExit(reinterpret_cast<void (__cdecl*)()>(0x0047B1D0));
        }
        ObjectSet& scratch = Scratch();
        scratch.SetSize(self->size);
        CountingPass(0, *self, scratch);
        CountingPass(8, scratch, *self);
        CountingPass(16, *self, scratch);
        ObjectSetRadixPass(24, &scratch, self);
    }
    // Each element compared with the one before it in place, as the original does.
    std::uint32_t* end = self->data + self->size;
    std::uint32_t* out = self->data + 1;
    for (std::uint32_t* p = self->data + 1; p < end; ++p) {
        std::uint32_t value = *p;
        if (value != p[-1]) *out++ = value;
    }
    self->SetSizeDiscardStep(static_cast<int>(out - self->data), -1);
}
EI_REPLACE(0x0047ADF0, ObjectSetSortUnique);

// 0047B1F0 (ECX sent, EDX current, RET 0xC). `current` is sorted first; `sent` (sorted by an
// earlier call) is walked beside it. Objects only in `current` go to `added` (referenced once
// more, +4), objects only in `sent` to `gone` (slot 4 called on them), objects in both to `same`.
// When anything differs `sent` becomes a copy of `current`.
void __fastcall DiffObjectSets(ObjectSet* sent, ObjectSet* current, ObjectSet* added, ObjectSet* gone, ObjectSet* same) {
    ObjectSetSortUnique(current);
    int most = current->size > sent->size ? current->size : sent->size;
    added->SetSize(most);
    gone->SetSize(most);
    same->SetSize(most);

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
        if (address_order::ObjectBefore(old, now)) {
            gone->data[goneCount++] = old;
            ++s;
            ObjectOf(old)->Release();
            --sentLeft;
        } else {
            added->data[addedCount++] = now;
            ++c;
            ++ObjectOf(now)->refCount_;
            --currentLeft;
        }
    }
    if (changed || sentLeft != 0 || currentLeft != 0) {
        for (; sentLeft != 0; --sentLeft) {
            gone->data[goneCount++] = *s++;
            ObjectOf(s[-1])->Release();
        }
        for (; currentLeft != 0; --currentLeft) {
            added->data[addedCount++] = *c++;
            ++ObjectOf(c[-1])->refCount_;
        }
        sent->SetSize(current->size, -1);
        if (current->size != 0) std::memcpy(sent->data, current->data, static_cast<std::size_t>(current->size) * 4);
    }
    added->SetSize(addedCount, -1);
    gone->SetSize(goneCount, -1);
    same->SetSize(sameCount, -1);
}
EI_REPLACE(0x0047B1F0, DiffObjectSets);
