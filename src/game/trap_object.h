#pragma once

// Placed traps (GetClassName "CTrapObject", class id 0x55, 0x23C bytes, made by the factory
// for kind 0x3010; a real virtual class: the compiler makes its vtable):
// the world arms one when a trap item is used (the world server calls
// Arm); while armed on the server, the first unit within 1 of it sets it off: a world script
// "CreateParticleSource( 100, x, y, z, 1, "Mushroom")" at its position and the trap disarms.
// Names provisional; trap_object.cpp.

#include "map_object.h"
#include "lib/crt.h"

#include <cstddef>
#include <cstdint>

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

struct LockKey;

class CTrapObject : public CMapObject {
public:
    CTrapObject();

    int Create() override { return CreateTrap(); }
    int RunTick() override { return UpdateTrap(); }
    void Load(CStream* stream) override;                      // LoadTrapObject
    void Save(CStream* stream) override;                      // SaveTrapObject
    const char* GetClassName() override;                   // "CTrapObject"
    int GetClassId() override;                             // 0x55
    bool IsSynchronised() override { return Slot29True(); }
    int IsUsable() override { return Slot38One(); }
    // Disarm with the key record.
    bool ApplyScience(const LockKey* key, CEngineObject* user) override;
    int AcceptsKey(const LockKey* key) override { return KeyFits(key); }

    // The map object's Create, flags_ bit 0x40 cleared; returns Create's result.
    int CreateTrap();
    // The map object's update; then, armed (flags_ 0x80000) on the server (the
    // context's slot 26): the units within 1 of the position (the sector map). With any, the
    // world script above runs (on the mission script, z = the world's slot 28 at x, y)
    // and the trap disarms; with none, flags_ 0x100000 is cleared. Returns 0.
    int UpdateTrap();
    // An armed trap disarms when `key` accepts (always);
    // AL 1, else 0. The second argument is not read.
    bool Disarm(const LockKey* key, int unused);
    // The check's EAX (1).
    int KeyFits(const LockKey* key);
    // Slots 9 / 10. The map object part, then
    // +0x238.
    void LoadTrapObject(CStream* stream);
    void SaveTrapObject(CStream* stream);
    bool Slot29True();   // AL = 1
    int Slot38One();     // 1

    std::uint32_t savedValue_;   // only loaded and saved; nothing sets it (not even the constructor)
};

// Slots 11 / 12: "CTrapObject" and 0x55. The magic trap's
// "CMagicTrapObject" and 0x61.
class MagicTrap;
