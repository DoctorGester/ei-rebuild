// The object sets a player is sent (the server's SyncPlayer, world_sync.cpp): the set collected
// this frame is sorted (duplicates dropped) and compared with the set sent
// before (0047B1F0), giving the objects new to the player (referenced once more), the objects gone
// (released through slot 4) and the objects still there; the sent set then becomes the new one.
// The arrays are the original's aligned pointer arrays ({data, size, capacity, growBy}, blocks
// from 0047B940) and their two SetSize variants. See docs/notes/world.md. The game orders the sets
// by address; ours by object id (address_order.h, T2f).
#include "address_order.h"
#include "core_streams.h"
#include "engine_object.h"

#include "ei/hook.h"
#include "lib/containers.h"
#include "lib/crt.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include "game_data.h"

// {data, size, capacity, growBy} of world objects; the game compares and sorts them by address.
using ObjectSet = lib::AlignedArray<CEngineObject*>;
static_assert(sizeof(ObjectSet) == 0x10);

bool address_order::ObjectBefore(const CEngineObject* a, const CEngineObject* b) {
    int ia = a->id_;
    int ib = b->id_;
    if (ia != ib) return ia < ib;
    return std::less<const CEngineObject*>()(a, b);   // the address (the original's unsigned compare)
}

EI_REWRITTEN(0x0047B770, ObjectSet::SetSize);
// 0047B880 (RET 8): the sort's SetSize(n, growBy): no special case for 0 (an empty block is
// allocated), and growing does not keep the elements.
EI_REPLACE_METHOD(0x0047B880, ObjectSet::SetSizeDiscardStep);

// 0047ADF0: sorts the set and drops the duplicates. The original sorts by address (four byte passes of
// a radix sort, 0047B120, through a scratch array freed at exit by 0047B1D0); ours by id
// (address_order::ObjectBefore), a comparison sort without the scratch array.
void __fastcall ObjectSetSortUnique(ObjectSet* self) {
    if (self->size < 2) return;
    std::sort(self->data, self->data + self->size, address_order::ObjectBefore);
    // Each element compared with the one before it in place, as the original does.
    CEngineObject** end = self->data + self->size;
    CEngineObject** out = self->data + 1;
    for (CEngineObject** p = self->data + 1; p < end; ++p) {
        CEngineObject* value = *p;
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

    CEngineObject* const* s = sent->data;
    CEngineObject* const* c = current->data;
    int sentLeft = sent->size;
    int currentLeft = current->size;
    int addedCount = 0, goneCount = 0, sameCount = 0;
    bool changed = false;
    while (sentLeft != 0 && currentLeft != 0) {
        CEngineObject* old = *s;
        CEngineObject* now = *c;
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
            old->Release();
            --sentLeft;
        } else {
            added->data[addedCount++] = now;
            ++c;
            ++now->refCount_;
            --currentLeft;
        }
    }
    if (changed || sentLeft != 0 || currentLeft != 0) {
        for (; sentLeft != 0; --sentLeft) {
            gone->data[goneCount++] = *s++;
            s[-1]->Release();
        }
        for (; currentLeft != 0; --currentLeft) {
            added->data[addedCount++] = *c++;
            ++c[-1]->refCount_;
        }
        sent->SetSize(current->size, -1);
        if (current->size != 0) std::memcpy(sent->data, current->data, static_cast<std::size_t>(current->size) * 4);
    }
    added->SetSize(addedCount, -1);
    gone->SetSize(goneCount, -1);
    same->SetSize(sameCount, -1);
}
EI_REWRITTEN(0x0047B1F0, DiffObjectSets);
