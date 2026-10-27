#pragma once

// Named party records (0x7C bytes, a counted object): a party object and the name scripts refer to it by.
// The party manager keeps them in a vector at +0x24 followed by two strings. Names are provisional.

#include "core_refcount.h"
#include "party_storage.h"

#include "lib/mfc.h"
#include "lib/stlp.h"

#include <cstddef>
#include <cstdint>

class CParty;
class ChunkArchive;

class PartyRecord : public CRefCounted {
public:
    // The record's constructor (inlined in its creators; the count is theirs): the party constructed and
    // created, an empty name.
    PartyRecord();
    // Deleting destructor (app_lifetime.cpp): the party destroyed and its destructor, the name.
    ~PartyRecord() override;
    // Serialize: the name as string field 1, the party (SaveParty / LoadParty with variables) as stream
    // field 2.
    void Serialize(ChunkArchive* ar) override;

    PartyStorage party;         // the party
    mfc::CString name;          // "" for the unnamed record

    CParty* Party() { return party.Get(); }
};
static_assert(sizeof(PartyRecord) == 0x7C);
// A new unnamed record with no references (operator new and the constructor: the party constructed and
// created, an empty name). The application's start registers it with the class registry as class 2.
PartyRecord* __cdecl CreatePartyRecord();
static_assert(offsetof(PartyRecord, name) == 0x78);

// The named records (+0x24): an STLport vector of counted pointers, then two strings.
struct NamedPartyRecords {
    // The party (+0x08) of the first record named `name` (_mbscmp), else of the
    // last unnamed record, else null.
    CParty* Find(const char* name) const;
    // Appends a new record named `name`.
    void Add(const char* name);
    // Removes the records named `name`; after a removal the record moved into
    // the slot is skipped (original behaviour).
    void Remove(const char* name);
    stlp::vector<PartyRecord*> records;
    mfc::CString text0C;
    mfc::CString text10;

    // Chunk 1: the records as object references (field 1 per chunk 1; loading releases the old ones first);
    // then the strings as fields 2 and 3.
    void Serialize(ChunkArchive* ar);
    // The redeploy (party_manager.cpp). The record a name picks is found as PartyRecordsFind does (the
    // unnamed one when no record has the name). The record named text10 gets a copy of `party` (SaveParty /
    // LoadParty through a memory stream); then every record's party gets `party`'s variables, zone name and
    // +0x6C.
    void BeginRedeploy(CParty* party);
    // `party` gets a copy of the record named text0C, or is cleared (Clear(1, 0)) when
    // there is none; then text10 = text0C.
    void EndRedeploy(CParty* party);
};
static_assert(sizeof(NamedPartyRecords) == 0x14);

// New game: releases every record, empties the two strings, adds one unnamed record and gives its party the
// member "Hero" ("Human Hero"), the zone text "gz1g" and the variable gtime = 6.
void __fastcall PartyRecordsReset(NamedPartyRecords* list);

// The party manager (party_manager.h). __thiscall. New game: the records reset, then five / four templates.
struct PartyManager;
void __fastcall PartyManagerNewGame(PartyManager* manager);
void __fastcall PartyManagerNewGameFour(PartyManager* manager);
// *out = the global counter, then the counter is raised; returns it.
int __stdcall PartyNextId(int* out);

// The party a script's "owner::name" text refers to: `name` loses its "owner::" prefix; an owner (empty
// without "::") equal to the manager's current name (NamedPartyRecords::text10) gives the player's own
// party (+0x70), any other the records' party of that owner (PartyRecordsFind). Null without a player.
CParty* __fastcall PartyForScript(void* player, mfc::CString* name);
