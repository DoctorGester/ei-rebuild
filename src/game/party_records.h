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
    std::int16_t unknown_06;
    char party[0x70];           // +0x08 the party (constructor 00661050)
    mfc::CString name;          // +0x78 "" for the unnamed record

    // 004A5AF0 (virtual slot 1 Serialize, RET 4; party_manager.cpp). The name as string field 1,
    // the party (SaveParty / LoadParty with variables) as stream field 2.
    void Serialize(ChunkArchive* ar);
};
static_assert(sizeof(PartyRecord) == 0x7C);
static_assert(offsetof(PartyRecord, name) == 0x78);

// The named records (007AA5A0 +0x24): an STLport vector of counted pointers, then two strings.
struct NamedPartyRecords {
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
// 004A5E10 (__thiscall). The party (+0x08) of the first record named `name` (_mbscmp), else of the
// last unnamed record, else null.
void* __fastcall PartyRecordsFind(const NamedPartyRecords* list, void* edx, const char* name);
// 004A5E90 (__thiscall). Appends a new record named `name`.
void __fastcall PartyRecordsAdd(NamedPartyRecords* list, void* edx, const char* name);
// 004A5FA0 (__thiscall). Removes the records named `name`; after a removal the record moved into
// the slot is skipped (original behaviour).
void __fastcall PartyRecordsRemove(NamedPartyRecords* list, void* edx, const char* name);

// The party manager (007AA5A0): records at +0x24, class templates at +0x38. __thiscall.
// 004A88A0 / 004A88C0: new game; the records reset (004A5BD0), then five / four templates (004A7630).
void __fastcall PartyManagerNewGame(void* manager);
void __fastcall PartyManagerNewGameFour(void* manager);
// 004A8890 / 004A8880: 004A6050 / 004A6360 on the records (the script's redeploy).
void __fastcall PartyManagerBeginRedeploy(void* manager, void* edx, void* players);
void __fastcall PartyManagerEndRedeploy(void* manager, void* edx, void* players);
// 004A8910 (__stdcall): *out = the counter at 0078B0E8, then the counter is raised; returns it.
int __stdcall PartyNextId(int* out);
// 004A8790 (__thiscall on a hash_map<int, Party>, four stack arguments, the first two unused):
// key 0 resets `target` (00661700(1, 0)); else the party stored under the key is copied into it
// (00661300). False when the key is not stored. Sets AL only.
bool __fastcall PartyMapLoad(const void* map, void* edx, int unused1, int unused2, const int* key, void* target);
// 004A8800 (__thiscall). Copies `source` into the party stored under the key (operator[] 004A91B0
// creates it when missing). True.
bool __fastcall PartyMapStore(void* map, void* edx, const int* key, const void* source);

// 004BAEC0 (__fastcall: ECX player, EDX name; script_runtime_helpers.cpp). The party a script's
// "owner::name" text refers to: `name` loses its "owner::" prefix; an owner (empty without "::")
// equal to the manager's current name (007AA5A0 +0x34, NamedPartyRecords::text10) gives the
// player's own party (+0x70), any other the records' party of that owner (PartyRecordsFind).
// Null without a player.
void* __fastcall PartyForScript(void* player, mfc::CString* name);
