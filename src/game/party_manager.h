#pragma once

// The party manager (the object at 007AA5A0): the parties stored by key (the players' parties
// between zones), a list of entries, the named party records and the class templates, saved with
// the game through a chunk archive (004A8560) and loaded back (004A8310). Names provisional; see
// docs/notes/script.md ("Leftovers") and docs/notes/world.md ("Party records and class templates").

#include "class_templates.h"
#include "party_records.h"

#include "lib/mfc.h"
#include "lib/stlp.h"
#include "lib/static_object.h"
#include "game_data.h"

#include <cstddef>
#include <cstdint>

class CMemoryStream;
class CParty;

// An entry of the manager's list (0x14 bytes): two texts and a list of ids. Its copy, assignment
// and destructor are compiler-made (004AD500, 004AE2D0; library).
struct PartyManagerEntry {
    mfc::CString name;              // +0x00 chunk field 1
    mfc::CString text;              // +0x04 chunk field 2
    stlp::vector<int> ids;          // +0x08 chunk 3

    // What 004AE2D0 does: frees the ids' storage, then destroys the texts.
    void Destruct();
};
static_assert(sizeof(PartyManagerEntry) == 0x14);

// A map node's party: the CParty object itself (constructed by the map's own members, 004AB980;
// held as storage here because the class is abstract in its C++ declaration).
struct PartyInPlace {
    alignas(4) std::uint8_t storage[0x70];
    CParty* Get() { return reinterpret_cast<CParty*>(storage); }
};

// The parties by key (STLport hash_map<int, CParty>; 0x78-byte nodes).
using PartyMap = stlp::hash_map<int, PartyInPlace, stlp::IntHash>;

struct PartyManager {
    PartyMap parties;                           // +0x00 (004A8790 / 004A8800 load and store)
    stlp::vector<PartyManagerEntry> entries;    // +0x14
    int field3;                         // +0x20 field 3 of the archive (nothing reads it)
    NamedPartyRecords records;                  // +0x24
    ClassTemplateVector templates;              // +0x38

    // 004A8310 (RET 4). Reads the block `from` holds (CStream::ReadSized) as an archive: chunk 1
    // the parties, 2 the entries, field 3, chunk 4 the class templates, chunk 5 the records.
    void Load(CMemoryStream* from);
    // 004A8560 (RET 4). Writes the same archive into a memory stream (version `to`'s) and appends
    // it to `to` as a sized block (CStream::WriteSized).
    void Save(CMemoryStream* to);
    // 004A5700 (A-E1; app_lifetime.cpp). The destructor the C runtime runs at exit: the templates
    // and the records released and their vectors freed, the records' two strings, the entries, then
    // every party of the map (the map's own node deleter) and the bucket array.
    void Destruct();
    // 004A5870 (A-E1; app_lifetime.cpp). The constructor: an empty party map of 193 buckets (the
    // prime after 100), no entries, no records with two empty strings, no templates; field3 is
    // left as it is. It runs from the static initialiser 004A56D0 on the manager at 007AA5A0,
    // before the hooks are installed, so the hook on it never fires. Returns this.
    PartyManager* Construct();
    // 004A8890 / 004A8880 (party_records.cpp, RET 4): the records' BeginRedeploy (004A6050) / EndRedeploy
    // (004A6360), the script's redeploy.
    void BeginRedeploy(void* players);
    void EndRedeploy(void* players);
    // 004A8790 (RET 0x10, four stack arguments, the first two unused): key 0 resets `target` (00661700(1,
    // 0)); else the party stored under the key is copied into it (00661300). False when the key is not
    // stored. Sets AL only.
    bool LoadParty(int unused1, int unused2, const int* key, void* target) const;
    // 004A8800 (RET 8). Copies `source` into the party stored under the key (operator[] 004A91B0 creates
    // it when missing). True.
    bool StoreParty(const int* key, const void* source);
};
static_assert(offsetof(PartyManager, entries) == 0x14);
static_assert(offsetof(PartyManager, field3) == 0x20);
static_assert(offsetof(PartyManager, records) == 0x24);
static_assert(offsetof(PartyManager, templates) == 0x38);
static_assert(sizeof(PartyManager) == 0x44);
