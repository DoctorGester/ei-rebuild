// Checks for the world's manager creation (src/game/world_create.cpp): ours only (it allocates
// and attaches the managers); afterwards every manager slot is set.
#include "verify.h"

#include "../../src/game/world.h"

#include <cstring>

namespace {
void* s_unused;
}

static int __fastcall WorldCreateManagersVerify(CWorld* world) {
    int result = WorldCreateManagers(world);
    bool ok = result == 1;
    for (const void* manager : {static_cast<const void*>(world->players_), static_cast<const void*>(world->manager3C_),
                                static_cast<const void*>(world->manager40_), static_cast<const void*>(world->manager48_),
                                static_cast<const void*>(world->manager4C_), static_cast<const void*>(world->manager50_),
                                static_cast<const void*>(world->itemManager_), static_cast<const void*>(world->manager58_),
                                static_cast<const void*>(world->shared5C_.ptr)})
        if (!manager) ok = false;
    ei::verify::Check("World::CreateManagers", true, ok, "world %p", world);
    return result;
}
EI_VERIFY(0x0055EE40, WorldCreateManagersVerify, 0, s_unused);

// A-W13. 0055EAB0 (the base constructor) only stores fields: both versions run on scratch blocks
// that start with the same bytes, and the blocks are compared. 7 stolen bytes: MOV EAX,ECX;
// XOR ECX,ECX; MOV [EAX+4],ECX. Its callers are the two worlds' static-initialiser constructors,
// so it runs before the first frame.
static CWorld*(__fastcall* s_constructBase)(CWorld*);
static CWorld* __fastcall ConstructBaseVerify(CWorld* self) {
    alignas(4) unsigned char expected[0x84], actual[0x84];
    std::memset(expected, 0xA5, sizeof(expected));
    std::memset(actual, 0xA5, sizeof(actual));
    auto* a = reinterpret_cast<CWorld*>(expected);
    auto* b = reinterpret_cast<CWorld*>(actual);
    bool same = s_constructBase(a) == a && b->ConstructBase() == b && std::memcmp(expected, actual, sizeof(expected)) == 0;
    ei::verify::Check("CWorld::ConstructBase", true, same, "");
    return self->ConstructBase();
}
EI_VERIFY(0x0055EAB0, ConstructBaseVerify, 7, s_constructBase);

// 0055FB00 FindUnit: MOV EDX,[ESP+4]; MOV EAX,[ECX] (6 bytes). Pure. The scripts' and the
// interface's unit lookups (`newgame`).
static CEngineObject*(__fastcall* s_findUnit)(CWorld*, void*, int);
static CEngineObject* __fastcall FindUnitVerify(CWorld* self, void*, int id) {
    CEngineObject* expected = s_findUnit(self, nullptr, id);
    CEngineObject* actual = self->FindUnit(id);
    ei::verify::Check("CWorld::FindUnit", expected, actual, "id %d", id);
    return actual;
}
EI_VERIFY(0x0055FB00, FindUnitVerify, 6, s_findUnit);

// 0055FAE0 FindMapObject: MOV EAX,[ESP+4]; MOV EDX,[ECX] (6 bytes). Pure.
static CEngineObject*(__fastcall* s_findMapObject)(CWorld*, void*, int);
static CEngineObject* __fastcall FindMapObjectVerify(CWorld* self, void*, int id) {
    CEngineObject* expected = s_findMapObject(self, nullptr, id);
    CEngineObject* actual = self->FindMapObject(id);
    ei::verify::Check("CWorld::FindMapObject", expected, actual, "id %d", id);
    return actual;
}
EI_VERIFY(0x0055FAE0, FindMapObjectVerify, 6, s_findMapObject);
