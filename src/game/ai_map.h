#pragma once

#include "engine_object.h"

#include <cstddef>
#include <cstdint>

constexpr int kClassUnit = 0x50;    // CEngineObject::GetClassId() of units (creatures)

// A unit, as far as the AI map needs it. Name and layout provisional.
class CUnit : public CEngineObject {
public:
    std::uint8_t unknown_1C[0x324 - 0x1C];
    int sizeClass;          // +0x324 selects the 4-bit passability field in each AI map cell
    std::uint8_t unknown_328[4];
    float standingHeight;   // +0x32C; deeper water or higher ground than (2 - height) * 16 blocks it
};
static_assert(offsetof(CUnit, sizeClass) == 0x324);
static_assert(offsetof(CUnit, standingHeight) == 0x32C);

// One AI map cell (9 bytes): a 4-bit passability field per unit size class, then a height level.
#pragma pack(push, 1)
struct AIMapCell {
    std::uint32_t passable;     // bits 4*sizeClass .. +3: non-zero = passable for that class
    std::uint8_t unknown_4[4];
    std::uint8_t level;         // +8 compared with the unit's standing level
};
#pragma pack(pop)
static_assert(sizeof(AIMapCell) == 9);

// The navigation grid of the current map, loaded by CAIMap::Load (005B43C0, patched by Rebirth).
// Name from the game's error strings; layout provisional.
class CAIMap {
public:
    // 005B80F0. Can `unit` stand on cell (x, y)? `reason` gets 0 (ok or not a unit), 1 (cell level
    // too high) or 2 (water too deep).
    bool CanStandAt(CUnit* unit, int x, int y, int* reason);

    // 005B79D0. Can `unit` stand on and enter cell (x, y)? Always true for non-units.
    bool IsPassable(CUnit* unit, int x, int y);

    std::uint8_t unknown_000[0x1F8];
    std::int8_t** waterDepth;           // +0x1F8 rows of per-cell water depth
    std::uint8_t unknown_1FC[0x269C - 0x1FC];
    AIMapCell** cells;                  // +0x269C rows of cells
    int width;                          // +0x26A0
    int height;                         // +0x26A4
};
static_assert(offsetof(CAIMap, cells) == 0x269C);
