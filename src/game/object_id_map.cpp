// The world's object map by id (ObjectIdMap, world.h): the world's map and the player's two maps of objects sent to
// the client (player.cpp) have the same shape. Main thread. See docs/notes/core.md.
#include "world.h"

void ObjectIdMap::Set(const unsigned& key, EngineObject* const& value) {
    if (static_cast<int>(key) > maxId) maxId = static_cast<int>(key);
    map[key] = value;
}
