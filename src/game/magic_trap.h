#pragma once

// Magic traps (map objects of class id 0x3B; the zone file's
// "MagicTrap" records; a real virtual class: the compiler makes its vtable).
// Names provisional. flags_ bit 0x80000 is
// "armed" (the script's ActivateTrap); the position is the map object's. The trap's own members
// start (declared below). See docs/notes/units.md ("Magic traps").

#include "lib/crt.h"
#include "lib/mfc.h"
#include "map_object.h"
#include "map_spec.h"   // MagicTrapRecord, TrapArea, TrapPoints

#include <cstddef>
#include <cstdint>

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

class CSpell;
class CStream;

class MagicTrap : public CMapObject {
public:
    // The constructor (the factory, class id 0x3B): the map object,
    // no spell, the two arrays (circles, points) and the list of units
    // inside (blocks of 10) empty; then the map object's Create (called directly),
    // sides -1, flags_ |= 0x80040, the spell released, both arrays SetSize(0,
    // -1), the list emptied, countdown 0, +0x28C 0, period 10.
    MagicTrap();
    ~MagicTrap() override;

    int Create() override { return CreateTrap(); }
    void Destroy() override { DestroyTrap(); }
    int RunTick() override { return UpdateTrap(); }
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;
    const char* GetClassName() override;                   // 11: "CMagicTrapObject" (trap_object.cpp)
    int GetClassId() override;
    bool IsSynchronised() override { return true; }        // 29: the trap's

    // SaveTrap. The map object part, the spell's id (-1:
    // none), the circles (count, then 12 bytes each), the units inside (count, then each id
    // through the reference's Save), the points (count, then 8 bytes each), the period, countdown, +0x28C and
    // the sides.
    void SaveTrap(CStream* stream);
    // LoadTrap. Reads what SaveTrap writes: the spell found by id in the
    // spell manager (the context's slot 22), the circles and points through SetSize(n,
    // -1), the units inside appended (resolved in the trap's context).
    void LoadTrap(CStream* stream);

    // Virtual slot 2 (Create; the manager's CreateObject). The map object's Create
    // then as the constructor's tail: sides -1, flags_ |= 0x80040, the spell released,
    // the circles' storage freed inline (count and capacity 0), the points SetSize(0, -1), the
    // list emptied, period 10, countdown and +0x28C 0. Returns what the map object's Create returned.
    int CreateTrap();
    // Virtual slot 3 (Destroy; the manager's ReleaseObject / DestroyAll). The map object's Destroy, the spell
    // released, the circles freed inline, the points SetSize(0, -1), the list emptied.
    void DestroyTrap();
    // Virtual slot 6 (the per-tick update). The map object's update; then,
    // on the server world (its slot 26) with a spell: a period not above 0 updates the units
    // inside; otherwise the countdown goes down by one (not below 0) and, once it is
    // not above 0, the trap fires at what is in it. Returns the map object's result.
    int UpdateTrap();
    // An armed trap casts its spell at the living units of its sides found in its circles, when there are
    // any. The context is fetched even when the trap is not armed.
    void FireAtOccupants();
    // Virtual slot 56 (the world server's RemoveObject tells every object). Drops
    // every entry of the units inside that refers to `object` (the reference released and
    // cleared, the node freed; the last one empties the list as RemoveAll). Returns 0.
    int ForgetObject(CEngineObject* object) override;

    // Sets the trap up from its zone record (called by the zone loader): the sides, the spell made from the
    // record's text by the spell manager (the context's slot 22, the old one released), the position, the
    // circles and points copied, the units inside forgotten (each reference released, the list's blocks
    // freed), the period with the countdown at 0, and +0x28C from the record's byte.
    void SetUp(const MagicTrapRecord* record);

    // Called from the trap's update while the world is live, it has
    // a spell and its period is not positive. The units inside that left every circle
    // (or a unit that is dead, in the first circle that holds it) are dropped. An armed trap then
    // takes in the living units of its sides found in its circles and casts its spell
    // when any of them is new.
    void UpdateVictims();

    // Casts the trap's spell (the world server's cast, no caster) from the
    // trap's position raised by the ground height under it: at each of its points {x, y, 0} unless
    // the spell has flag 0x10000000 or there are none; otherwise at each unit of `units` (target
    // and point the unit), with +0x28C first narrowing `units` to the last one closer than 1e30
    // (squared; NaN counts). Then the countdown starts again (+0x288 = +0x284). Nothing without
    // units or a spell; nothing either (the countdown kept) when +0x28C finds none.
    void CastAtUnits(mfc::CTypedPtrList<CUnit>* units);

    using UnitList = mfc::CTypedPtrList<CUnit>;

    std::uint32_t sides;            // bit per unit side that sets it off
    CSpell* spell;                  // a counted reference; null: none
    TrapArea area;
    TrapPoints points;              // where it casts ({x, y})
    UnitList inside;                // the units inside: counted references
    int period;                     // 0: casts at newcomers; else ticks between casts
    int countdown;
    int singleTarget;               // cast at one unit only
};
static_assert(offsetof(MagicTrap, sides) == 0x238);
static_assert(offsetof(MagicTrap, spell) == 0x23C);
static_assert(offsetof(MagicTrap, area) == 0x240);
static_assert(offsetof(MagicTrap, points) == 0x254);
static_assert(offsetof(MagicTrap, inside) == 0x268);
static_assert(offsetof(MagicTrap, period) == 0x284);
static_assert(offsetof(MagicTrap, countdown) == 0x288);
static_assert(offsetof(MagicTrap, singleTarget) == 0x28C);
static_assert(sizeof(MagicTrap) == 0x290);

// Empties `out` (an MFC CPtrList, RemoveAll inlined), then for each
// circle of `area` asks the sector map (`context` + 0x294) for the units within its
// radius of {x, y, 0} into a CPtrList of its own (blocks of 10) and appends to
// `out` (AddTail inlined) each one alive (slot 55 returns 0) whose side has its bit in
// `sides`; a unit in two circles is appended twice. `context` is the trap's map object context
// (the world). See docs/notes/units.md ("Magic traps").
void __fastcall FindTrapVictims(CWorldServer* context, const TrapArea* area, std::uint32_t sides,
                                mfc::CTypedPtrList<CUnit>* out);
