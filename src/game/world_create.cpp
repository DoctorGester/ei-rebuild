// 0055EE40: the world's managers created (the server's and the client's world both call it, and
// it is a virtual slot of their base, vtable entry 0073F194). See docs/notes/combat.md ("Fourth
// round"), the 0055C000-00560000 managers.
//
// The world's paths are taken from the command line globals; every manager is made, referenced
// and attached to the world (its slot 13 with the world, or slot 2); a 0x0C-byte helper goes into
// a counted pointer at +0x5C; then the ISAAC generator is seeded and +0x70 set up.
#include "world.h"
#include "world_data.h"

#include "base_manager.h"
#include "item.h"
#include "random.h"

#include "ei/hook.h"
#include "ei/vcall.h"

#include "lib/crt.h"

#include <cstdint>
#include <cstring>
#include "originals_core.h"
#include "originals_world.h"
#include "originals_library.h"

namespace {

// MFC CString::operator=(LPCTSTR) on the path globals.
inline void* __fastcall CStringAssign(void* s, void*, const char* t) { return CStringAssignTextOriginal(s, nullptr, t); }
// The managers' constructors (thiscall, no arguments; they return this).
__declspec(naked) void* __fastcall Manager5125E0(void*) EI_ORIGINAL(0x005125E0)
__declspec(naked) void* __fastcall Manager5AA070(void*) EI_ORIGINAL(0x005AA070)
__declspec(naked) void* __fastcall Manager683FF0(void*) EI_ORIGINAL(0x00683FF0)
__declspec(naked) void* __fastcall Manager688840(void*) EI_ORIGINAL(0x00688840)
__declspec(naked) void* __fastcall Manager6A5AB0(void*) EI_ORIGINAL(0x006A5AB0)
__declspec(naked) void* __fastcall ResourceManagerConstruct(void*) EI_ORIGINAL(0x00674D90)
// 0049F150 IsaacRandom::Init (random.cpp); 006D43E0 the world's +0x70 set up.
__declspec(naked) void __fastcall RandomInit(IsaacRandom*) EI_ORIGINAL(0x0049F150)

using Construct = void* (__fastcall*)(void*);

// operator new, the constructor (null without memory), an optional vtable, the reference.
CEngineObject* Make(std::size_t size, Construct construct, std::uintptr_t vtable) {
    void* memory = gamecrt::operator_new(size);
    CEngineObject* object = nullptr;
    if (memory) {
        object = static_cast<CEngineObject*>(construct(memory));
        if (vtable) {
            object = static_cast<CEngineObject*>(memory);
            ei::SetVtable(object, vtable);
        }
    }
    ++object->refCount_;   // the original counts the reference without testing the pointer
    return object;
}

// A manager attached to the world (its slot 13), after the world holds it.
void Attach(CEngineObject* manager, CWorld* world) { static_cast<CBaseManager*>(manager)->Init(world); }

} // namespace

int __fastcall WorldCreateManagers(CWorld* world) {
    world->paused_ = 0;
    CStringAssign(worlddata::DataFolderString(), nullptr, worlddata::ConfiguredDataPath());
    CStringAssign(worlddata::TextFolderString(), nullptr, worlddata::ConfiguredTextPath());
    CStringAssign(worlddata::BaseFolderString(), nullptr, worlddata::ConfiguredBasePath());
    world->tick_ = 0;
    // The wind: along x, no strength.
    const float direction[3] = {1.0f, 0.0f, 0.0f};
    std::memcpy(world->windDirection_, direction, sizeof(direction));
    world->updateCount_ = 0;
    world->windStrength_ = 0;

    world->players_ = Make(sizeof(CBaseManager), ManagerConstructBaseOriginal, worlddata::kPlayerListVtable);
    Attach(world->players_, world);
    world->manager40_ = Make(0xBC, Manager5125E0, 0);
    Attach(world->manager40_, world);
    world->manager3C_ = Make(0xD0, Manager5AA070, 0);
    Attach(world->manager3C_, world);
    world->manager48_ = Make(sizeof(CBaseManager), ManagerConstructBaseOriginal, worlddata::kTimedObjectsVtable);
    Attach(world->manager48_, world);
    world->manager50_ = Make(0x24, Manager683FF0, 0);
    world->manager50_->Create();
    world->manager4C_ = Make(0xD4, Manager688840, 0);
    Attach(world->manager4C_, world);
    world->manager58_ = Make(0x34, Manager6A5AB0, 0);
    world->manager58_->Create();
    world->itemManager_ = Make(CItemManager::kSize, ResourceManagerConstruct, CItemManager::kVtableAddress);
    Attach(world->itemManager_, world);

    // The helper goes into the counted pointer at +0x5C (RefPtr::ShareAssign, inlined).
    auto* helper = static_cast<CWorldHandle*>(gamecrt::operator_new(sizeof(CWorldHandle)));
    // The count and both flag bytes as one dword, written before the test as the original does.
    const std::uint32_t noReferences = 0;
    std::memcpy(&helper->refs_, &noReferences, sizeof(noReferences));
    if (helper) {
        helper->pendingLink_ = nullptr;
        ei::SetVtable(helper, CWorldHandle::kVtableAddress);
    }
    world->shared5C_.ShareAssign(helper);
    RandomInit(&IsaacRandom::Global());
    TweenArrayRemoveAllOriginal(world->countdowns_);
    return 1;
}
EI_REPLACE(0x0055EE40, WorldCreateManagers);

CWorld* CWorld::ConstructBase() {
    ClearFields();
    figureLoader_ = nullptr;
    shared5C_.ptr = nullptr;
    netSession_ = nullptr;
    reinterpret_cast<mfc::CArray<int>*>(countdowns_)->Construct(kCountdownsVtable);
    ei::SetVtable(this, kBaseVtableAddress);
    players_ = nullptr;
    manager3C_ = nullptr;
    manager40_ = nullptr;
    manager44_ = nullptr;
    manager48_ = nullptr;
    manager4C_ = nullptr;
    manager50_ = nullptr;
    itemManager_ = nullptr;
    manager58_ = nullptr;
    timeBase_ = 0.0f;
    secondsPerTick_ = 0.0f;
    return this;
}
EI_REPLACE_METHOD(0x0055EAB0, CWorld::ConstructBase);

CEngineObject* CWorld::FindUnit(int id) {
    CEngineObject* object = ObjectWithId(id);
    if (!object) return nullptr;
    return object->GetClassId() == world::kClassUnit ? object : nullptr;
}
EI_REPLACE_METHOD(0x0055FB00, CWorld::FindUnit);

CEngineObject* CWorld::FindMapObject(int id) {
    return static_cast<CBaseManager*>(MapObjectManager())->FindObject(static_cast<unsigned>(id));
}
EI_REPLACE_METHOD(0x0055FAE0, CWorld::FindMapObject);
