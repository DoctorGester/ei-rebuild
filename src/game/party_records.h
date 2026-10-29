#pragma once

// Named party records (0x7C bytes, a counted object): a party object and the name scripts refer to it by.
// The party manager keeps them in a vector followed by two strings. Names are provisional.

#include "core_refcount.h"
#include "party_storage.h"

#include <string>

#include <cstddef>
#include <cstdint>
#include <vector>

class CPlayer;

class PartyRecord : public CRefCounted {
public:
    PartyRecord();
    ~PartyRecord() override;
    // Serialize: the name as string field 1, the party (SaveParty / LoadParty with variables) as stream
    // field 2.
    void Serialize(ChunkArchive* ar) override;

    std::string name;           // "" for the unnamed record
    CParty party;               // destroyed before the name

    CParty* Party() { return &party; }
};
// A new unnamed record with no references (operator new and the constructor: the party constructed and
// created, an empty name). The application's start registers it with the class registry as class 2.
CRefCounted* CreatePartyRecord();

// The named records: counted pointers, then two strings.
struct NamedPartyRecords {
    // The party of the first record named `name` (_mbscmp), else of the last unnamed record, else null.
    CParty* Find(std::string_view name) const;
    // Appends a new record named `name`.
    void Add(const char* name);
    // Removes the records named `name`; after a removal the record moved into
    // the slot is skipped (original behaviour).
    void Remove(const char* name);
    std::vector<HeldRef<PartyRecord>> records;
    std::string requestedName;      // SetCurrentParty's name; EndRedeploy makes it the current one
    std::string currentName;        // the party on stage (the record BeginRedeploy saves it to)

    // Chunk 1: the records as object references (field 1 per chunk 1; loading releases the old ones first);
    // then the strings as fields 2 and 3.
    void Serialize(ChunkArchive* ar);
    // The redeploy (party_manager.cpp). The record a name picks is found as PartyRecordsFind does (the
    // unnamed one when no record has the name). The record named currentName gets a copy of `party` (SaveParty /
    // LoadParty through a memory stream); then every record's party gets `party`'s variables, zone name and
    // +0x6C.
    void BeginRedeploy(CParty* party);
    // `party` gets a copy of the record named requestedName, or is cleared (Clear(1, 0)) when
    // there is none; then currentName = requestedName.
    void EndRedeploy(CParty* party);
};

// New game: releases every record, empties the two strings, adds one unnamed record and gives its party the
// member "Hero" ("Human Hero"), the zone text "gz1g" and the variable gtime = 6.
void PartyRecordsReset(NamedPartyRecords* list);

// The party manager (party_manager.h). New game: the records reset, then five / four templates.
struct PartyManager;
void PartyManagerNewGame(PartyManager* manager);
void PartyManagerNewGameFour(PartyManager* manager);
// *out = the global counter, then the counter is raised; returns it.
int PartyNextId(int* out);

// The party a script's "owner::name" text refers to: `name` loses its "owner::" prefix; an owner (empty
// without "::") equal to the manager's current name (NamedPartyRecords::currentName) gives the player's own
// party, any other the records' party of that owner (PartyRecordsFind). Null without a player.
CParty* PartyForScript(CPlayer* player, std::string* name);
