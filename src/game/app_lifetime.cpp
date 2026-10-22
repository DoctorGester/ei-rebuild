// Creators and destructors the application's start (00456440) registers or runs (A-E1):
//  - the creators of the class registry at 0079B270: id 1 a class template (00457850), id 2 a named
//    party record (00457740); the registry makes these objects when a saved party manager is loaded;
//  - the party record's destructor (004577E0, its slot 0 004577C0);
//  - the DirectInput mouse's destructor (004578D0, deleting 004578B0), run when the application
//    drops the mouse at exit;
//  - the party manager's destructor (004A5700), which the C runtime runs at exit on 007AA5A0.
// Main thread. See docs/notes/app.md.
#define DIRECTINPUT_VERSION 0x0700
#include "class_templates.h"
#include "core_refcount.h"
#include "party.h"
#include "party_manager.h"
#include "party_records.h"
#include "ui_cursor.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "lib/stlp.h"
#include "game_hashes.h"
#include "game_data.h"

#include <dinput.h>

#include <cstring>
#include <new>

namespace {

// A counted pointer's release (00439910): the object deleted with its last reference.
void ReleaseCounted(CRefCounted* object) {
    if (object && --object->refs_ == 0) delete object;
}

} // namespace

// ---- The registry's creators ----------------------------------------------------------------------

// The original clears the count dword before testing the allocation; ours tests first.
PartyRecord* __cdecl CreatePartyRecord() {
    auto* record = new PartyRecord();
    if (!record) return nullptr;
    record->refs_ = 0;
    record->flags6_ = 0;
    record->flags7_ = 0;
    return record;
}
EI_REPLACE(0x00457740, CreatePartyRecord);

ClassTemplate* __cdecl CreateClassTemplate() {
    auto* created = new ClassTemplate();
    if (!created) return nullptr;
    created->refs_ = 0;
    created->flags6_ = 0;
    created->flags7_ = 0;
    return created;
}
EI_REPLACE(0x00457850, CreateClassTemplate);

// ---- The party record -----------------------------------------------------------------------------

PartyRecord::PartyRecord() {
    new (Party()) CParty();
    Party()->Create();   // slot 2
}

// The original destroys the name before the party's destructor; ours after (it is a member).
PartyRecord::~PartyRecord() {
    Party()->Destroy();
    Party()->~CParty();
}
EI_REWRITTEN_SPECIAL(0x004577E0, PartyRecord::~PartyRecord);
EI_REWRITTEN_SPECIAL(0x004577C0, PartyRecord::~PartyRecord);

// ---- The DirectInput mouse ------------------------------------------------------------------------

void* CDirectInputMouse::operator new(std::size_t size) noexcept { return gamecrt::operator_new(size); }
void CDirectInputMouse::operator delete(void* block) { gamecrt::operator_delete(block); }

CDirectInputMouse::~CDirectInputMouse() {
    Shutdown();
    // The two counted pointers' own destructors (Shutdown has cleared both).
    if (device_) device_->Release();
    if (input_) input_->Release();
}
EI_REWRITTEN_SPECIAL(0x004578D0, CDirectInputMouse::~CDirectInputMouse);
EI_REWRITTEN_SPECIAL(0x004578B0, CDirectInputMouse::~CDirectInputMouse);

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
EI_REWRITTEN(0x004A5700, PartyManager::Destruct);

PartyManager* PartyManager::Construct() {
    parties.InitZeroedBuckets(100);
    new (&entries) stlp::vector<PartyManagerEntry>();
    new (&records.records) stlp::vector<PartyRecord*>();
    new (&records.text0C) mfc::CString();
    new (&records.text10) mfc::CString();
    new (&templates) stlp::vector<ClassTemplate*>();
    return this;
}
EI_REWRITTEN(0x004A5870, PartyManager::Construct);

// The manager, empty at the start: built by the static initialiser 004A56D0 in the list's order and destroyed by the exit
// function it registers (004A56F0); both ported on the name and registered at their addresses (docs/notes/standalone.md,
// "The static initialisers' objects").
constinit lib::StaticObject<PartyManager> g_partyManager = /*007AA5A0*/ {};
PartyManager* ThePartyManager() { return &*g_partyManager; }

namespace {
void __cdecl ConstructPartyManager() {   // 004A56D0
    g_partyManager->Construct();
    libcrt::AtExit(ei::Code<void(__cdecl*)()>(0x004A56F0));
}
void __cdecl DestroyPartyManager() { g_partyManager->Destruct(); }   // 004A56F0
} // namespace

EI_REPLACE(0x004A56D0, ConstructPartyManager);
EI_REPLACE(0x004A56F0, DestroyPartyManager);
