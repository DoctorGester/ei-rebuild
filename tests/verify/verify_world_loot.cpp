// Checks for CWorldServer::Loot (src/game/world_loot.cpp). Looting moves items into a player's
// inventory, so only ours runs; afterwards a shared loot record of the object is gone and the
// player's gold did not decrease.
#include "verify.h"

#include "../../src/game/world.h"

namespace {
void* s_unused;

bool OnSharedList(CWorldServer* world, int id) {
    struct Node {
        Node* next;
        Node* prev;
        int objectId;
    };
    for (auto* node = static_cast<Node*>(world->sharedObjects_); node; node = node->next)
        if (node->objectId == id) return true;
    return false;
}
} // namespace

// 005942D0.
static bool __fastcall LootVerify(CWorldServer* self, void*, CEngineObject* object, WorldPlayer* player) {
    int id = object ? object->id_ : 0;
    bool shared = object && (object->flags_ & 0x20000000u) && OnSharedList(self, id);
    int goldBefore = player ? *reinterpret_cast<int*>(reinterpret_cast<std::uint8_t*>(player) + 0xC0) : 0;
    bool result = self->Loot(object, player);
    int goldAfter = player ? *reinterpret_cast<int*>(reinterpret_cast<std::uint8_t*>(player) + 0xC0) : 0;
    bool ok = goldAfter >= goldBefore;
    if (shared && result) ok = ok && !OnSharedList(self, id);
    ei::verify::Check("CWorldServer::Loot", true, ok, "object %d shared %d -> %d, gold %d -> %d", id, shared, result,
                      goldBefore, goldAfter);
    return result;
}
EI_VERIFY(0x005942D0, LootVerify, 0, s_unused);
