#pragma once

// Magic traps (map objects of the factory type MapObjectType::MagicTrap, class id 0x61; the zone file's "MagicTrap" records). MapObjectFlag::Armed is
// "armed" (the script's ActivateTrap); the position is the map object's. See docs/notes/units.md ("Magic traps").

#include "map_object.h"
#include "map_spec.h"   // MagicTrapRecord, TrapArea, TrapPoints

#include <cstddef>
#include <cstdint>
#include <vector>

class CSpell;

class MagicTrap : public CMapObject {
public:
    // Also runs the map object's Create and the trap's reset (as Create).
    MagicTrap();
    ~MagicTrap() override;

    // The map object's Create, then sides -1, flags |= 0x80040, no spell, no circles, points or units, period
    // 10, countdown and singleTarget 0.
    void Create() override;
    void Destroy() override { DestroyTrap(); }
    // On the server world with a spell: a period not above 0 updates the units inside; otherwise the countdown
    // goes down by one (not below 0) and, once it is not above 0, the trap fires at what is in it.
    bool RunTick() override;
    void Load(ByteStream* stream) override;
    void Save(ByteStream* stream) override;
    const char* ClassName() override;   // "CMagicTrapObject"
    ClassId GetClassId() override;
    bool IsSynchronised() override { return true; }

    // The map object part, the spell's id (-1: none), the circles (count, then 12 bytes each), the units inside
    // (count, then each id), the points (count, then 8 bytes each), the period, countdown, singleTarget and the
    // sides.
    void SaveTrap(ByteStream* stream);
    // Reads what SaveTrap writes (the spell found by id in the spell manager, the units resolved in the context).
    void LoadTrap(ByteStream* stream);

    void DestroyTrap();
    // An armed trap casts its spell at the living units of its sides found in its circles, when there are
    // any. The context is fetched even when the trap is not armed.
    void FireAtOccupants();
    // Drops every entry of the units inside that refers to `object`. Returns 0.
    int ForgetObject(EngineObject* object) override;

    // Sets the trap up from its zone record (called by the zone loader); the units inside are forgotten and the
    // countdown starts at 0.
    void SetUp(const MagicTrapRecord* record);

    // The units inside that left every circle (or a unit that is dead, in the first circle that holds it) are
    // dropped. An armed trap then takes in the living units of its sides found in its circles and casts its
    // spell when any of them is new.
    void UpdateVictims();

    // Casts the trap's spell (no caster) from the trap's position raised to the ground: at each of its points
    // unless the spell has flag 0x10000000 or there are none; otherwise at each unit of `units`, singleTarget
    // first narrowing them to the last one closer than 1e30 (squared; NaN counts). Then countdown = period.
    // Nothing without units or a spell; nothing either (the countdown kept) when singleTarget finds none.
    void CastAtUnits(std::vector<CUnit*>* units);

    std::uint32_t sides;            // bit per unit side that sets it off
    Ref<CSpell> spell;              // null: none
    std::vector<TrapCircle> area;
    std::vector<TrapPoint> points;              // where it casts ({x, y})
    std::vector<HeldRef<CUnit>> inside;      // the units inside
    int period;                     // 0: casts at newcomers; else ticks between casts
    int countdown;
    int singleTarget;               // cast at one unit only
};

// `out` becomes the living units whose side has its bit in `sides` within each circle of `area` (from the
// world's sector map); a unit in two circles is appended twice.
void FindTrapVictims(WorldServer* context, const std::vector<TrapCircle>* area, std::uint32_t sides,
                                std::vector<CUnit*>* out);
