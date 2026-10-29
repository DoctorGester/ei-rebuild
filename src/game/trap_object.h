#pragma once

// Placed traps (ClassName "CTrapObject", class id 0x55, 0x23C bytes, made by the factory
// for kind 0x3010; a real virtual class: the compiler makes its vtable):
// the world arms one when a trap item is used (the world server calls
// Arm); while armed on the server, the first unit within 1 of it sets it off: a world script
// "CreateParticleSource( 100, x, y, z, 1, "Mushroom")" at its position and the trap disarms.
// Names provisional; trap_object.cpp.

#include "map_object.h"

#include <cstddef>
#include <cstdint>

struct LockKey;

class CTrapObject : public CMapObject {
public:
    CTrapObject();

    void Create() override;                                   // the map object's Create, flags bit 0x40 cleared
    // The map object's update; then, armed (flags 0x80000) on the server (the
    // context's query): the units within 1 of the position (the sector map). With any, the
    // world script above runs (on the mission script, z = what the world's virtual gives at x, y)
    // and the trap disarms; with none, flags 0x100000 is cleared. False.
    bool RunTick() override;
    void Load(CStream* stream) override;                      // LoadTrapObject
    void Save(CStream* stream) override;                      // SaveTrapObject
    const char* ClassName() override;                         // "CTrapObject"
    ClassId GetClassId() override;
    bool IsSynchronised() override { return true; }
    bool IsUsable() override { return true; }
    // Disarm with the key record.
    bool ApplyScience(const LockKey* key, CEngineObject* user) override;
    bool AcceptsKey(const LockKey* key) override { return KeyFits(key); }

    // An armed trap disarms when `key` accepts (always); returns true. The second argument is not read.
    bool Disarm(const LockKey* key);
    // The check's result (1).
    int KeyFits(const LockKey* key);
    // Load / Save. The map object part, then
    // +0x238.
    void LoadTrapObject(CStream* stream);
    void SaveTrapObject(CStream* stream);

    std::uint32_t savedValue;    // only loaded and saved; nothing sets it (not even the constructor)
};

// Class name and id: "CTrapObject" and 0x55. The magic trap's
// "CMagicTrapObject" and 0x61.
class MagicTrap;
