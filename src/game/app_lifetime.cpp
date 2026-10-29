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

#include <cstdlib>
#include <cstring>
#include <dinput.h>
#include <memory>
#include <new>

// ---- The registry's creators ----------------------------------------------------------------------

CRefCounted* CreatePartyRecord() {
    auto* record = new PartyRecord();
    record->refs = 0;
    record->flags6 = 0;
    record->flags7 = 0;
    return record;
}

CRefCounted* CreateClassTemplate() {
    auto* created = new ClassTemplate();
    created->refs = 0;
    created->flags6 = 0;
    created->flags7 = 0;
    return created;
}

// ---- The party record -----------------------------------------------------------------------------

PartyRecord::PartyRecord() { party.Create(); }

PartyRecord::~PartyRecord() { party.Destroy(); }

// ---- The DirectInput mouse ------------------------------------------------------------------------


CDirectInputMouse::~CDirectInputMouse() {
    Shutdown();
    // The two counted pointers' own destructors (Shutdown has cleared both).
    if (device) device->Release();
    if (input) input->Release();
}

// ---- The party manager ----------------------------------------------------------------------------

// The manager.
PartyManager g_partyManager;
