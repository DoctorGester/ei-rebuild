// Checks for base_manager.cpp and engine_object.cpp.
//  - FindObject, CopyFrom, the default name / description, ObjectListIterator::Next and the empty
//    archive slots: pure (or writing only their own object), both run side by side.
//  - ConstructBase: ours on the real manager, then the original on a scratch block (our memory)
//    holding a copy of its bytes before construction; BuiltCompare (bucket allocations pair up); the scratch is destroyed
//    with the destructor body.
//  - Everything else (destructors, registration, spares, saving and loading): ours only, then the
//    resulting state is checked where it can be.
#include "net_verify.h"

#include "../../src/game/base_manager.h"
#include "../../src/lib/crt.h"

#include <cstring>
#include <vector>

namespace {

void* s_unused;

// 00403F40 starts with PUSH EBX; MOV EBX,[ECX+0x24]; PUSH ESI (5 bytes).
CEngineObject*(__fastcall* s_findObject)(const CBaseManager*, void*, unsigned);
CEngineObject* __fastcall FindObjectVerify(const CBaseManager* self, void*, unsigned id) {
    CEngineObject* expected = s_findObject(self, nullptr, id);
    ei::verify::Check("CBaseManager::FindObject", expected, self->FindObject(id), "id %u", id);
    return expected;
}

// 00402B20: PUSH -1 (2), PUSH imm32 (5).
CBaseManager*(__fastcall* s_construct)(CBaseManager*);
CBaseManager* __fastcall ConstructVerify(CBaseManager* self) {
    constexpr std::size_t kSize = sizeof(CBaseManager);
    // The scratch is our memory, not the game's heap, whose address order the game reads (SV2f).
    std::vector<std::uint8_t> scratchBytes(kSize);
    std::uint8_t* scratch = scratchBytes.data();
    std::memcpy(scratch, self, kSize);
    CBaseManager* actual = self->ConstructBase();
    CBaseManager* expected = s_construct(reinterpret_cast<CBaseManager*>(scratch));
    // The four hashtables' empty functor bytes (+0 .. +2 of each) are copied from uninitialised
    // stack bytes in the original (and in the container constructors both call): they are never
    // read, so they are left out of the comparison.
    for (std::size_t table : {offsetof(CBaseManager, objects_), offsetof(CBaseManager, positions_),
                              offsetof(CBaseManager, spares_), offsetof(CBaseManager, spareCounts_)})
        std::memcpy(scratch + table, reinterpret_cast<const std::uint8_t*>(self) + table, 3);
    netverify::BuiltCompare cmp{scratch, reinterpret_cast<const std::uint8_t*>(self), kSize};
    cmp.Range(0, kSize);
    ei::verify::Check("CBaseManager::ConstructBase", true,
                      cmp.ok && expected == reinterpret_cast<CBaseManager*>(scratch) && actual == self, "%s", cmp.detail);
    reinterpret_cast<CBaseManager*>(scratch)->DestructBody();
    return actual;
}

void __fastcall DestructVerify(CBaseManager* self) {
    self->DestructBody();
    ei::verify::Check("CBaseManager::DestructBody", true, true, "");
}
CBaseManager* __fastcall ManagerDeleteVerify(CBaseManager* self, void*, unsigned flags) {
    ei::verify::Check("CBaseManager::DeletingDestruct", true, true, "");
    return self->DeletingDestruct(flags);
}
CEngineObject* __fastcall ObjectDeleteVerify(CEngineObject* self, void*, unsigned flags) {
    ei::verify::Check("CEngineObject::DeletingDestruct", true, true, "");
    return self->DeletingDestruct(flags);
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
const char*(__fastcall* s_name)(CEngineObject*);
const char*(__fastcall* s_description)(CEngineObject*);
const char* __fastcall NameVerify(CEngineObject* self) {
    const char* expected = s_name(self);
    const char* actual = self->CEngineObject::GetName();
    ei::verify::Check("CEngineObject::GetName", expected, actual, "");
    return actual;
}
const char* __fastcall DescriptionVerify(CEngineObject* self) {
    const char* expected = s_description(self);
    const char* actual = self->CEngineObject::GetDescription();
    ei::verify::Check("CEngineObject::GetDescription", expected, actual, "");
    return actual;
}

// 004031E0: ours only; afterwards no object is registered.
void __fastcall DestroyVerify(CBaseManager* self) {
    self->CBaseManager::Destroy();
    ei::verify::Check("CBaseManager::Destroy", 0u, static_cast<unsigned>(self->objects_.Size()), "");
}

// 00403020 / 004031B0 / 00403AE0: ours only (they empty tables, create, destroy objects).
int __fastcall CreateVerify(CBaseManager* self) {
    int result = self->CBaseManager::Create();
    ei::verify::Check("CBaseManager::Create", true, result == 1 && self->objects_.Size() == 0 && self->maxId_ == 0, "");
    return result;
}
BOOL __fastcall InitVerify(CBaseManager* self, void*, void* context) {
    BOOL result = self->CBaseManager::InitImpl(context);
    ei::verify::Check("CBaseManager::Init", true, result ? self->context_ == context : self->context_ == nullptr, "");
    return result;
}
int __fastcall ReleaseObjectVerify(CBaseManager* self, void*, CEngineObject* object) {
    int result = self->CBaseManager::ReleaseObjectImpl(object);
    ei::verify::Check("CBaseManager::ReleaseObject", true, true, "");
    return result;
}

// 00403730: ours only (it creates the object). The object must be registered under its id with
// the manager as owner.
CEngineObject* __fastcall CreateObjectVerify(CBaseManager* self, void*, int type, int id) {
    CEngineObject* object = self->CBaseManager::CreateObjectImpl(type, id);
    bool ok = !object || (object->owner_ == self && object->type_ == type && (id == -1 || object->id_ == id) &&
                          self->FindObject(static_cast<unsigned>(object->id_)) == object);
    ei::verify::Check("CBaseManager::CreateObject", true, ok, "type %d id %d", type, id);
    return object;
}

// 00404040: ours only; the object ends as a spare with id -1.
int __fastcall RemoveObjectVerify(CBaseManager* self, void*, CEngineObject* object) {
    int result = self->RemoveObject(object);
    ei::verify::Check("CBaseManager::RemoveObject", true, result == 0 && object->id_ == -1, "");
    return result;
}

// 004034E0: ours only; afterwards nothing is registered.
void __fastcall DestroyAllVerify(CBaseManager* self) {
    self->CBaseManager::DestroyAll();
    ei::verify::Check("CBaseManager::DestroyAll", true, self->objects_.Size() == 0 && self->maxId_ == 0, "");
}

// 004039B0 / 00403BD0: ours only (they change the tables); the id is (no longer) registered.
CEngineObject* __fastcall UnregisterVerify(CBaseManager* self, void*, int id) {
    CEngineObject* before = self->FindObject(static_cast<unsigned>(id));
    CEngineObject* result = self->Unregister(id);
    ei::verify::Check("CBaseManager::Unregister", true,
                      result == before && self->FindObject(static_cast<unsigned>(id)) == nullptr, "id %d", id);
    return result;
}
int __fastcall RegisterInstantVerify(CBaseManager* self, void*, CEngineObject* object) {
    int id = self->RegisterInstant(object);
    ei::verify::Check("CBaseManager::RegisterInstant", true,
                      object->id_ == id && object->owner_ == self && self->FindObject(static_cast<unsigned>(id)) == object,
                      "id %d", id);
    return id;
}

// 00404410: ours only; the spare tables end empty.
void __fastcall FreeSparesVerify(CBaseManager* self) {
    self->FreeSpares();
    ei::verify::Check("CBaseManager::FreeSpares", true, self->spares_.Size() == 0 && self->spareCounts_.Size() == 0, "");
}

// 004046E0 / 00404760 / 004047E0 / 00404860: ours only (they read or write the stream).
void __fastcall LoadVerify(CBaseManager* self, void*, void* stream) {
    self->CBaseManager::LoadImpl(stream);
    ei::verify::Check("CBaseManager::Load", true, true, "");
}
void __fastcall SaveVerify(CBaseManager* self, void*, void* stream) {
    self->CBaseManager::SaveImpl(stream);
    ei::verify::Check("CBaseManager::Save", true, true, "");
}
void __fastcall LoadObjectsVerify(CBaseManager* self, void*, void* stream) {
    self->LoadObjects(stream);
    ei::verify::Check("CBaseManager::LoadObjects", true, true, "");
}
void __fastcall SaveObjectsVerify(CBaseManager* self, void*, void* stream) {
    self->SaveObjects(stream);
    ei::verify::Check("CBaseManager::SaveObjects", true, true, "");
}

// 004048F0: ours only (it releases objects).
void __fastcall ReleaseUnusedVerify(CBaseManager* self) {
    self->ReleaseUnused();
    ei::verify::Check("CBaseManager::ReleaseUnused", true, true, "");
}

// 00404920: ours only (its prologue has a short jump within the first 5 bytes); the iterator holds
// the manager and the returned object is the list's first.
CEngineObject* __fastcall ListFirstVerify(ObjectListIterator* it, void*, CBaseManager* manager) {
    CEngineObject* result = it->First(manager);
    const CBaseManager::ObjectList::Node* head = manager->objectList_.head;
    bool ok = it->manager == manager && (head ? result == head->data && it->position == head->next : !result && !it->position);
    ei::verify::Check("ObjectListIterator::First", true, ok, "");
    return result;
}

// 00404960: MOV EAX,[ECX+4] (3), TEST EAX,EAX (2). Side by side on a copy of the iterator.
CEngineObject*(__fastcall* s_listNext)(ObjectListIterator*);
CEngineObject* __fastcall ListNextVerify(ObjectListIterator* it) {
    ObjectListIterator copy = *it;
    CEngineObject* expected = s_listNext(&copy);
    CEngineObject* actual = it->Next();
    ei::verify::Check("ObjectListIterator::Next", true,
                      expected == actual && copy.position == it->position && copy.manager == it->manager, "");
    return actual;
}

// 00403720: ours only.
void __fastcall SetContextVerify(CBaseManager* self, void*, void* context) {
    self->SetContext(context);
    ei::verify::Check("CBaseManager::SetContext", true, self->context_ == context, "");
}

// The empty archive slots: XOR (2) + RET n (3), side by side.
BOOL(__fastcall* s_addArchive)(CBaseManager*, void*, const char*);
BOOL(__fastcall* s_removeArchive)(CBaseManager*, void*, const char*);
void*(__fastcall* s_getFile)(CBaseManager*, void*, const char*, std::uint32_t*);
int(__fastcall* s_closeFile)(CBaseManager*, void*, void*);
BOOL(__fastcall* s_hasFile)(CBaseManager*, void*, const char*);
BOOL __fastcall AddArchiveVerify(CBaseManager* self, void*, const char* path) {
    BOOL actual = self->CBaseManager::AddArchiveImpl(path);
    ei::verify::Check("CBaseManager::AddArchive", s_addArchive(self, nullptr, path), actual, "");
    return actual;
}
BOOL __fastcall RemoveArchiveVerify(CBaseManager* self, void*, const char* path) {
    BOOL actual = self->CBaseManager::RemoveArchiveImpl(path);
    ei::verify::Check("CBaseManager::RemoveArchive", s_removeArchive(self, nullptr, path), actual, "");
    return actual;
}
void* __fastcall GetFileVerify(CBaseManager* self, void*, const char* name, std::uint32_t* size) {
    void* actual = self->CBaseManager::GetFileImpl(name, size);
    ei::verify::Check("CBaseManager::GetFile", s_getFile(self, nullptr, name, size), actual, "");
    return actual;
}
bool __fastcall CloseFileVerify(CBaseManager* self, void*, void* data) {
    bool actual = self->CBaseManager::CloseFileImpl(data);
    bool expected = (s_closeFile(self, nullptr, data) & 0xFF) != 0;
    ei::verify::Check("CBaseManager::CloseFile", expected, actual, "");
    return actual;
}
BOOL __fastcall HasFileVerify(CBaseManager* self, void*, const char* name) {
    BOOL actual = self->CBaseManager::HasFileImpl(name);
    ei::verify::Check("CBaseManager::HasFile", s_hasFile(self, nullptr, name), actual, "");
    return actual;
}

} // namespace

EI_VERIFY(0x00403F40, FindObjectVerify, 5, s_findObject);
EI_VERIFY(0x004034E0, DestroyAllVerify, 0, s_unused);
EI_VERIFY(0x004039B0, UnregisterVerify, 0, s_unused);
EI_VERIFY(0x00403BD0, RegisterInstantVerify, 0, s_unused);
EI_VERIFY(0x00404410, FreeSparesVerify, 0, s_unused);
EI_VERIFY(0x004046E0, LoadVerify, 0, s_unused);
EI_VERIFY(0x00404760, SaveVerify, 0, s_unused);
EI_VERIFY(0x004047E0, LoadObjectsVerify, 0, s_unused);
EI_VERIFY(0x00404860, SaveObjectsVerify, 0, s_unused);
EI_VERIFY(0x004048F0, ReleaseUnusedVerify, 0, s_unused);
EI_VERIFY(0x00404920, ListFirstVerify, 0, s_unused);
EI_VERIFY(0x00404960, ListNextVerify, 5, s_listNext);
EI_VERIFY(0x00403720, SetContextVerify, 0, s_unused);
EI_VERIFY(0x00402D60, AddArchiveVerify, 5, s_addArchive);
EI_VERIFY(0x00402D70, RemoveArchiveVerify, 5, s_removeArchive);
EI_VERIFY(0x00402D90, GetFileVerify, 5, s_getFile);
EI_VERIFY(0x00402DA0, CloseFileVerify, 5, s_closeFile);
EI_VERIFY(0x00402DB0, HasFileVerify, 5, s_hasFile);
EI_VERIFY(0x00404040, RemoveObjectVerify, 0, s_unused);
EI_VERIFY(0x00403730, CreateObjectVerify, 0, s_unused);
EI_VERIFY(0x00403020, CreateVerify, 0, s_unused);
EI_VERIFY(0x004031B0, InitVerify, 0, s_unused);
EI_VERIFY(0x00403AE0, ReleaseObjectVerify, 0, s_unused);
EI_VERIFY(0x004031E0, DestroyVerify, 0, s_unused);
EI_VERIFY(0x00402B20, ConstructVerify, 7, s_construct);
EI_VERIFY(0x00402DF0, DestructVerify, 0, s_unused);
EI_VERIFY(0x00402DD0, ManagerDeleteVerify, 0, s_unused);
EI_VERIFY(0x00402D20, ObjectDeleteVerify, 0, s_unused);
EI_VERIFY(0x00402D40, CopyVerify, 7, s_copy);
EI_VERIFY(0x00402CE0, NameVerify, 5, s_name);
EI_VERIFY(0x00402CF0, DescriptionVerify, 5, s_description);
