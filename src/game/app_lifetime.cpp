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

#include <memory>
#include <new>

#include <cstdlib>
#include <dinput.h>

#include <cstring>

namespace {

// A counted pointer's release: the object deleted with its last reference.
void ReleaseCounted(CRefCounted* object) {
    if (object && --object->refs_ == 0) delete object;
}

} // namespace

// ---- The registry's creators ----------------------------------------------------------------------

// The count dword is cleared only after the allocation has been tested.
CRefCounted* CreatePartyRecord() {
    auto* record = new PartyRecord();
    if (!record) return nullptr;
    record->refs_ = 0;
    record->flags6_ = 0;
    record->flags7_ = 0;
    return record;
}

CRefCounted* CreateClassTemplate() {
    auto* created = new ClassTemplate();
    if (!created) return nullptr;
    created->refs_ = 0;
    created->flags6_ = 0;
    created->flags7_ = 0;
    return created;
}

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

// ---- The DirectInput mouse ------------------------------------------------------------------------


CDirectInputMouse::~CDirectInputMouse() {
    Shutdown();
    // The two counted pointers' own destructors (Shutdown has cleared both).
    if (device_) device_->Release();
    if (input_) input_->Release();
}

// ---- The party manager ----------------------------------------------------------------------------

void PartyManager::Destruct() {
    std::destroy_at(&templates);
    std::destroy_at(&records.text10);
    std::destroy_at(&records.text0C);
    std::destroy_at(&records.records);
    std::destroy_at(&entries);
    std::destroy_at(&parties);
}

PartyManager* PartyManager::Construct() {
    std::construct_at(&parties);
    std::construct_at(&entries);
    std::construct_at(&records.records);
    std::construct_at(&records.text0C);
    std::construct_at(&records.text10);
    std::construct_at(&templates);
    return this;
}

// The manager, empty at the start: built by a static initialiser in the list's order and destroyed by the exit
// function it registers (docs/notes/standalone.md, "The static initialisers' objects").
constinit lib::StaticObject<PartyManager> g_partyManager = /*007AA5A0*/ {};
PartyManager* ThePartyManager() { return &*g_partyManager; }

// A second block: it uses g_partyManager (defined above).
namespace {
void DestroyPartyManager() { g_partyManager->Destruct(); }
} // namespace

void ConstructPartyManager() {
    g_partyManager->Construct();
    std::atexit(DestroyPartyManager);
}
