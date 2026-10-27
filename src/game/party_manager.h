#pragma once

// The party manager: the parties stored by key (the players' parties between zones), a list of entries, the
// named party records and the class templates, saved with the game through a chunk archive and loaded back.
// Names provisional; see docs/notes/script.md ("Leftovers") and docs/notes/world.md ("Party records and
// class templates").

#include "class_templates.h"
#include "party_records.h"

#include "lib/mfc.h"
#include "lib/stlp.h"
#include "lib/stlp_hash.h"
#include "lib/static_object.h"
#include "game_data.h"

#include <cstddef>
#include <cstdint>

class CMemoryStream;
class CParty;

// An entry of the manager's list: two texts and a list of ids. Its copy, assignment and
// destructor are compiler-made (library).
struct PartyManagerEntry {
    mfc::CString name;              // chunk field 1
    mfc::CString text;              // chunk field 2
    stlp::vector<int> ids;          // chunk 3

    // What the destructor does: frees the ids' storage, then destroys the texts.
    void Destruct();
};
static_assert(sizeof(PartyManagerEntry) == 0x14);

// A map node's party: the CParty object itself, constructed and destroyed by the map's own members
using PartyInPlace = PartyStorage;

namespace gamehash {

// hash_map<int, CParty> (the party manager's parties): 0x78-byte nodes. A node's party is copied by
// CParty's copy constructor (it reads its source non-const) and destroyed by its destructor body
// (game_hashes.cpp).
struct PartyElement {
    static void Construct(PartyInPlace* to, const PartyInPlace& from);
    static void Destroy(PartyInPlace* p);
};
using PartyTraits = stlp::DwordObjectTraits<PartyInPlace, PartyElement>;
// Hash_map<int, CParty>::operator[]: a party constructed on the stack, a pair of the key and a copy of it,
// find_or_insert of the pair (a new node copies it again), then the pair's party and the stack party
// destroyed. The node's party.
struct PartyTable : stlp::hashtable<PartyTraits> {
    PartyInPlace* At(const int* key);
};

} // namespace gamehash

// The parties by key (STLport hash_map<int, CParty>; 0x78-byte nodes).
using PartyMap = gamehash::PartyTable;

struct PartyManager {
    PartyMap parties;                           // (LoadParty / StoreParty)
    stlp::vector<PartyManagerEntry> entries;
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
    // freed, the records' two strings, the entries, then every party of the map (the map's own node
    // deleter) and the bucket array.
    void Destruct();
    // The constructor: an empty party map of 193 buckets (the prime after 100), no entries, no records with
    // two empty strings, no templates; field3 is left as it is. It runs from a static initialiser. Returns
    // this.
    PartyManager* Construct();
    // The records' BeginRedeploy / EndRedeploy the script's redeploy.
    void BeginRedeploy(CParty* party);
    void EndRedeploy(CParty* party);
    // Key 0 resets `target` (Reset(1, 0)); else the party stored under the key is copied into it. False when
    // the key is
    // not stored. The login's name and password are not read.
    bool LoadParty(const char* name, const char* password, const int* key, CParty* target) const;
    // Copies `source` into the party stored under the key (operator[] creates it when missing; the copy
    // reads `source` non-const). True.
    bool StoreParty(const int* key, CParty* source);
};
static_assert(offsetof(PartyManager, entries) == 0x14);
static_assert(offsetof(PartyManager, field3) == 0x20);
static_assert(offsetof(PartyManager, records) == 0x24);
static_assert(offsetof(PartyManager, templates) == 0x38);
static_assert(sizeof(PartyManager) == 0x44);
