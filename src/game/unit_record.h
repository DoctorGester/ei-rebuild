#pragma once

// The unit record the world server applies to a unit it creates (00477B00, called from
// CWorldServer's object creation 005927C0): base stats, skills, the inventory, spells and
// equipment. Names are provisional; see docs/notes/objects.md ("Rewritten").

#include <cstddef>
#include <cstdint>

class CUnit;

namespace unitrecord {

// Offsets in the record.
namespace field {
constexpr std::size_t kStats = 0x00;          // 4 ints -> stats +0x28, +0x2C, +0x34, +0x38 as floats
constexpr std::size_t kNoStats1 = 0x04;       // with kNoStats0 / 2 == 0: only the items are applied
constexpr std::size_t kNoStats0 = 0x10;       // float
constexpr std::size_t kNoStats2 = 0x14;       // float
constexpr std::size_t kValues = 0x10;         // 0x10..0x53: copied to the controller (see the .cpp)
constexpr std::size_t kBytes54 = 0x54;        // 7 bytes -> controller +0x270
constexpr std::size_t kModelScale = 0x70;     // byte: times the prototype model's +0x6C..+0x84
constexpr std::size_t kPairsA = 0x74;         // 6 floats, each stored twice (controller +0x290)
constexpr std::size_t kPairsB = 0x8C;         // 6 floats, each stored twice (controller +0x2C0)
constexpr std::size_t kBytesA4 = 0xA4;        // 3 bytes -> controller +0x8A8
constexpr std::size_t kCarried = 0xB0;        // dword array {data, size}: item ids
constexpr std::size_t kQuickItems = 0xC4;
constexpr std::size_t kSpells = 0xD8;
constexpr std::size_t kWeapons = 0xEC;
constexpr std::size_t kArmour = 0x100;
constexpr std::size_t kApplyStats = 0x110;    // 0: only the items
} // namespace field

class UnitRecord {
public:
    // 00477B00. Applies the record to `unit`.
    void ApplyTo(CUnit* unit);
};

} // namespace unitrecord
