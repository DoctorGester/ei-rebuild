#pragma once

// Named party records (0x7C bytes, vtable 0073C360, base 0073BB68): a party object and the name
// scripts refer to it by. The object at 007AA5A0 keeps them in a vector at +0x24 followed by two
// strings. Names are provisional.

#include "lib/mfc.h"
#include "lib/stlp.h"

#include <cstddef>
#include <cstdint>

class CParty;
class ChunkArchive;

struct PartyRecord {
    void* vtable;
    std::int16_t refCount;      // +0x04 counted pointers (the dword is cleared before construction)
    std::int16_t refsHigh;              // +0x06 the reference dword's high half (zeroed with it)
    char party[0x70];           // +0x08 the party (constructor 00661050)
    mfc::CString name;          // +0x78 "" for the unnamed record

    // 004A5AF0 (virtual slot 1 Serialize, RET 4; party_manager.cpp). The name as string field 1,
    // the party (SaveParty / LoadParty with variables) as stream field 2.
    void Serialize(ChunkArchive* ar);
    // 004577E0 (A-E1; app_lifetime.cpp). Destructor body: the vtable, the party destroyed (slot
    // 3), the name, the party's destructor body, the root vtable.
    void Destruct();
    // 004577C0 (slot 0, RET 4). Destruct, then operator delete when bit 0 of `flags` is set.
    PartyRecord* DeletingDestruct(unsigned flags);
};
static_assert(sizeof(PartyRecord) == 0x7C);
// 00457740 (A-E1; app_lifetime.cpp). A new unnamed record with no references (operator new and the
// constructor: the party constructed and created, an empty name). The application's start
// registers it with the class registry at 0079B270 as class 2.
PartyRecord* __cdecl CreatePartyRecord();
static_assert(offsetof(PartyRecord, name) == 0x78);

// The named records (007AA5A0 +0x24): an STLport vector of counted pointers, then two strings.
struct NamedPartyRecords {
    // 004A5E10 (__thiscall). The party (+0x08) of the first record named `name` (_mbscmp), else of the
    // last unnamed record, else null.
    void* Find(const char* name) const;
    // 004A5E90 (__thiscall). Appends a new record named `name`.
    void Add(const char* name);
    // 004A5FA0 (__thiscall). Removes the records named `name`; after a removal the record moved into
    // the slot is skipped (original behaviour).
    void Remove(const char* name);
    stlp::vector<PartyRecord*> records;
    mfc::CString text0C;        // +0x0C
    mfc::CString text10;        // +0x10

    // 004A64B0 (RET 4; party_manager.cpp). Chunk 1: the records as object references (field 1 per
    // chunk 1; loading releases the old ones first); then the strings as fields 2 and 3.
    void Serialize(ChunkArchive* ar);
    // The redeploy (004A8890 / 004A8880; party_manager.cpp), RET 4 each. The record a name picks is
    // found as PartyRecordsFind does (the unnamed one when no record has the name).
    // 004A6050. The record named text10 gets a copy of `party` (SaveParty / LoadParty through a
    // memory stream); then every record's party gets `party`'s variables, zone name and +0x6C.
    void BeginRedeploy(CParty* party);
    // 004A6360. `party` gets a copy of the record named text0C, or is cleared (Clear(1, 0)) when
    // there is none; then text10 = text0C.
    void EndRedeploy(CParty* party);
};
static_assert(sizeof(NamedPartyRecords) == 0x14);

// 004A5BD0 (__thiscall, new game). Releases every record, empties the two strings, adds one
// unnamed record and gives its party the member "Hero" ("Human Hero"), the zone text "gz1g" and
// the variable gtime = 6.
void __fastcall PartyRecordsReset(NamedPartyRecords* list);

// The party manager (007AA5A0): records at +0x24, class templates at +0x38. __thiscall.
// 004A88A0 / 004A88C0: new game; the records reset (004A5BD0), then five / four templates (004A7630).
void __fastcall PartyManagerNewGame(void* manager);
void __fastcall PartyManagerNewGameFour(void* manager);
// (004A8890 / 004A8880 are PartyManager::BeginRedeploy / EndRedeploy.)
// 004A8910 (__stdcall): *out = the counter at 0078B0E8, then the counter is raised; returns it.
int __stdcall PartyNextId(int* out);
// (004A8790 / 004A8800 are PartyManager::LoadParty / StoreParty.)

// 004BAEC0 (__fastcall: ECX player, EDX name; script_runtime_helpers.cpp). The party a script's
// "owner::name" text refers to: `name` loses its "owner::" prefix; an owner (empty without "::")
// equal to the manager's current name (007AA5A0 +0x34, NamedPartyRecords::text10) gives the
// player's own party (+0x70), any other the records' party of that owner (PartyRecordsFind).
// Null without a player.
void* __fastcall PartyForScript(void* player, mfc::CString* name);
