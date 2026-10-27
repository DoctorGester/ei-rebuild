#pragma once

#include "maths.h"
#include "lib/crt.h"
#include "map_object.h"

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)
#include "lock_key.h"
#include "map_spec.h"   // LeverSetup
#include "unit.h"

#include "lib/mfc.h"
#include "lib/stlp.h"
#include "lib/static_object.h"
#include "game_data.h"

#include <cstddef>
#include <cstdint>

class CStream;   // core_streams.h

// The spots a use order may go to (map_object.h's UseSpotArray).
using LeverSpots = UseSpotArray;

// A model's record in the lever table (0x2C bytes, read from Levers.ldb,
// whose field ids are given): the spot offsets are in the lever's own frame (along its facing's
// X and Y axes).
struct LeverRecord {
    LeverRecord* Assign(const LeverRecord* from);
    LeverRecord* CopyConstruct(const LeverRecord* from);
    LeverRecord* DeleteScalar(unsigned flags);
    void Archive(ChunkArchive* archive);
    mfc::CString name;          // +0x00 (field 0)
    float spots[2][2];          // +0x04 (field 1) {along, across}
    std::uint32_t spotCount;    // +0x14 (2)
    float farScale;             // +0x18 (3) the spots' distance factor at +0xCC = 1
    std::uint32_t duration;     // +0x1C (4) a state change's default ticks (SetState)
    mfc::CString text20;        // +0x20 (5-7)
    mfc::CString sound;         // +0x24 played while the lever moves (ReadLeverNetState)
    mfc::CString text28;        // +0x28
};
static_assert(sizeof(LeverRecord) == 0x2C);

// The lever table's layout: the records (an STLport vector) and their index by model name. Its
// constructor is a static initialiser, its destructor in lever_table.cpp.
// (The archive's helper for the records is ChunkArchive::SerializeLevers, app_device_list.cpp.)

struct LeverRecordTable {
    using NameIndex = stlp::hash_map<stlp::string, LeverRecord*, stlp::StringHash>;

    // Load (lever_table_load.cpp; Rebirth patches it, so the vanilla launch only).
    // Reads Levers.ldb of <folder>database.res into the records (chunk 1 of a chunk archive) and
    // enters every record into the index under its name. Returns 1, or the fatal error's result
    // when the archive or the file does not open.
    int Load(const char* folder);
    // (lever_table_load.cpp). The constructor: no records, an empty index of 193
    // buckets (the prime after 100). It runs from a static initialiser on the table,
    // before the hooks are installed, so the hook on it never fires. Returns this.
    LeverRecordTable* Construct();
    // The record filed under `name`, null when there is none (the index's operator[]:
    // a null record is filed under a new name).
    LeverRecord* Named(const char* name) { return stlp::NamedSlot<LeverRecord>(byName, stlp::string(name)); }

    LeverRecord* begin;             // +0x00
    LeverRecord* end;               // +0x04
    LeverRecord* endOfStorage;      // +0x08
    NameIndex byName;               // +0x0C
};
static_assert(offsetof(LeverRecordTable, byName) == 0x0C && sizeof(LeverRecordTable) == 0x20);
// The lever table (lever_table.cpp).
extern lib::StaticObject<LeverRecordTable> g_leverTable;
inline LeverRecordTable& TheLeverTable() { return *g_leverTable; }

// Levers (GetClassName "CLeverObject", class id 0x5D, 0x264 bytes; made by the factory;
// gates are levers too, e.g. zone1's "VGate", id 1889; a real virtual class: the compiler makes its
// vtable). A map object whose
// scale's y (+0xC8) is the shown position (0..1) and z (+0xCC) a second animation value that
// scales the spots' distance. flags_ 0x80000: disabled (EnableLever 0). Names provisional; see
// docs/notes/units.md ("Levers"). unit_lever.cpp: SetState, CollectUseSpots; lever_object.cpp the rest.
class Lever : public CMapObject {
public:
    // The map object, the lock {1, 0, 0}, the map object's Create,
    // the animation cleared, 6 network fields, then the lock again, state 0 of 1, not
    // cycling, both flags 0.
    Lever();
    // The map object's Destroy, then its destructor.
    ~Lever() override;
    // The deleting destructor (slot 0) frees into the game's heap.

    int Create() override { return CreateLever(); }
    int RunTick() override { return UpdateLever(); }
    void Load(CStream* stream) override;                      // 9: LoadLever
    void Save(CStream* stream) override;                      // 10: SaveLever
    const char* GetClassName() override;                   // 11: "CLeverObject"
    int GetClassId() override;                             // 12: 0x5D
    bool IsSynchronised() override { return Slot29True(); }        // 29
    int IsUsable() override { return NeedsKey(); }                 // 38
    int IsDirectUseBlocked() override { return Flag260Slot(); }    // 39
    // 41: ApplyKey. The first argument is the key record (combat_actions.cpp passes its address).
    bool ApplyScience(int first, int second) override;
    int AcceptsKey(const LockKey* key) override { return KeyOpens(key); }   // 42
    bool KeepsMapFlag() override { return KeepsMapFlagSlot(); }    // 43
    void UseSpots(UseSpotArray* out, float userRadius) override;           // 44: CollectUseSpots
    std::uint32_t ReadNetState(int first) override { return ReadLeverNetState(first); }   // 49
    void WriteNetState() override { WriteLeverNetState(); }        // 50
    float Radius() override { return static_cast<float>(RadiusSlot()); }   // 60

    // Sets state `state` (negative: the next one, wrapping when cycling) and
    // moves the lever there over `duration` ticks (negative: the default stored with the lever table,
    // else 10; zero or less: at once). The units standing around it are lifted off the sector map
    // while it moves and put back on free cells if they cannot stay where they are.
    void SetState(int state, float duration);

    // The lever's virtual slot 44. The spots from which a unit
    // of radius `userRadius` can use the lever, into `out` (emptied first): for each offset
    // (along, across) of the model's record in the lever table (by +0xA4; none for an
    // unknown model), the position plus the facing's X axis times `along` and its Y axis times
    // `across`, the offset normalised and set at distance
    // ((1 - +0xCC) + +0xCC * record scale) * |offset| + userRadius.
    void CollectUseSpots(LeverSpots* out, float userRadius);

    // Slot 2 (Create). The map object's Create, then as the constructor's tail. Returns
    // the map object's result.
    int CreateLever();
    // Slot 6 (the update). The map object's update; then on the client only (the
    // context's slot 26 is 0): while the move runs (t = (end tick - tick) * rate above 0) the
    // shown position is to - (to - from) * t, else `to` when it differs; the scale is applied
    // and the client world refreshes the object. Returns 1 while moving,
    // else 0.
    int UpdateLever();
    // Slots 9 / 10. The map object part, then
    // state, count, cycling, the lock (three dwords; LockKey's Load / Save) and both flag bytes.
    void LoadLever(CStream* stream);
    void SaveLever(CStream* stream);
    // The map object's network state, then field 5: a finished
    // move (to != shown position) restarts from the shown position with rate 0; the lock, from,
    // to, end tick, rate and the disabled bit.
    void WriteLeverNetState();
    // Reads what WriteLeverNetState writes; the result is the map
    // object's and-ed with "the snapshot was consumed". The client world queues the object
    // on the first read a moving lever plays its model's sound (lever table record
    // +0x24) at its position (8 to 40 units). Returns the map object's result without field 5.
    std::uint32_t ReadLeverNetState(int first);

    // The zone loader's set-up. Copies the record's state, count, cycling, lock
    // and flags; the shown position is state / (count - 1) (0 for one state); MarkChanged(1).
    // Returns MarkChanged's EAX.
    std::uintptr_t SetUp(const LeverSetup* setup);
    // EnableLever. Clears (on) or sets (off) the disabled bit, MarkChanged(5).
    std::uintptr_t Enable(bool on);
    // SetScience. The lock becomes `lock`, MarkChanged(5).
    std::uintptr_t SetLock(const LockKey* lock);

    // Small slots.
    bool Slot29True();                  // AL = 1
    int Flag260Slot();                  // the byte +0x260
    bool KeepsMapFlagSlot();            // AL = the byte +0x261
    // 0 when disabled, else whether the lock has a mask.
    int NeedsKey();
    // The lock's id when its mask has bit 3, else 0.
    std::uint32_t KeyId() override;
    // Slot 41 (ApplyScience). When `key` opens the lock (Rebirth's
    // FixLeversActivate, through its address): the next state (SetState(-1, -10)), disabled,
    // MarkChanged(5); AL 1. Else 0. The second argument is not read.
    bool ApplyKey(const LockKey* key, int unused);
    // Whether `key` opens the lock (the check's EAX).
    BOOL KeyOpens(const LockKey* key);
    // The radius (+0x134), in ST0.
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

// Slots 11 / 12: "CLeverObject" and 0x5D.
const char* __fastcall LeverClassName(Lever* self);
int __fastcall LeverClassId(Lever* self);
