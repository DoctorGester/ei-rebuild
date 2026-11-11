// The object sets a player is sent (the server's SyncPlayer, world_sync.cpp): the set collected this frame
// is sorted (duplicates dropped) and compared with the set sent before, giving the objects new to the
// player (referenced once more), the objects gone (released by a virtual call) and the objects still there;
// the sent set then becomes the new one. The arrays are the game's aligned pointer arrays ({data, size,
// capacity, growBy}) and their two SetSize variants. See docs/notes/world.md. The game orders the sets by
// address; this one by object id (world/object_order.h).
#include "game/world/object_order.h"
#include "engine/core/core_streams.h"
#include "engine/core/engine_object.h"
#include "game/objects/map_object.h"
#include "game/effects/world_effect.h"
#include "lib/containers.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

bool object_order::ObjectBefore(const EngineObject* a, const EngineObject* b) {
    int ia = a->id;
    int ib = b->id;
    if (ia != ib) return ia < ib;
    return std::less<const EngineObject*>()(a, b);   // the address (an unsigned compare in the game)
}

// Sorts the set and drops the duplicates. The game sorts by address (four byte passes of a radix sort,
// through a scratch array freed at exit); this sorts by id (object_order::ObjectBefore), a comparison sort
// without the scratch array.
template <class T>
void ObjectSetSortUnique(std::vector<T*>* self) {
    if (std::ssize(*self) < 2) return;
    std::sort(self->data(), self->data() + std::ssize(*self), object_order::ObjectBefore);
    // Each element compared with the one before it in place.
    T** end = self->data() + std::ssize(*self);
    T** out = self->data() + 1;
    for (T** p = self->data() + 1; p < end; ++p) {
        T* value = *p;
        if (value != p[-1]) *out++ = value;
    }
    self->resize(static_cast<int>(out - self->data()));
}

// `current` is sorted first; `sent` (sorted by an earlier call) is walked beside it. Objects only in
// `current` go to `added` (referenced once more, +4), objects only in `sent` to `gone` (released by a
// virtual call), objects in both to `same`. When anything differs `sent` becomes a copy of `current`.
template <class T>
void DiffObjectSets(std::vector<T*>* sent, std::vector<T*>* current, std::vector<T*>* added, std::vector<T*>* gone,
                    std::vector<T*>* same) {
    ObjectSetSortUnique(current);
    int most = lib::Count(*current) > lib::Count(*sent) ? lib::Count(*current) : lib::Count(*sent);
    added->resize(most);
    gone->resize(most);
    same->resize(most);

    T* const* s = sent->data();
    T* const* c = current->data();
    int sentLeft = lib::Count(*sent);
    int currentLeft = lib::Count(*current);
    int addedCount = 0, goneCount = 0, sameCount = 0;
    bool changed = false;
    while (sentLeft != 0 && currentLeft != 0) {
        T* old = *s;
        T* now = *c;
        if (old == now) {
            (*same)[sameCount++] = now;
            ++s;
            ++c;
            --sentLeft;
            --currentLeft;
            continue;
        }
        changed = true;
        if (object_order::ObjectBefore(old, now)) {
            (*gone)[goneCount++] = old;
            ++s;
            old->Release();
            --sentLeft;
        } else {
            (*added)[addedCount++] = now;
            ++c;
            now->AddRef();
            --currentLeft;
        }
    }
    if (changed || sentLeft != 0 || currentLeft != 0) {
        for (; sentLeft != 0; --sentLeft) {
            (*gone)[goneCount++] = *s++;
            s[-1]->Release();
        }
        for (; currentLeft != 0; --currentLeft) {
            (*added)[addedCount++] = *c++;
            c[-1]->AddRef();
        }
        *sent = *current;
    }
    added->resize(addedCount);
    gone->resize(goneCount);
    same->resize(sameCount);
}

// The two passes of SyncPlayer: the map objects (with the script's party units), then the timed objects.
template void DiffObjectSets(std::vector<MapObject*>*, std::vector<MapObject*>*, std::vector<MapObject*>*,
                             std::vector<MapObject*>*, std::vector<MapObject*>*);
template void DiffObjectSets(std::vector<Effect*>*, std::vector<Effect*>*, std::vector<Effect*>*, std::vector<Effect*>*,
                             std::vector<Effect*>*);
