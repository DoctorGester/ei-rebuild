// Checks for app_lifetime.cpp (A-E1). Main thread. All ours only: the creators make a party (its
// Create is not repeatable side by side, as the party checks note) and the rest destroy.
//  - CreatePartyRecord / CreateClassTemplate: the new object's vtable, a count of 0 and its first
//    fields. Reached when a saved game's party manager is loaded (the class registry 0079B270 makes
//    the records and templates), by reading.
//  - PartyRecord::Destruct / DeletingDestruct: the root vtable left when the memory stays. Reached
//    when a record loses its last reference (a new game or a load after one).
//  - CDirectInputMouse::Destruct / DeletingDestruct and the root's slot 0: the root vtable left, no
//    device or DirectInput object kept. Reached at the application's exit; the root's slot never.
//  - PartyManager::Destruct: the map empty afterwards. Reached at the process's exit (atexit).
#include "verify.h"

#include "../../src/game/class_templates.h"
#include "../../src/game/party_manager.h"
#include "../../src/game/party_records.h"
#include "../../src/game/ui_cursor.h"

#include <cstdint>

// app_lifetime.cpp (00457950; no header declares it).
void* __fastcall CountedRootDeletingDestruct(void* self, void*, unsigned flags);

namespace {
void* s_unused;

void* const kCountedRootVtable = reinterpret_cast<void*>(0x0073BB68);
void* const kPartyRecordVtable = reinterpret_cast<void*>(0x0073C360);
void* const kMouseRootVtable = reinterpret_cast<void*>(0x0073C368);

PartyRecord* __cdecl CreateRecordVerify() {
    PartyRecord* record = CreatePartyRecord();
    bool good = record && record->vtable == kPartyRecordVtable && record->refCount == 0 && record->unknown_06 == 0 &&
                record->name.GetLength() == 0;
    ei::verify::Check("CreatePartyRecord", true, good, "%p", static_cast<void*>(record));
    return record;
}

ClassTemplate* __cdecl CreateTemplateVerify() {
    ClassTemplate* created = CreateClassTemplate();
    bool good = created && created->vtable == reinterpret_cast<void*>(ClassTemplate::kVtableAddress) &&
                created->refCount == 0 && created->unknown_06 == 0 && created->type == -1 && created->flag38 == 1;
    ei::verify::Check("CreateClassTemplate", true, good, "%p", static_cast<void*>(created));
    return created;
}

void __fastcall RecordDestructVerify(PartyRecord* self) {
    self->Destruct();
    ei::verify::Check("PartyRecord::Destruct", true, self->vtable == kCountedRootVtable, "");
}

PartyRecord* __fastcall RecordDeletingVerify(PartyRecord* self, void*, unsigned flags) {
    PartyRecord* result = self->DeletingDestruct(flags);
    bool good = result == self && ((flags & 1) || self->vtable == kCountedRootVtable);
    ei::verify::Check("PartyRecord::DeletingDestruct", true, good, "flags %u", flags);
    return result;
}

void __fastcall MouseDestructVerify(CDirectInputMouse* self) {
    self->Destruct();
    bool good = self->vtable_ == kMouseRootVtable && !self->device_ && !self->input_;
    ei::verify::Check("CDirectInputMouse::Destruct", true, good, "");
}

CDirectInputMouse* __fastcall MouseDeletingVerify(CDirectInputMouse* self, void*, unsigned flags) {
    CDirectInputMouse* result = self->DeletingDestruct(flags);
    bool good = result == self && ((flags & 1) || self->vtable_ == kMouseRootVtable);
    ei::verify::Check("CDirectInputMouse::DeletingDestruct", true, good, "flags %u", flags);
    return result;
}

void* __fastcall RootDeletingVerify(void* self, void*, unsigned flags) {
    void* result = ::CountedRootDeletingDestruct(self, nullptr, flags);
    bool good = result == self && ((flags & 1) || *static_cast<void**>(self) == kMouseRootVtable);
    ei::verify::Check("the counted root's slot 0 (00457950)", true, good, "flags %u", flags);
    return result;
}

void __fastcall ManagerDestructVerify(PartyManager* self) {
    unsigned parties = static_cast<unsigned>(self->parties.Size());
    self->Destruct();
    ei::verify::Check("PartyManager::Destruct", true, self->parties.Size() == 0, "%u parties", parties);
}
} // namespace

EI_VERIFY(0x00457740, CreateRecordVerify, 0, s_unused);
EI_VERIFY(0x00457850, CreateTemplateVerify, 0, s_unused);
EI_VERIFY(0x004577E0, RecordDestructVerify, 0, s_unused);
EI_VERIFY(0x004577C0, RecordDeletingVerify, 0, s_unused);
EI_VERIFY(0x004578D0, MouseDestructVerify, 0, s_unused);
EI_VERIFY(0x004578B0, MouseDeletingVerify, 0, s_unused);
EI_VERIFY(0x00457950, RootDeletingVerify, 0, s_unused);
EI_VERIFY(0x004A5700, ManagerDestructVerify, 0, s_unused);
