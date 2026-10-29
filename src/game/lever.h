#pragma once

#include "lock_key.h"
#include "map_object.h"
#include "map_spec.h"   // LeverSetup
#include "maths.h"
#include "unit.h"

#include "lib/static_object.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// A model's record in the lever table, read from Levers.ldb by field id. The spot offsets are in the
// lever's own frame (along its facing's X and Y axes).
struct LeverRecord {
    void Archive(ChunkArchive* archive);
    std::string name;           // field 0
    float spots[2][2];          // field 1: {along, across}
    std::uint32_t spotCount;    // field 2
    float farScale;             // field 3: the spots' distance factor when scale.z is 1
    std::uint32_t duration;     // field 4: a state change's default ticks
    std::string unreadTextFirst;   // field 5; nothing reads it
    std::string sound;          // played while the lever moves
    std::string unreadTextLast;    // field 7; nothing reads it
};

// The lever table: the records and their index by model name.

struct LeverRecordTable {

    // Reads Levers.ldb of <folder>database.res into the records (chunk 1 of a chunk archive) and files
    // every record in the index under its name. Returns 1, or the fatal error's result when the archive
    // or the file does not open.
    int Load(const std::string& folder);
    // The record filed under `name`, or null; like the index's operator[], an unknown name gets a null entry.
    LeverRecord* Named(std::string_view name) { return byName[std::string(name)]; }

    std::vector<LeverRecord> records;
    std::unordered_map<std::string, LeverRecord*> byName;
};
extern LeverRecordTable g_leverTable;   // lever_table.cpp

// Levers ("CLeverObject", class id 0x5D); gates are levers too (zone1's "VGate"). scale.y is the shown
// position (0..1), scale.z a second animation value that scales the spots' distance; flags_ 0x80000
// disables the lever (EnableLever 0). See docs/notes/units.md ("Levers").
class Lever : public CMapObject {
public:
    Lever();
    ~Lever() override;

    void Create() override;
    // The map object's update; on the client, while the move runs, the shown position goes from `from`
    // to `to` and the client world refreshes the object. True while moving, else false.
    bool RunTick() override;
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;
    const char* ClassName() override;
    ClassId GetClassId() override;
    bool IsSynchronised() override { return true; }
    bool IsUsable() override { return NeedsKey(); }
    bool IsDirectUseBlocked() override { return directUseBlocked; }
    // ApplyKey.
    bool ApplyScience(const LockKey* key, CEngineObject* user) override;
    bool AcceptsKey(const LockKey* key) override { return KeyOpens(key); }
    bool KeepsMapFlag() override { return keepsMapFlag != 0; }
    void UseSpots(std::vector<Vector3>* out, float userRadius) override;
    std::uint32_t ReadNetState(int first) override { return ReadLeverNetState(first); }
    void WriteNetState() override { WriteLeverNetState(); }
    float Radius() override { return static_cast<float>(bounds.radius); }

    // Sets state `state` (negative: the next one, wrapping when cycling) and
    // moves the lever there over `duration` ticks (negative: the default stored with the lever table,
    // else 10; zero or less: at once). The units standing around it are lifted off the sector map
    // while it moves and put back on free cells if they cannot stay where they are.
    void SetState(int state, float duration);

    // The spots from which a unit of radius `userRadius` can use the lever, into `out` (emptied first):
    // each offset of the model's record turned into the lever's frame, its length scaled between 1 and
    // the record's farScale by scale.z, plus userRadius. None for an unknown model.
    void CollectUseSpots(std::vector<Vector3>* out, float userRadius);

    // The save record: the map object part, then state, count, cycling, the lock (three dwords) and
    // both flag bytes.
    void LoadLever(CStream* stream);
    void SaveLever(CStream* stream);
    // The map object's network state, then field 5: the lock, from, to, end tick, rate and the disabled
    // bit. A finished move (to != shown position) first restarts from the shown position with rate 0.
    void WriteLeverNetState();
    // Reads what WriteLeverNetState writes: the map object's result and-ed with "the snapshot was
    // consumed", or the map object's result alone without field 5. The client world queues the object;
    // on the first read a moving lever plays its record's sound at its position (8 to 40 units).
    std::uint32_t ReadLeverNetState(int first);

    // The zone loader's set-up: the record's state, count, cycling, lock and flags; the shown position
    // is state / (count - 1) (0 for one state).
    void SetUp(const LeverSetup* setup);
    // The script's EnableLever: clears (on) or sets (off) the disabled bit.
    void Enable(bool on);
    // The script's SetScience.
    void SetLock(const LockKey* lock);

    // 0 when disabled, else whether the lock has a mask.
    bool NeedsKey();
    // The lock's id when its mask has bit 3, else 0.
    std::uint32_t KeyId() override;
    // When `key` opens the lock (Rebirth's FixLeversActivate): the next state at the model's default
    // speed, then disabled; true. Else false.
    bool ApplyKey(const LockKey* key);
    bool KeyOpens(const LockKey* key);

    int state;
    int stateCount;
    int cycling;                    // wraps instead of stopping at the last state
    LockKey lock;
    float from;                     // the shown position when the move started
    float to;                       // the state's position
    float endTick;                  // the world tick at which the move ends
    float rate;                     // 1 / duration (0: no move)
    std::uint8_t directUseBlocked;  // the player cannot use it by clicking (IsDirectUseBlocked; chunk BBAC0007)
    std::uint8_t keepsMapFlag;      // the AI map flag kept while it moves
};

