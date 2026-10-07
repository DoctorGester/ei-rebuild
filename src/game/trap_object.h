#pragma once

// Placed traps (GetClassName "CTrapObject", class id 0x55, 0x23C bytes, vtable 0073E548, made by the
// factory 00512730 for kind 0x3010): the world arms one when a trap item is used (0058ECE0 calls
// Arm); while armed on the server, the first unit within 1 of it sets it off: a world script
// "CreateParticleSource( 100, x, y, z, 1, "Mushroom")" at its position and the trap disarms.
// Names provisional; trap_object.cpp (A-U6).

#include "map_object.h"

#include <cstddef>
#include <cstdint>

class CStream;      // core_streams.h
struct LockKey;     // unit.h

class CTrapObject : public CMapObject {
public:
    // 00529A80. The map object's constructor, the vtable, the map object's Create (0050E770),
    // flags_ bit 0x40 cleared. Returns this.
    CTrapObject* Construct();
    // 00529B30 (slot 0, RET 4). The map object's destructor body (00529B50 jumps there: no vtable
    // or Destroy), then operator delete when `flags` bit 0 is set; returns this.
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

    std::uint32_t unknown_238;   // +0x238 saved and loaded; nothing sets it here
};
static_assert(offsetof(CTrapObject, unknown_238) == 0x238);
static_assert(sizeof(CTrapObject) == 0x23C);

// 00529B00 / 00529B10 (slots 11 / 12): "CTrapObject" (0078F724) and 0x55. 00529F50 / 00529F60:
// the magic trap's "CMagicTrapObject" (0078F774) and 0x61.
const char* __fastcall TrapClassName(CMapObject* self);
int __fastcall TrapClassId(CMapObject* self);
const char* __fastcall MagicTrapClassName(CMapObject* self);
int __fastcall MagicTrapClassId(CMapObject* self);
