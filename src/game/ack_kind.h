#pragma once

// The ack kinds: what a unit says (acks.db, acks_db.h) and the code a party message carries about a unit
// (CPlayerServer::SendObject0B, CUnitServer::Shout, CWorldServer::TellParties; the client's OnUnitSpeech reads it
// back). One dword on the wire. The names are the folders the speeches' sounds sit in (Acks\<unit>\<folder>\n.wav)
// of the kind's list in acks.db, with the English words spelt out; where the code's use is clearer it is said.

#include <cstdint>

enum class AckKind : std::uint32_t {
    Selected = 0x00,
    Move = 0x01,
    Attack = 0x02,
    Cast = 0x03,
    Loot = 0x04,              // a search of a body
    UseObject = 0x05,         // a use of an object; a tame order says it too
    Steal = 0x06,
    Follow = 0x07,
    UsePotion = 0x08,
    ChangePosition = 0x09,    // a new movement mode (walk / run / sneak)
    BigAttack = 0x0A,         // a member ordered to attack a target more than 5 levels above it
    NoWayToAttack = 0x0B,     // an attack that cannot be reached or planned
    OutOfReach = 0x0C,        // a target too high or too far to reach; no speech list (the unit is silent)
    CantChangePosition = 0x0D,
    NoPath = 0x0E,            // an order with no way to it
    NoMana = 0x0F,            // acks.db's "CantCast": no strength left to cast
    CantTeleport = 0x10,
    ScienceFailed = 0x11,     // a use of science that failed; also a quick item without a charge
    NoTarget = 0x12,          // the target is out of sight
    SpellTooComplex = 0x13,   // acks.db's "ComplSp": the unit cannot cast this spell
    Overloaded = 0x14,        // cannot run with this load
    Injured = 0x15,           // cannot run with this wound
    TameFailed = 0x16,        // no speech list
    LinkImpossible = 0x18,    // the link spell's target is not free; no speech list
    LinkEnded = 0x19,         // no speech list
    LinkTameFailed = 0x1A,    // no speech list
    Stopped = 0x1B,           // no speech list
    DecidedToAttack = 0x1C,
    OutOfStamina = 0x1D,
    ArmCrippled = 0x1E,
    LegCrippled = 0x1F,
    AttackedInDefence = 0x20,
    WaitForFollowers = 0x21,
    StealFailed = 0x22,       // acks.db's "StealEmp"
    ArmourBroken = 0x23,
    WeaponBroken = 0x24,
    ArmourWorn = 0x25,        // the sound of a worn piece, no speech
    WeaponWorn = 0x26,
    ShopYes = 0x27,           // a party unit walking slowly says these instead of the kinds 0..5, 7..10 (PartyKind)
    ShopNo = 0x28,            // ... and instead of 0x0B..0x13 and 0x1D
    Bored = 0x29,
    Scenario = 0x2A,          // a named speech (a script's "say")
    ScenarioTalking = 0x2B,   // the same, the unit marked as talking
    Extra1 = 0x2C,            // the extras' lists (no sender, none filled)
    Extra2 = 0x2D,
    Aggression = 0x2E,        // acks.db's models table: "Agress"
    Suspicion = 0x2F,         // "Suspect"
    Kill = 0x30,
    Rest = 0x31,              // an idle unit
    FightingRest = 0x32,      // "InAgress": an idle unit in aggression
    StopSpeech = 0xFF,        // the unit stops talking
    None = 0xFFFFFFFFu,       // no ack (a planner's "say nothing")
};

// The kinds a unit's Shout / the order planner send: the plain ones and the kill.
constexpr bool IsShoutable(AckKind kind) { return kind <= AckKind::Bored || kind == AckKind::Kill; }
