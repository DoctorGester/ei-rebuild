// The object sets a player is sent (the server's SyncPlayer, world_sync.cpp): the set collected this frame
// is sorted (duplicates dropped) and compared with the set sent before, giving the objects new to the
// player (referenced once more), the objects gone (released by a virtual call) and the objects still there;
// the sent set then becomes the new one. The arrays are the game's aligned pointer arrays ({data, size,
// capacity, growBy}) and their two SetSize variants. See docs/notes/world.md. The game orders the sets by
// address; this one by object id (address_order.h).
#include "address_order.h"
#include "core_streams.h"
#include "engine_object.h"

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
    return std::less<const CEngineObject*>()(a, b);   // the address (an unsigned compare in the game)
}

// Sorts the set and drops the duplicates. The game sorts by address (four byte passes of a radix sort,
// through a scratch array freed at exit); this sorts by id (address_order::ObjectBefore), a comparison sort
// without the scratch array.
void ObjectSetSortUnique(ObjectSet* self) {
    if (self->size < 2) return;
    std::sort(self->data, self->data + self->size, address_order::ObjectBefore);
    // Each element compared with the one before it in place.
    CEngineObject** end = self->data + self->size;
    CEngineObject** out = self->data + 1;
    for (CEngineObject** p = self->data + 1; p < end; ++p) {
        CEngineObject* value = *p;
        if (value != p[-1]) *out++ = value;
    }
    self->SetSizeDiscardStep(static_cast<int>(out - self->data), -1);
}

// `current` is sorted first; `sent` (sorted by an earlier call) is walked beside it. Objects only in
// `current` go to `added` (referenced once more, +4), objects only in `sent` to `gone` (released by a
// virtual call), objects in both to `same`. When anything differs `sent` becomes a copy of `current`.
void DiffObjectSets(ObjectSet* sent, ObjectSet* current, ObjectSet* added, ObjectSet* gone, ObjectSet* same) {
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
