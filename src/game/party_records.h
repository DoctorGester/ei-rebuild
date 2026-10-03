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
