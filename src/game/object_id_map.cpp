// ObjectIdMap::ReleaseAll (0047B670; world.h; A-E1): the world's object map when a map closes
// (world_map.cpp) and the player's two maps of objects sent to the client (player.cpp), which have
// the same shape. Main thread. See docs/notes/core.md.
#include "world.h"

#include "ei/hook.h"

void ObjectIdMap::ReleaseAll() {
    for (auto* node = map.First(); node && node->value; node = map.After(node)) node->value->Release();
    map.Clear();
    maxId = 0;
}
EI_REPLACE_METHOD(0x0047B670, ObjectIdMap::ReleaseAll);
