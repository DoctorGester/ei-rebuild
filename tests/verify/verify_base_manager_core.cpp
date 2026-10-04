// Checks for base_manager_core.cpp.
//  - ConstructBase: ours on the real manager; the original on a scratch block holding a copy of its
//    bytes before construction; BuiltCompare (bucket allocations pair up); the scratch is destroyed
//    with the destructor body.
//  - CopyFrom, the default name / description: pure, side by side.
//  - The destructor bodies: ours only.
#include "net_verify.h"

#include "../../src/game/base_manager.h"
#include "../../src/game/object.h"
#include "../../src/lib/crt.h"

#include <cstring>

CBaseManager* __fastcall BaseManagerDeletingDestructor(CBaseManager* self, void*, unsigned flags);
CEngineObject* __fastcall EngineObjectDeletingDestructor(CEngineObject* self, void*, unsigned flags);
int __fastcall BaseManagerInit(CBaseManager* self, void*, void* owner);
CEngineObject* __fastcall BaseManagerCreateObject(CBaseManager* self, void*, int type, int id);
int __fastcall BaseManagerRemoveObject(CBaseManager* self, void*, CEngineObject* object);
void __fastcall BaseManagerReleaseObject(CBaseManager* self, void*, CEngineObject* object);
void __fastcall BaseManagerDestroyAll(CBaseManager* self);
CEngineObject* __fastcall BaseManagerUnregister(CBaseManager* self, void*, int id);
int __fastcall BaseManagerRegisterInstant(CBaseManager* self, void*, CEngineObject* object);
void __fastcall BaseManagerFreeRecords(CBaseManager* self);
void __fastcall BaseManagerLoadObjects(CBaseManager* self, void*, void* stream);
void __fastcall BaseManagerSaveObjects(CBaseManager* self, void*, void* stream);
void __fastcall BaseManagerReleaseUnused(CBaseManager* self);
void __fastcall BaseManagerSetContext(CBaseManager* self, void*, void* context);
int __fastcall BaseManagerSlot16(CBaseManager*, void*, int);
int __fastcall BaseManagerSlot17(CBaseManager*, void*, int);
int __fastcall BaseManagerSlot19(CBaseManager*, void*, int, int);
bool __fastcall BaseManagerSlot20(CBaseManager*, void*, int);
int __fastcall BaseManagerSlot21(CBaseManager*, void*, int);

namespace {

void* s_unused;

// 00402B20: PUSH -1 (2), PUSH imm32 (5).
CBaseManager*(__fastcall* s_construct)(CBaseManager*);
CBaseManager* __fastcall ConstructVerify(CBaseManager* self) {
    constexpr std::size_t kSize = sizeof(CBaseManager);
    auto* scratch = static_cast<std::uint8_t*>(gamecrt::operator_new(kSize));
    std::memcpy(scratch, self, kSize);
    CBaseManager* actual = self->ConstructBase();
    CBaseManager* expected = s_construct(reinterpret_cast<CBaseManager*>(scratch));
    // The four hashtables' empty functor bytes (+0 .. +2 of each) are copied from uninitialised
    // stack bytes in the original (and in the container constructors both call): they are never
    // read, so they are left out of the comparison.
    for (std::size_t table : {0x1Cu, 0x50u, 0x68u, 0x80u})
        std::memcpy(scratch + table, reinterpret_cast<const std::uint8_t*>(self) + table, 3);
    netverify::BuiltCompare cmp{scratch, reinterpret_cast<const std::uint8_t*>(self), kSize};
    cmp.Range(0, kSize);
    ei::verify::Check("CBaseManager::ConstructBase", true,
                      cmp.ok && expected == reinterpret_cast<CBaseManager*>(scratch) && actual == self, "%s", cmp.detail);
    reinterpret_cast<CBaseManager*>(scratch)->DestructBody();
    gamecrt::operator_delete(scratch);
    return actual;
}

void __fastcall DestructVerify(CBaseManager* self) {
    self->DestructBody();
    ei::verify::Check("CBaseManager::DestructBody", true, true, "");
}
CBaseManager* __fastcall ManagerDeleteVerify(CBaseManager* self, void*, unsigned flags) {
    ei::verify::Check("BaseManagerDeletingDestructor", true, true, "");
    return BaseManagerDeletingDestructor(self, nullptr, flags);
}
CEngineObject* __fastcall ObjectDeleteVerify(CEngineObject* self, void*, unsigned flags) {
    ei::verify::Check("EngineObjectDeletingDestructor", true, true, "");
    return EngineObjectDeletingDestructor(self, nullptr, flags);
}

// 00402D40: MOV EAX,[ESP+4] (4), MOV EDX,[EAX+8] (3).
void(__fastcall* s_copy)(CEngineObject*, void*, const CEngineObject*);
void __fastcall CopyVerify(CEngineObject* self, void*, const CEngineObject* other) {
    alignas(4) unsigned char scratch[sizeof(CEngineObject)];
    std::memcpy(scratch, self, sizeof(scratch));
    s_copy(reinterpret_cast<CEngineObject*>(scratch), nullptr, other);
    self->CEngineObject::CopyFrom(*other);
    ei::verify::Check("CEngineObject::CopyFrom", true, std::memcmp(scratch, self, sizeof(scratch)) == 0, "");
}

// 00402CE0 / 00402CF0: MOV EAX,imm32 (5).
const char*(__fastcall* s_name)(void*);
const char*(__fastcall* s_description)(void*);
const char* __fastcall NameVerify(void* self) {
    const char* expected = s_name(self);
    const char* actual = reinterpret_cast<const char*>(0x00783BF0);
    ei::verify::Check("DefaultName", expected, actual, "");
    return actual;
}
const char* __fastcall DescriptionVerify(void* self) {
    const char* expected = s_description(self);
    const char* actual = reinterpret_cast<const char*>(0x00783BF8);
    ei::verify::Check("DefaultDescription", expected, actual, "");
    return actual;
}

// 004031E0: ours only; afterwards no object is registered.
void __fastcall DestroyVerify2(CBaseManager* self) {
    self->CBaseManager::Destroy();
    ei::verify::Check("CBaseManager::Destroy", 0u, static_cast<unsigned>(self->objects_.Size()), "");
}

// 00403020 / 004031B0 / 00403AE0: ours only (they empty tables, create, destroy objects).
int __fastcall CreateVerify(CBaseManager* self) {
    int result = self->CBaseManager::Create();
    ei::verify::Check("CBaseManager::Create", true, result == 1 && self->objects_.Size() == 0 && self->maxId_ == 0, "");
    return result;
}
int __fastcall InitVerify(CBaseManager* self, void*, void* owner) {
    int result = BaseManagerInit(self, nullptr, owner);
    ei::verify::Check("BaseManagerInit", true, result ? self->owner_AC == owner : self->owner_AC == nullptr, "");
    return result;
}
void __fastcall ReleaseObjectVerify(CBaseManager* self, void*, CEngineObject* object) {
    BaseManagerReleaseObject(self, nullptr, object);
    ei::verify::Check("BaseManagerReleaseObject", true, true, "");
}

// 00403730: ours only (it creates the object). The object must be registered under its id with
// the manager as owner.
CEngineObject* __fastcall CreateObjectVerify(CBaseManager* self, void*, int type, int id) {
    CEngineObject* object = BaseManagerCreateObject(self, nullptr, type, id);
    bool ok = !object || (object->owner_ == self && object->type_ == type && (id == -1 || object->id_ == id) &&
                          self->FindObject(static_cast<unsigned>(object->id_)) == object);
    ei::verify::Check("BaseManagerCreateObject", true, ok, "type %d id %d", type, id);
    return object;
}

// 00404040: ours only; the object ends as a spare with id -1.
int __fastcall RemoveObjectVerify(CBaseManager* self, void*, CEngineObject* object) {
    int result = BaseManagerRemoveObject(self, nullptr, object);
    ei::verify::Check("BaseManagerRemoveObject", true, result == 0 && object->id_ == -1, "");
    return result;
}

// 004034E0: ours only; afterwards nothing is registered.
void __fastcall DestroyAllVerify(CBaseManager* self) {
    BaseManagerDestroyAll(self);
    ei::verify::Check("BaseManagerDestroyAll", true, self->objects_.Size() == 0 && self->maxId_ == 0, "");
}

// 004039B0 / 00403BD0: ours only (they change the tables); the id is (no longer) registered.
CEngineObject* __fastcall UnregisterVerify(CBaseManager* self, void*, int id) {
    CEngineObject* before = self->FindObject(static_cast<unsigned>(id));
    CEngineObject* result = BaseManagerUnregister(self, nullptr, id);
    ei::verify::Check("BaseManagerUnregister", true,
                      result == before && self->FindObject(static_cast<unsigned>(id)) == nullptr, "id %d", id);
    return result;
}
int __fastcall RegisterInstantVerify(CBaseManager* self, void*, CEngineObject* object) {
    int id = BaseManagerRegisterInstant(self, nullptr, object);
    ei::verify::Check("BaseManagerRegisterInstant", true,
                      object->id_ == id && object->owner_ == self && self->FindObject(static_cast<unsigned>(id)) == object,
                      "id %d", id);
    return id;
}

// 00404410: ours only; the spare tables end empty.
void __fastcall FreeRecordsVerify(CBaseManager* self) {
    BaseManagerFreeRecords(self);
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(self);
    std::uint32_t spares, counts;
    std::memcpy(&spares, bytes + 0x78, 4);
    std::memcpy(&counts, bytes + 0x90, 4);
    ei::verify::Check("BaseManagerFreeRecords", true, spares == 0 && counts == 0, "");
}

// 004046E0 / 00404760 / 004047E0 / 00404860: ours only (they read or write the stream).
void __fastcall LoadVerify(CBaseManager* self, void*, void* stream) {
    self->CBaseManager::Load(stream);
    ei::verify::Check("CBaseManager::Load", true, true, "");
}
void __fastcall SaveVerify(CBaseManager* self, void*, void* stream) {
    self->CBaseManager::Save(stream);
    ei::verify::Check("CBaseManager::Save", true, true, "");
}
void __fastcall LoadObjectsVerify(CBaseManager* self, void*, void* stream) {
    BaseManagerLoadObjects(self, nullptr, stream);
    ei::verify::Check("BaseManagerLoadObjects", true, true, "");
}
void __fastcall SaveObjectsVerify(CBaseManager* self, void*, void* stream) {
    BaseManagerSaveObjects(self, nullptr, stream);
    ei::verify::Check("BaseManagerSaveObjects", true, true, "");
}

// 004048F0: ours only (it releases objects).
void __fastcall ReleaseUnusedVerify(CBaseManager* self) {
    BaseManagerReleaseUnused(self);
    ei::verify::Check("BaseManagerReleaseUnused", true, true, "");
}

// 00404920: ours only (its prologue has a short jump within the first 5 bytes); the iterator holds
// the manager and the returned object is the list's first.
CEngineObject* __fastcall ListFirstVerify(ObjectListIterator* it, void*, CBaseManager* manager) {
    CEngineObject* result = it->First(manager);
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(manager);
    void** head;
    std::memcpy(&head, bytes + 0x38, 4);
    bool ok = it->manager == manager && (head ? result == head[2] && it->position == head[0] : !result && !it->position);
    ei::verify::Check("ObjectListFirst", true, ok, "");
    return result;
}

// 00404960: MOV EAX,[ECX+4] (3), TEST EAX,EAX (2). Side by side on a copy of the iterator.
CEngineObject*(__fastcall* s_listNext)(ObjectListIterator*);
CEngineObject* __fastcall ListNextVerify(ObjectListIterator* it) {
    ObjectListIterator copy = *it;
    CEngineObject* expected = s_listNext(&copy);
    CEngineObject* actual = it->Next();
    ei::verify::Check("ObjectListNext", true, expected == actual && copy.position == it->position && copy.manager == it->manager, "");
    return actual;
}

// 00403720: ours only.
void __fastcall SetContextVerify(CBaseManager* self, void*, void* context) {
    BaseManagerSetContext(self, nullptr, context);
    ei::verify::Check("BaseManagerSetContext", true, self->owner_AC == context, "");
}

// The empty slots: XOR (2) + RET n (3), side by side.
int(__fastcall* s_slot16)(CBaseManager*, void*, int);
int(__fastcall* s_slot17)(CBaseManager*, void*, int);
int(__fastcall* s_slot19)(CBaseManager*, void*, int, int);
int(__fastcall* s_slot20)(CBaseManager*, void*, int);
int(__fastcall* s_slot21)(CBaseManager*, void*, int);
int __fastcall Slot16Verify(CBaseManager* self, void*, int a) {
    int actual = BaseManagerSlot16(self, nullptr, a);
    ei::verify::Check("BaseManagerSlot16", s_slot16(self, nullptr, a), actual, "");
    return actual;
}
int __fastcall Slot17Verify(CBaseManager* self, void*, int a) {
    int actual = BaseManagerSlot17(self, nullptr, a);
    ei::verify::Check("BaseManagerSlot17", s_slot17(self, nullptr, a), actual, "");
    return actual;
}
int __fastcall Slot19Verify(CBaseManager* self, void*, int a, int b) {
    int actual = BaseManagerSlot19(self, nullptr, a, b);
    ei::verify::Check("BaseManagerSlot19", s_slot19(self, nullptr, a, b), actual, "");
    return actual;
}
bool __fastcall Slot20Verify(CBaseManager* self, void*, int a) {
    bool actual = BaseManagerSlot20(self, nullptr, a);
    bool expected = (s_slot20(self, nullptr, a) & 0xFF) != 0;
    ei::verify::Check("BaseManagerSlot20", expected, actual, "");
    return actual;
}
int __fastcall Slot21Verify(CBaseManager* self, void*, int a) {
    int actual = BaseManagerSlot21(self, nullptr, a);
    ei::verify::Check("BaseManagerSlot21", s_slot21(self, nullptr, a), actual, "");
    return actual;
}

} // namespace

EI_VERIFY(0x004034E0, DestroyAllVerify, 0, s_unused);
EI_VERIFY(0x004039B0, UnregisterVerify, 0, s_unused);
EI_VERIFY(0x00403BD0, RegisterInstantVerify, 0, s_unused);
EI_VERIFY(0x00404410, FreeRecordsVerify, 0, s_unused);
EI_VERIFY(0x004046E0, LoadVerify, 0, s_unused);
EI_VERIFY(0x00404760, SaveVerify, 0, s_unused);
EI_VERIFY(0x004047E0, LoadObjectsVerify, 0, s_unused);
EI_VERIFY(0x00404860, SaveObjectsVerify, 0, s_unused);
EI_VERIFY(0x004048F0, ReleaseUnusedVerify, 0, s_unused);
EI_VERIFY(0x00404920, ListFirstVerify, 0, s_unused);
EI_VERIFY(0x00404960, ListNextVerify, 5, s_listNext);
EI_VERIFY(0x00403720, SetContextVerify, 0, s_unused);
EI_VERIFY(0x00402D60, Slot16Verify, 5, s_slot16);
EI_VERIFY(0x00402D70, Slot17Verify, 5, s_slot17);
EI_VERIFY(0x00402D90, Slot19Verify, 5, s_slot19);
EI_VERIFY(0x00402DA0, Slot20Verify, 5, s_slot20);
EI_VERIFY(0x00402DB0, Slot21Verify, 5, s_slot21);
EI_VERIFY(0x00404040, RemoveObjectVerify, 0, s_unused);
EI_VERIFY(0x00403730, CreateObjectVerify, 0, s_unused);
EI_VERIFY(0x00403020, CreateVerify, 0, s_unused);
EI_VERIFY(0x004031B0, InitVerify, 0, s_unused);
EI_VERIFY(0x00403AE0, ReleaseObjectVerify, 0, s_unused);
EI_VERIFY(0x004031E0, DestroyVerify2, 0, s_unused);
EI_VERIFY(0x00402B20, ConstructVerify, 7, s_construct);
EI_VERIFY(0x00402DF0, DestructVerify, 0, s_unused);
EI_VERIFY(0x00402DD0, ManagerDeleteVerify, 0, s_unused);
EI_VERIFY(0x00402D20, ObjectDeleteVerify, 0, s_unused);
EI_VERIFY(0x00402D40, CopyVerify, 7, s_copy);
EI_VERIFY(0x00402CE0, NameVerify, 5, s_name);
EI_VERIFY(0x00402CF0, DescriptionVerify, 5, s_description);
