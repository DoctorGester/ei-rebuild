#pragma once

// The party manager (the object at 007AA5A0): the parties stored by key (the players' parties
// between zones), a list of entries, the named party records and the class templates, saved with
// the game through a chunk archive (004A8560) and loaded back (004A8310). Names provisional; see
// docs/notes/script.md ("A-S9") and docs/notes/world.md ("Party records and class templates").

#include "class_templates.h"
#include "party_records.h"

#include "lib/mfc.h"
#include "lib/stlp.h"

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
    int unknown_20;                             // +0x20 field 3 of the archive
    NamedPartyRecords records;                  // +0x24
    ClassTemplateVector templates;              // +0x38

    // 004A8310 (RET 4). Reads the block `from` holds (CStream::ReadSized) as an archive: chunk 1
    // the parties, 2 the entries, field 3, chunk 4 the class templates, chunk 5 the records.
    void Load(CMemoryStream* from);
    // 004A8560 (RET 4). Writes the same archive into a memory stream (version `to`'s) and appends
    // it to `to` as a sized block (CStream::WriteSized).
    void Save(CMemoryStream* to);
};
static_assert(offsetof(PartyManager, entries) == 0x14);
static_assert(offsetof(PartyManager, unknown_20) == 0x20);
static_assert(offsetof(PartyManager, records) == 0x24);
static_assert(offsetof(PartyManager, templates) == 0x38);

// 007AA5A0: the game's party manager.
inline PartyManager* GamePartyManager() { return reinterpret_cast<PartyManager*>(std::uintptr_t(0x007AA5A0)); }
