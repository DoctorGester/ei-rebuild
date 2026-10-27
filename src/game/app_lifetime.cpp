// Creators and destructors the application's start registers or runs:
//  - the creators of the class registry: id 1 a class template, id 2 a named
//    party record; the registry makes these objects when a saved party manager is loaded;
//  - the party record's destructor;
//  - the DirectInput mouse's destructor, run when the application drops the mouse at exit;
//  - the party manager's destructor, which the C runtime runs at exit.
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
#include <memory>
#include <new>

namespace {

// A counted pointer's release: the object deleted with its last reference.
void ReleaseCounted(CRefCounted* object) {
    if (object && --object->refs_ == 0) delete object;
}

} // namespace

// ---- The registry's creators ----------------------------------------------------------------------

// The count dword is cleared only after the allocation has been tested.
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
    Party()->Create();
}

// The name is destroyed after the party's destructor (it is a member).
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
    std::destroy_at(&records.text10);
    std::destroy_at(&records.text0C);
    for (PartyRecord** p = records.records.begin(); p != records.records.end(); ++p) ReleaseCounted(*p);
    records.records.FreeStorage();
    for (PartyManagerEntry* entry = entries.begin(); entry != entries.end(); ++entry) entry->Destruct();
    entries.FreeStorage();
    parties.Destroy();   // every node deleted, then the buckets freed
}
EI_REWRITTEN(0x004A5700, PartyManager::Destruct);

PartyManager* PartyManager::Construct() {
    parties.InitZeroedBuckets(100);
    new (&entries) stlp::vector<PartyManagerEntry>();
    new (&records.records) stlp::vector<PartyRecord*>();
    std::construct_at(&records.text0C);
    std::construct_at(&records.text10);
    new (&templates) stlp::vector<ClassTemplate*>();
    return this;
}
EI_REWRITTEN(0x004A5870, PartyManager::Construct);

// The manager, empty at the start: built by a static initialiser in the list's order and destroyed by the exit
// function it registers (docs/notes/standalone.md, "The static initialisers' objects").
constinit lib::StaticObject<PartyManager> g_partyManager = /*007AA5A0*/ {};
PartyManager* ThePartyManager() { return &*g_partyManager; }

namespace {
void __cdecl DestroyPartyManager() { g_partyManager->Destruct(); }
} // namespace

void __cdecl ConstructPartyManager() {
    g_partyManager->Construct();
    libcrt::AtExit(ei::Code<void(__cdecl*)()>(0x004A56F0));
}

EI_REWRITTEN(0x004A56D0, ConstructPartyManager);
EI_REPLACE(0x004A56F0, DestroyPartyManager);
