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

void* const kCountedRootVtable = ei::GamePointer<void>(0x0073BB68);
void* const kPartyRecordVtable = ei::GamePointer<void>(0x0073C360);
void* const kMouseVtable = ei::GamePointer<void>(0x0073C350);
void* const kMouseRootVtable = ei::GamePointer<void>(0x0073C368);

CParty* PartyOf(PartyRecord* record) { return reinterpret_cast<CParty*>(record->party); }

// A counted pointer's release (00439910): the object deleted with its last reference.
template <typename T>
void ReleaseCounted(T* object) {
    if (object) reinterpret_cast<CRefObject*>(object)->Release();
}

} // namespace

// ---- The registry's creators ----------------------------------------------------------------------

PartyRecord* __cdecl CreatePartyRecord() {
    auto* record = static_cast<PartyRecord*>(gamecrt::operator_new(sizeof(PartyRecord)));
    // The count's dword is cleared before the null check, as the original does.
    record->refCount = 0;
    record->refsHigh = 0;
    if (!record) return nullptr;
    record->vtable = kCountedRootVtable;
    PartyOf(record)->Construct();
    new (&record->name) mfc::CString();
    record->vtable = kPartyRecordVtable;
    PartyOf(record)->Create();
    return record;
}
EI_REPLACE(0x00457740, CreatePartyRecord);

ClassTemplate* __cdecl CreateClassTemplate() {
    auto* created = static_cast<ClassTemplate*>(gamecrt::operator_new(sizeof(ClassTemplate)));
    created->refCount = 0;
    created->refsHigh = 0;
    if (!created) return nullptr;
    std::memset(created->weightsTail, 0, sizeof(created->weightsTail));   // SV7b2: never set, saved
    return created->Construct();
}
EI_REPLACE(0x00457850, CreateClassTemplate);

// ---- The party record -----------------------------------------------------------------------------

void PartyRecord::Destruct() {
    vtable = kPartyRecordVtable;
    PartyOf(this)->Destroy();
    name.~CString();
    PartyOf(this)->DestructBody();
    vtable = kCountedRootVtable;
}
EI_REWRITTEN(0x004577E0, PartyRecord::Destruct);

PartyRecord* PartyRecord::DeletingDestruct(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x004577C0, PartyRecord::DeletingDestruct);

// ---- The DirectInput mouse ------------------------------------------------------------------------

void CDirectInputMouse::Destruct() {
    vtable_ = kMouseVtable;
    Shutdown();
    // The two counted pointers' own destructors (Shutdown has cleared both).
    if (device_) device_->Release();
    if (input_) input_->Release();
    vtable_ = kMouseRootVtable;
}
EI_REWRITTEN(0x004578D0, CDirectInputMouse::Destruct);

CDirectInputMouse* CDirectInputMouse::DeletingDestruct(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x004578B0, CDirectInputMouse::DeletingDestruct);

// 00457950: slot 0 of the mouse's root class (vtable 0073C368; one slot): the vtable stored, then
// operator delete when bit 0 of `flags` is set. Returns the object.
void* __fastcall CountedRootDeletingDestruct(void* self, void*, unsigned flags) {
    *static_cast<void**>(self) = kMouseRootVtable;
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}
EI_REPLACE(0x00457950, CountedRootDeletingDestruct);

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
    parties.ClearWith([this](PartyMap::Node* node) {   // 004ABA70
        gamehash::PartyAbi::DeleteNode(reinterpret_cast<gamehash::PartyTable*>(&parties), nullptr,
                                       reinterpret_cast<gamehash::PartyAbi::Node*>(node));
    });
    static_cast<stlp::vector<void*>*>(parties.BucketVector())->FreeStorage();
}
EI_REPLACE_METHOD(0x004A5700, PartyManager::Destruct);

PartyManager* PartyManager::Construct() {
    parties.InitBuckets(100);
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
