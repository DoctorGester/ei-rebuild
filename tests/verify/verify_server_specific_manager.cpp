// Checks for server_specific_manager.cpp. Scenario: newgame (the world server creates the manager,
// 0058AE10, and its server objects; torches make CTorchObjectSpec objects), world_load.
#include "verify.h"

#include "../../src/game/server_specific_manager.h"

#include <cstring>

namespace {
std::uintptr_t VtableOf(const void* object) {
    std::uintptr_t vtable;
    std::memcpy(&vtable, object, sizeof(vtable));
    return vtable;
}
} // namespace

// 004D6900 (ours only): the manager's vtable, no id yet.
static CServerSpecificManager* (__fastcall* s_construct)(CServerSpecificManager*);
static CServerSpecificManager* __fastcall ConstructVerify(CServerSpecificManager* self) {
    CServerSpecificManager* result = self->Construct();
    ei::verify::Check("ServerSpecificManager::Construct", true,
                      result == self && VtableOf(self) == CServerSpecificManager::kVtableAddress && self->lastId_ == -1,
                      "%p", static_cast<void*>(self));
    return result;
}
EI_VERIFY(0x004D6900, ConstructVerify, 0, s_construct);

// 004D6970 (ours only): a known type gives a fresh object (no references, id -1); a torch spec has
// its vtable, an empty name and its Create's values; any other type gives null.
static CEngineObject* (__fastcall* s_newObject)(CServerSpecificManager*, void*, int);
static CEngineObject* __fastcall NewObjectVerify(CServerSpecificManager* self, void*, int type) {
    CEngineObject* object = self->CServerSpecificManager::NewObject(type);
    const bool known = type >= 0x46 && type <= 0x4B && type != 0x4A;
    bool ok = known ? object && object->refCount_ == 0 && object->id_ == -1 && object->owner_ == nullptr : !object;
    if (ok && type == 0x4B) {
        auto* spec = static_cast<CTorchObjectSpec*>(object);
        ok = VtableOf(spec) == CTorchObjectSpec::kVtableAddress && spec->name_.GetLength() == 0 &&
             spec->torch_ == nullptr && spec->values_[1] == 10.0f;
    }
    ei::verify::Check("ServerSpecificManager::NewObject", true, ok, "type %X", type);
    return object;
}
EI_VERIFY(0x004D6970, NewObjectVerify, 0, s_newObject);

// 004D6BA0 (ours only): the engine object's vtable afterwards.
static void (__fastcall* s_specDestruct)(CTorchObjectSpec*);
static void __fastcall SpecDestructVerify(CTorchObjectSpec* self) {
    self->Destruct();
    ei::verify::Check("TorchObjectSpec::Destruct", CEngineObject::kVtableAddress, VtableOf(self), "%08X");
}
EI_VERIFY(0x004D6BA0, SpecDestructVerify, 0, s_specDestruct);

// The class names and ids (MOV EAX,imm32, 5 bytes) and slot 6 against the original.
static const char* (__fastcall* s_managerName)(CServerSpecificManager*);
static const char* __fastcall ManagerNameVerify(CServerSpecificManager* self) {
    const char* expected = s_managerName(self);
    const char* actual = self->CServerSpecificManager::GetClassName();
    ei::verify::Check("ServerSpecificManager::GetClassName", expected, actual, "%p");
    return actual;
}
EI_VERIFY(0x004D6920, ManagerNameVerify, 5, s_managerName);

static int (__fastcall* s_managerId)(CServerSpecificManager*);
static int __fastcall ManagerIdVerify(CServerSpecificManager* self) {
    const int expected = s_managerId(self);
    const int actual = self->CServerSpecificManager::GetClassId();
    ei::verify::Check("ServerSpecificManager::GetClassId", expected, actual, "%X");
    return actual;
}
EI_VERIFY(0x004D6930, ManagerIdVerify, 5, s_managerId);

static const char* (__fastcall* s_specName)(CTorchObjectSpec*);
static const char* __fastcall SpecNameVerify(CTorchObjectSpec* self) {
    const char* expected = s_specName(self);
    const char* actual = self->CTorchObjectSpec::GetClassName();
    ei::verify::Check("TorchObjectSpec::GetClassName", expected, actual, "%p");
    return actual;
}
EI_VERIFY(0x004D6B60, SpecNameVerify, 5, s_specName);

static int (__fastcall* s_specId)(CTorchObjectSpec*);
static int __fastcall SpecIdVerify(CTorchObjectSpec* self) {
    const int expected = s_specId(self);
    const int actual = self->CTorchObjectSpec::GetClassId();
    ei::verify::Check("TorchObjectSpec::GetClassId", expected, actual, "%X");
    return actual;
}
EI_VERIFY(0x004D6B70, SpecIdVerify, 5, s_specId);

// 004D6960 (ours only): the engine object's vtable afterwards (CBaseManager's destructor body).
static void (__fastcall* s_managerDestruct)(CServerSpecificManager*);
static void __fastcall ManagerDestructVerify(CServerSpecificManager* self) {
    self->Destruct();
    ei::verify::Check("ServerSpecificManager::Destruct", CEngineObject::kVtableAddress, VtableOf(self), "%08X");
}
EI_VERIFY(0x004D6960, ManagerDestructVerify, 0, s_managerDestruct);

// 004D6940 / 004D6B80 (ours only): the result is the object.
static CServerSpecificManager* (__fastcall* s_managerDelete)(CServerSpecificManager*, void*, unsigned);
static CServerSpecificManager* __fastcall ManagerDeleteVerify(CServerSpecificManager* self, void*, unsigned flags) {
    CServerSpecificManager* result = self->DeletingDestruct(flags);
    ei::verify::Check("ServerSpecificManager::DeletingDestruct", static_cast<void*>(self), static_cast<void*>(result), "%p");
    return result;
}
EI_VERIFY(0x004D6940, ManagerDeleteVerify, 0, s_managerDelete);

static CTorchObjectSpec* (__fastcall* s_specDelete)(CTorchObjectSpec*, void*, unsigned);
static CTorchObjectSpec* __fastcall SpecDeleteVerify(CTorchObjectSpec* self, void*, unsigned flags) {
    CTorchObjectSpec* result = self->DeletingDestruct(flags);
    ei::verify::Check("TorchObjectSpec::DeletingDestruct", static_cast<void*>(self), static_cast<void*>(result), "%p");
    return result;
}
EI_VERIFY(0x004D6B80, SpecDeleteVerify, 0, s_specDelete);

// 004D6B50: MOV EAX,1 (5 bytes), against the original.
static int (__fastcall* s_specUpdate)(CTorchObjectSpec*);
static int __fastcall SpecUpdateVerify(CTorchObjectSpec* self) {
    const int expected = s_specUpdate(self);
    ei::verify::Check("TorchObjectSpec slot 6", expected, 1, "%d");
    return 1;
}
EI_VERIFY(0x004D6B50, SpecUpdateVerify, 5, s_specUpdate);
