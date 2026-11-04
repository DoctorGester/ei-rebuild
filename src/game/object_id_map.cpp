// The world's timed objects by id (EffectIdMap, world.h). Main thread. See docs/notes/core.md.
#include "world.h"

void EffectIdMap::Set(const unsigned& key, Effect* const& value) {
    if (static_cast<int>(key) > maxId) maxId = static_cast<int>(key);
    map[key] = value;
}
