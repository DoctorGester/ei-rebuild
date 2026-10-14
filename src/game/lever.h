#pragma once

#include "maths.h"
#include "map_object.h"
#include "unit.h"   // LockKey

#include "lib/stlp.h"

#include <cstddef>
#include <cstdint>

class CStream;   // core_streams.h

// The spots a use order may go to (an MFC CArray<Vector3>, vtable 0073C120; its SetAtGrow is
// 0052CC50). combat_planning.cpp's SpotArray is the same array.
struct LeverSpots {
    void* vtable;
    Vector3* data;      // +0x04
    int size;           // +0x08
    int capacity;       // +0x0C
    int growBy;         // +0x10
};
static_assert(sizeof(LeverSpots) == 0x14);

// A model's record in the lever table (0079C1A0: 0x2C bytes, read from Levers.ldb by 00463030,
// whose field ids are given): the spot offsets are in the lever's own frame (along its facing's
// X and Y axes).
struct LeverRecord {
    void* name;                 // +0x00 CString (field 0)
    float spots[2][2];          // +0x04 (field 1) {along, across}
    std::uint32_t spotCount;    // +0x14 (2)
    float farScale;             // +0x18 (3) the spots' distance factor at +0xCC = 1
    std::uint32_t duration;     // +0x1C (4) a state change's default ticks (SetState)
    void* text20;               // +0x20 CStrings (5-7)
    const char* sound;          // +0x24 played while the lever moves (ReadLeverNetState)
    void* text28;               // +0x28
};
static_assert(sizeof(LeverRecord) == 0x2C);

// The lever table (0079C1A0; lever_table.cpp); 0052CA40 finds a record (originals_units.h).
inline void* LeverTable() { return reinterpret_cast<void*>(0x0079C1A0); }

// The lever table's layout: the records (an STLport vector) and their index by model name. Its
// constructor (004632A0) is a static initialiser, its destructor 00463120 (lever_table.cpp).
struct LeverRecordTable {
    using NameIndex = stlp::hash_map<stlp::string, LeverRecord*, stlp::StringHash>;

    // 00463390 (RET 4; lever_table_load.cpp, A-E1; Rebirth-patched, so the vanilla launch only).
    // Reads Levers.ldb of <folder>database.res into the records (chunk 1 of a chunk archive) and
    // enters every record into the index under its name. Returns 1, or the fatal error's result
    // when the archive or the file does not open.
    int Load(const char* folder);
    // 004632A0 (lever_table_load.cpp, A-E1). The constructor: no records, an empty index of 193
    // buckets (the prime after 100). It runs from the static initialiser 004630F0 on the table at
    // 0079C1A0, before the hooks are installed, so the hook on it never fires. Returns this.
    LeverRecordTable* Construct();

    LeverRecord* begin;             // +0x00
    LeverRecord* end;               // +0x04
    LeverRecord* endOfStorage;      // +0x08
    NameIndex byName;               // +0x0C
};
static_assert(offsetof(LeverRecordTable, byName) == 0x0C && sizeof(LeverRecordTable) == 0x20);

// The lever part of a zone file's lever record (map_spec.h LeverSpec + 0x8C), what SetUp reads.
struct LeverSetup {
    LockKey lock;                   // +0x00
    int state;                      // +0x0C
    int stateCount;                 // +0x10
    std::uint8_t flag260;           // +0x14
    std::uint8_t keepsMapFlag;      // +0x15
    std::uint8_t cycling;           // +0x16
};
static_assert(offsetof(LeverSetup, cycling) == 0x16);

// Levers (GetClassName "CLeverObject", class id 0x5D, 0x264 bytes, vtable 0073E740; made by the
// factory 00512730; gates are levers too, e.g. zone1's "VGate", id 1889). A map object whose
// scale's y (+0xC8) is the shown position (0..1) and z (+0xCC) a second animation value that
// scales the spots' distance. flags_ 0x80000: disabled (EnableLever 0). Names provisional; see
// docs/notes/units.md ("Levers"). unit_lever.cpp: SetState, CollectUseSpots; lever_object.cpp the rest.
class Lever : public CMapObject {
public:
    // 0052B7A0 (RET 8). Sets state `state` (negative: the next one, wrapping when cycling) and
    // moves the lever there over `duration` ticks (negative: the model's default from 0079C1AC,
    // else 10; zero or less: at once). The units standing around it are lifted off the sector map
    // while it moves and put back on free cells if they cannot stay where they are.
    void SetState(int state, float duration);

    // 0052BBE0 (RET 8; the lever's virtual slot 44, vtable 0073E740). The spots from which a unit
    // of radius `userRadius` can use the lever, into `out` (emptied first): for each offset
    // (along, across) of the model's record in the lever table (0079C1A0, by +0xA4; none for an
    // unknown model), the position plus the facing's X axis times `along` and its Y axis times
    // `across`, the offset normalised and set at distance
    // ((1 - +0xCC) + +0xCC * record scale) * |offset| + userRadius.
    void CollectUseSpots(LeverSpots* out, float userRadius);

    // lever_object.cpp (A-U6).
    // 0052B1F0. The constructor: the map object (0050E2D0), the lock {1, 0, 0}, the vtable, the
    // map object's Create (0050E770), the animation cleared, 6 network fields (0050FA30), then
    // the lock again, state 0 of 1, not cycling, both flags 0. Returns this.
    Lever* Construct();
    // 0052B320. The destructor body: the vtable, the map object's Destroy (0050E840) and
    // destructor body (0050E660). 0052B300 (slot 0, RET 4): it, then operator delete when `flags`
    // bit 0 is set; returns this.
    void DestructBody();
    Lever* DeletingDestruct(int flags);
    // 0052B370 (slot 2, Create). The map object's Create, then as the constructor's tail. Returns
    // the map object's result.
    int CreateLever();
    // 0052B490 (slot 6, the update). The map object's update; then on the client only (the
    // context's slot 26 is 0): while the move runs (t = (end tick - tick) * rate above 0) the
    // shown position is to - (to - from) * t, else `to` when it differs; the scale is applied
    // (00517700) and the client world refreshes the object (005651A0). Returns 1 while moving,
    // else 0.
    int UpdateLever();
    // 0052BDD0 / 0052BE40 (slots 9 / 10, RET 4). The map object part (0050F7A0 / 0050F900), then
    // state, count, cycling, the lock (three dwords; 005229F0 / 00522A30) and both flag bytes.
    void LoadLever(CStream* stream);
    void SaveLever(CStream* stream);
    // 0052B560 (slot 50). The map object's network state (0050EAF0), then field 5: a finished
    // move (to != shown position) restarts from the shown position with rate 0; the lock, from,
    // to, end tick, rate and the disabled bit.
    void WriteLeverNetState();
    // 0052B650 (slot 49, RET 4). Reads what WriteLeverNetState writes; the result is the map
    // object's and-ed with "the snapshot was consumed". The client world queues the object
    // (0056D7E0); on the first read a moving lever plays its model's sound (lever table record
    // +0x24) at its position (8 to 40 units). Returns the map object's result without field 5.
    std::uint32_t ReadLeverNetState(int first);

    // 0052B3F0 (RET 4; the zone loader 005927C0). Copies the record's state, count, cycling, lock
    // and flags; the shown position is state / (count - 1) (0 for one state); MarkChanged(1).
    // Returns MarkChanged's EAX.
    std::uintptr_t SetUp(const LeverSetup* setup);
    // 0052BAD0 (RET 4; EnableLever). Clears (on) or sets (off) the disabled bit, MarkChanged(5).
    std::uintptr_t Enable(bool on);
    // 0052BB20 (RET 4; SetScience). The lock becomes `lock`, MarkChanged(5).
    std::uintptr_t SetLock(const LockKey* lock);

    // Small slots.
    bool Slot29True();                  // 0052B2B0 (slot 29): AL = 1
    int Flag260Slot();                  // 0052B2D0 (slot 39): the byte +0x260
    bool KeepsMapFlagSlot();            // 0052B2C0 (slot 43): AL = the byte +0x261
    // 0052BB00 (slot 38). 0 when disabled, else whether the lock has a mask.
    int NeedsKey();
    // 0052BBC0 (slot 40). The lock's id when its mask has bit 3, else 0.
    std::uint32_t KeyId() override;
    // 0052BB50 (slot 41, RET 8: ApplyScience). When `key` opens the lock (00522A80, Rebirth's
    // FixLeversActivate, through its address): the next state (SetState(-1, -10)), disabled,
    // MarkChanged(5); AL 1. Else 0. The second argument is not read.
    bool ApplyKey(const LockKey* key, int unused);
    // 0052BBA0 (slot 42, RET 4). Whether `key` opens the lock (00522A80's EAX).
    BOOL KeyOpens(const LockKey* key);
    // 0052B3E0 (slot 60). The radius (+0x134), in ST0.
    double RadiusSlot();

    int state;                      // +0x238
    int stateCount;                 // +0x23C
    int cycling;                    // +0x240 wraps instead of stopping at the last state
    LockKey lock;                   // +0x244
    float from;                     // +0x250 the shown position when the move started
    float to;                       // +0x254 the state's position
    float endTick;                  // +0x258 the world tick at which the move ends
    float rate;                     // +0x25C 1 / duration (0: no move)
    std::uint8_t flag260;           // +0x260 (record +0x14)
    std::uint8_t keepsMapFlag;      // +0x261 the AI map flag kept while it moves (SetState)
    std::uint8_t unused_262[2];
};
static_assert(offsetof(Lever, state) == 0x238);
static_assert(offsetof(Lever, lock) == 0x244);
static_assert(offsetof(Lever, from) == 0x250);
static_assert(offsetof(Lever, rate) == 0x25C);
static_assert(offsetof(Lever, keepsMapFlag) == 0x261);
static_assert(sizeof(Lever) == 0x264);

// 0052B2E0 / 0052B2F0 (slots 11 / 12): "CLeverObject" (0078F788) and 0x5D.
const char* __fastcall LeverClassName(Lever* self);
int __fastcall LeverClassId(Lever* self);
