#pragma once

// The party manager: the parties stored by key (the players' parties between zones), a list of entries, the
// named party records and the class templates, saved with the game through a chunk archive and loaded back.
// Names provisional; see docs/notes/script.md ("Leftovers") and docs/notes/world.md ("Party records and
// class templates").

#include "class_templates.h"
#include "party_records.h"

#include <string>
#include "lib/hash_map.h"
#include "lib/static_object.h"
#include "game_data.h"

#include <cstddef>
#include <cstdint>
#include <vector>

// An entry of the manager's list: two texts and a list of ids. Its copy, assignment and
// destructor are compiler-made (library).
struct PartyManagerEntry {
    std::string name;               // chunk field 1
    std::string text;               // chunk field 2
    std::vector<int> ids;          // chunk 3

    // What the destructor does: frees the ids' storage, then destroys the texts.
    void Destruct();
};

// The parties by key, walked in the game's order (the save's chunks).
using PartyMap = lib::HashMap<int, CParty>;
// The party under `key`, made when missing as the game's operator[] made it: a new party, a copy of it for the
// pair and a copy of that in the map (each copy runs CParty::Create through its save and load).
CParty* PartyAt(PartyMap& parties, int key);

struct PartyManager {
    PartyMap parties;                           // (LoadParty / StoreParty)
    std::vector<PartyManagerEntry> entries;
    int field3;                         // field 3 of the archive (nothing reads it)
    NamedPartyRecords records;
    ClassTemplateVector templates;

    // Reads the block `from` holds (CStream::ReadSized) as an archive: chunk 1
    // the parties, 2 the entries, field 3, chunk 4 the class templates, chunk 5 the records.
    void Load(CMemoryStream* from);
    // Writes the same archive into a memory stream (version `to`'s) and appends
    // it to `to` as a sized block (CStream::WriteSized).
    void Save(CMemoryStream* to);
    // The destructor the C runtime runs at exit: the templates and the records released and their vectors
    // freed, the records' two strings, the entries, then the parties.
    void Destruct();
    // The constructor: no parties (193 buckets), no entries, no records with two empty strings, no templates;
    // field3 is left as it is. It runs from a static initialiser. Returns this.
    PartyManager* Construct();
    // The records' BeginRedeploy / EndRedeploy the script's redeploy.
    void BeginRedeploy(CParty* party);
    void EndRedeploy(CParty* party);
    // Key 0 resets `target` (Reset(1, 0)); else the party stored under the key is copied into it. False when
    // the key is not stored. The login's name and password are not read.
    bool LoadParty(const char* name, const char* password, const int* key, CParty* target) const;
    // Copies `source` into the party stored under the key (PartyAt creates it when missing; the copy
    // reads `source` non-const). True.
    bool StoreParty(const int* key, CParty* source);
};
