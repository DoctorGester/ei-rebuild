#pragma once

// Placed traps (GetClassName "CTrapObject", class id 0x55, 0x23C bytes, made by the factory 00512730
// for kind 0x3010; a real virtual class: the compiler makes its vtable, C19, game.exe's 0073E548 is
// retired): the world arms one when a trap item is used (0058ECE0 calls
// Arm); while armed on the server, the first unit within 1 of it sets it off: a world script
// "CreateParticleSource( 100, x, y, z, 1, "Mushroom")" at its position and the trap disarms.
// Names provisional; trap_object.cpp (A-U6).

#include "map_object.h"
#include "lib/crt.h"

#include <cstddef>
#include <cstdint>

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

class CStream;      // core_streams.h
struct LockKey;     // unit.h

class CTrapObject : public CMapObject {
public:
    // 00529A80. The map object's constructor, the map object's Create (0050E770), flags_ bit 0x40
    // cleared. The destructor is the map object's (00529B50 jumps there: no Destroy).
    CTrapObject();
    // The deleting destructor (slot 0, 00529B30) frees into the game's heap.
    static void operator delete(void* block) { gamecrt::operator_delete(block); }

    int Create() override { return CreateTrap(); }         // 2: 00529B60
    int RunTick() override { return UpdateTrap(); }        // 6: 00529B80
    void Load(void* stream) override;                      // 9: 00529D70 LoadTrapObject
    void Save(void* stream) override;                      // 10: 00529DA0 SaveTrapObject
    const char* GetClassName() override;                   // 11: 00529B00 "CTrapObject"
    int GetClassId() override;                             // 12: 00529B10 (0x55)
    bool IsSynchronised() override { return Slot29True(); }   // 29
    int IsUsable() override { return Slot38One(); }           // 38
    // 41: Disarm. The first argument is the key record (combat_actions.cpp passes its address).
    bool ApplyScience(int first, int second) override;
    int AcceptsKey(const LockKey* key) override { return KeyFits(key); }   // 42

    // The registrations (C20 retires them): 00529A80 the constructor in place (returns this),
    // 00529B30 (slot 0, RET 4) the destructor, then operator delete when `flags` bit 0 is set
    // (returns this).
    CTrapObject* Construct();
    CTrapObject* DeletingDestruct(int flags);
    // 00529B60 (slot 2). The map object's Create, flags_ bit 0x40 cleared; returns Create's result.
    int CreateTrap();
    // 00529B80 (slot 6). The map object's update; then, armed (flags_ 0x80000) on the server (the
    // context's slot 26): the units within 1 of the position (the sector map). With any, the
    // world script above runs (004D23A0 on the mission script, z = the world's slot 28 at x, y)
    // and the trap disarms; with none, flags_ 0x100000 is cleared. Returns 0.
    int UpdateTrap();
    // 00529D20. Armed: flags_ |= 0x180000.
    void Arm();
    // 00529D30 (slot 41, RET 8). An armed trap disarms when `key` accepts (00522AD0: always);
    // AL 1, else 0. The second argument is not read.
    bool Disarm(const LockKey* key, int unused);
    // 00529D60 (slot 42, RET 4). 00522AD0's EAX (1).
    int KeyFits(const LockKey* key);
    // 00529D70 / 00529DA0 (slots 9 / 10, RET 4). The map object part (0050F7A0 / 0050F900), then
    // +0x238.
    void LoadTrapObject(CStream* stream);
    void SaveTrapObject(CStream* stream);
    bool Slot29True();   // 00529AE0 (slot 29): AL = 1
    int Slot38One();     // 00529AF0 (slot 38): 1

    std::uint32_t savedValue_;   // +0x238 only loaded and saved; nothing sets it (not even the constructor)
};
static_assert(offsetof(CTrapObject, savedValue_) == 0x238);
static_assert(sizeof(CTrapObject) == 0x23C);

// 00529B00 / 00529B10 (slots 11 / 12): "CTrapObject" (0078F724) and 0x55. 00529F50 / 00529F60:
// the magic trap's "CMagicTrapObject" (0078F774) and 0x61.
class MagicTrap;   // magic_trap.h
const char* __fastcall TrapClassName(CTrapObject* self);
int __fastcall TrapClassId(CTrapObject* self);
const char* __fastcall MagicTrapClassName(MagicTrap* self);
int __fastcall MagicTrapClassId(MagicTrap* self);
