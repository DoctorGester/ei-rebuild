#pragma once

// Named party records (0x7C bytes, vtable 0073C360, base 0073BB68): a party object and the name
// scripts refer to it by. The object at 007AA5A0 keeps them in a vector at +0x24 followed by two
// strings. Names are provisional.

#include "lib/mfc.h"

#include <cstddef>
#include <cstdint>

struct PartyRecord {
    void* vtable;
    std::int16_t refCount;      // +0x04 counted pointers (the dword is cleared before construction)
    std::int16_t unknown_06;
    char party[0x70];           // +0x08 the party (constructor 00661050)
    mfc::CString name;          // +0x78 "" for the unnamed record
};
static_assert(sizeof(PartyRecord) == 0x7C);
static_assert(offsetof(PartyRecord, name) == 0x78);

// STLport vector of counted pointers, then two strings (007AA5A0 +0x24).
struct PartyRecordList {
    PartyRecord** start;
    PartyRecord** finish;
    PartyRecord** eos;
    mfc::CString text0C;        // +0x0C
    mfc::CString text10;        // +0x10
};

// 004A5BD0 (__thiscall, new game). Releases every record, empties the two strings, adds one
// unnamed record and gives its party the member "Hero" ("Human Hero"), the zone text "gz1g" and
// the variable gtime = 6.
void __fastcall PartyRecordsReset(PartyRecordList* list);
// 004A5E10 (__thiscall). The party (+0x08) of the first record named `name` (_mbscmp), else of the
// last unnamed record, else null.
void* __fastcall PartyRecordsFind(const PartyRecordList* list, void* edx, const char* name);
// 004A5E90 (__thiscall). Appends a new record named `name`.
void __fastcall PartyRecordsAdd(PartyRecordList* list, void* edx, const char* name);
// 004A5FA0 (__thiscall). Removes the records named `name`; after a removal the record moved into
// the slot is skipped (original behaviour).
void __fastcall PartyRecordsRemove(PartyRecordList* list, void* edx, const char* name);

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
// equal to the manager's current name (007AA5A0 +0x34, PartyRecordList::text10) gives the
// player's own party (+0x70), any other the records' party of that owner (PartyRecordsFind).
// Null without a player.
void* __fastcall PartyForScript(void* player, mfc::CString* name);
