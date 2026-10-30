#pragma once

// The unit record the world server applies to a unit it creates (called from WorldServer's object creation):
// base stats, skills, the inventory, spells and equipment. It is the tail of a .mob unit record
// (mapspec::UnitSpec + 0x210). See docs/notes/objects.md ("Rewritten").

#include <cstddef>
#include <cstdint>
#include <string>

#include <vector>

class Unit;

namespace unitrecord {

// The statistics block (chunk BBBB0004, 0xAC bytes). The values go to the server companion's vitals and the
// controller's statistics.
struct UnitRecordStats {
    std::int32_t hitPoints;         // +0x00 -> vitals hitPoints (as a float)
    std::int32_t maxHitPoints;      // +0x04 -> vitals maxHitPoints; 0 with speed / rate 0: only the items
    std::int32_t mana;              // +0x08 -> vitals mana
    std::int32_t maxMana;           // +0x0C -> vitals maxMana
    float speed;                    // +0x10 -> UnitStats speed
    float rate;                     // +0x14 -> rate
    float modeSpeeds[4];            // +0x18 -> modeSpeeds
    float sightCone;                // +0x28 -> sightCone
    float peripheralRange;          // +0x2C -> peripheralRange
    float sideCone;                 // +0x30 -> sideCone
    float reachHeightScale;         // +0x34 -> reachHeightScale
    std::uint8_t sizeClass;         // +0x38 -> sizeClass
    std::uint8_t sizeClassSmall;    // +0x39 -> sizeClass20 and sizeClass60
    std::uint8_t unused_3A[2];
    float reach;                    // +0x3C -> combat[0]
    float attack;                   // +0x40 -> combat[1]
    float defence;                  // +0x44 -> combat[3]
    float combatSpeed;              // +0x48 -> combat[4]
    float damage;                   // +0x4C -> damage
    float damageSpread;             // +0x50 -> damageSpread
    std::uint8_t damageWeights[7];  // +0x54 the first 7 bytes of UnitStats::damageWeights
    std::uint8_t unused_5B[0x70 - 0x5B];
    std::uint8_t protectionScale;   // +0x70 times the prototype model's protection (vitals, each twice)
    std::uint8_t unused_71[3];
    float perception[6];              // +0x74 -> UnitStats perception (base and current)
    float conspicuity[6];              // +0x8C -> conspicuity
    std::uint8_t attributes[3];     // +0xA4 -> attributes
    std::uint8_t unused_A7[0xAC - 0xA7];
};
static_assert(offsetof(UnitRecordStats, speed) == 0x10);
static_assert(offsetof(UnitRecordStats, reachHeightScale) == 0x34);
static_assert(offsetof(UnitRecordStats, reach) == 0x3C);
static_assert(offsetof(UnitRecordStats, damageWeights) == 0x54);
static_assert(offsetof(UnitRecordStats, protectionScale) == 0x70);
static_assert(offsetof(UnitRecordStats, perception) == 0x74);
static_assert(offsetof(UnitRecordStats, conspicuity) == 0x8C);
static_assert(offsetof(UnitRecordStats, attributes) == 0xA4);
static_assert(sizeof(UnitRecordStats) == 0xAC);

class UnitRecord {
public:
    // Applies the record to `unit`.
    void ApplyTo(Unit* unit);

    UnitRecordStats stats;          // +0x000 (chunk BBBB0004)
    // +0x0AC (chunks BBBB0005 ... BBBB0009) the item names: carried, quick items (worn; also CreateObject),
    // spells, weapons, armour.
    std::vector<std::string> lists[5];
    std::uint32_t applyStats;       // +0x110 (chunk BBBB000A, a byte) 0: only the items are applied

    enum List { kCarried, kQuickItems, kSpells, kWeapons, kArmour };
};
} // namespace unitrecord
