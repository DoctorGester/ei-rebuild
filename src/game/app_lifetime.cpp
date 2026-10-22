// Creators and destructors the application's start (00456440) registers or runs (A-E1):
//  - the creators of the class registry at 0079B270: id 1 a class template (00457850), id 2 a named
//    party record (00457740); the registry makes these objects when a saved party manager is loaded;
//  - the party record's destructor (004577E0, its slot 0 004577C0);
//  - the DirectInput mouse's destructor (004578D0, its slot 0 004578B0), run when the application
//    drops the mouse at exit, and the counted root's slot 0 (00457950);
//  - the party manager's destructor (004A5700), which the C runtime runs at exit on 007AA5A0.
// Main thread. See docs/notes/app.md.
#define DIRECTINPUT_VERSION 0x0700
#include "class_templates.h"
#include "core_refcount.h"
#include "originals_script.h"
#include "party.h"
#include "party_manager.h"
#include "party_records.h"
#include "ui_cursor.h"

#include "ei/hook.h"
#include "lib/compiled_vtable.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "lib/stlp.h"
#include "originals_library.h"
#include "game_hashes.h"
#include "game_data.h"

#include <dinput.h>

#include <cstring>
#include <new>

namespace {


CParty* PartyOf(PartyRecord* record) { return record->Party(); }

// The compiler's tables in place of game.exe's (C19): the party record's 0073C360 (0 the deleting
// destructor, 1 Serialize) and the mouse's 0073C350 (0 the deleting destructor).
struct PartyRecordClass final : ei::CompiledDeleter<PartyRecordClass, &PartyRecord::DeletingDestruct> {
    virtual void Serialize(ChunkArchive* ar) { ObjectAs<PartyRecord>()->Serialize(ar); }   // 1
};
struct MouseClass final : ei::CompiledDeleter<MouseClass, &CDirectInputMouse::DeletingDestruct> {};

// A counted pointer's release (00439910): the object deleted with its last reference.
template <typename T>
void ReleaseCounted(T* object) {
    if (object) reinterpret_cast<CRefObject*>(object)->Release();
}

} // namespace

// ---- The registry's creators ----------------------------------------------------------------------

PartyRecord* __cdecl CreatePartyRecord() {
    auto* record = gamecrt::operator_new_as<PartyRecord>();
    // The count's dword is cleared before the null check, as the original does.
    record->refCount = 0;
    record->refsHigh = 0;
    if (!record) return nullptr;
    record->vtable = CRefCounted::RootVtable();
    PartyOf(record)->Construct();
    new (&record->name) mfc::CString();
    record->vtable = PartyRecord::Vtable();
    PartyOf(record)->Create();
    return record;
}
EI_REPLACE(0x00457740, CreatePartyRecord);

ClassTemplate* __cdecl CreateClassTemplate() {
    auto* created = gamecrt::operator_new_as<ClassTemplate>();
    created->refCount = 0;
    created->refsHigh = 0;
    if (!created) return nullptr;
    std::memset(created->weightsTail, 0, sizeof(created->weightsTail));   // SV7b2: never set, saved
    return created->Construct();
}
EI_REPLACE(0x00457850, CreateClassTemplate);

// ---- The party record -----------------------------------------------------------------------------

void* PartyRecord::Vtable() { return PartyRecordClass::Vtable(); }
void* CDirectInputMouse::Vtable() { return MouseClass::Vtable(); }

void PartyRecord::Destruct() {
    vtable = PartyRecord::Vtable();
    PartyOf(this)->Destroy();
    name.~CString();
    PartyOf(this)->DestructBody();
    vtable = CRefCounted::RootVtable();
}
EI_REWRITTEN(0x004577E0, PartyRecord::Destruct);

PartyRecord* PartyRecord::DeletingDestruct(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REWRITTEN(0x004577C0, PartyRecord::DeletingDestruct);

// ---- The DirectInput mouse ------------------------------------------------------------------------

void CDirectInputMouse::Destruct() {
    vtable_ = CDirectInputMouse::Vtable();
    Shutdown();
    // The two counted pointers' own destructors (Shutdown has cleared both).
    if (device_) device_->Release();
    if (input_) input_->Release();
    vtable_ = PlainRootVtable();
}
EI_REWRITTEN(0x004578D0, CDirectInputMouse::Destruct);

CDirectInputMouse* CDirectInputMouse::DeletingDestruct(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REWRITTEN(0x004578B0, CDirectInputMouse::DeletingDestruct);

CDirectInputMouse* CDirectInputMouse::RootDeletingDestruct(unsigned flags) {
    vtable_ = PlainRootVtable();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REWRITTEN(0x00457950, CDirectInputMouse::RootDeletingDestruct);

// ---- The party manager ----------------------------------------------------------------------------

void PartyManager::Destruct() {
    for (ClassTemplate** p = templates.begin(); p != templates.end(); ++p) ReleaseCounted(*p);
    templates.FreeStorage();
    records.text10.~CString();
    records.text0C.~CString();
    for (PartyRecord** p = records.records.begin(); p != records.records.end(); ++p) ReleaseCounted(*p);
    records.records.FreeStorage();
    for (PartyManagerEntry* entry = entries.begin(); entry != entries.end(); ++entry) entry->Destruct();
    entries.FreeStorage();
    parties.Destroy();   // every node deleted (004ABA70), then the buckets freed
}
EI_REPLACE_METHOD(0x004A5700, PartyManager::Destruct);

PartyManager* PartyManager::Construct() {
    parties.InitZeroedBuckets(100);
    new (&entries) stlp::vector<PartyManagerEntry>();
    new (&records.records) stlp::vector<PartyRecord*>();
    new (&records.text0C) mfc::CString();
    new (&records.text10) mfc::CString();
    new (&templates) stlp::vector<ClassTemplate*>();
    return this;
}
EI_REPLACE_METHOD(0x004A5870, PartyManager::Construct);

// The manager, empty at the start: built by the static initialiser 004A56D0 in the list's order and destroyed by the exit
// function it registers (004A56F0); both ported on the name and registered at their addresses (docs/notes/standalone.md,
// "The static initialisers' objects").
constinit lib::StaticObject<PartyManager> g_partyManager = /*007AA5A0*/ {};
PartyManager* ThePartyManager() { return &*g_partyManager; }

namespace {
void __cdecl ConstructPartyManager() {   // 004A56D0
    g_partyManager->Construct();
    GameAtExit(ei::Code<void(__cdecl*)()>(0x004A56F0));
}
void __cdecl DestroyPartyManager() { g_partyManager->Destruct(); }   // 004A56F0
} // namespace

EI_REPLACE(0x004A56D0, ConstructPartyManager);
EI_REPLACE(0x004A56F0, DestroyPartyManager);
