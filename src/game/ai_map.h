#pragma once

#include "engine_object.h"
#include "maths.h"

#include <cstddef>
#include <cstdint>

constexpr int kClassUnit = 0x50;    // CEngineObject::GetClassId() of units (creatures)

// A unit, as far as the AI map needs it. Name and layout provisional.
class CUnit : public CEngineObject {
public:
    // 0050E930. Called when the unit leaves the sector map; not rewritten yet.
    void OnLeftSectorMap();

    float positionX;        // +0x1C
    float positionY;        // +0x20
    std::uint8_t unknown_24[0x1B8 - 0x24];
    int sectorCellX;        // +0x1B8 cell the unit is registered in, -1 when not
    int sectorCellY;        // +0x1BC
    std::uint8_t unknown_1C0[0x324 - 0x1C0];
    int sizeClass;          // +0x324 selects the 4-bit passability field in each AI map cell
    std::uint8_t unknown_328[4];
    float standingHeight;   // +0x32C; deeper water or higher ground than (2 - height) * 16 blocks it
};
static_assert(offsetof(CUnit, sectorCellX) == 0x1B8);
static_assert(offsetof(CUnit, sizeClass) == 0x324);

constexpr std::uint32_t kInSectorMap = 0x8000;    // CEngineObject::flags_
static_assert(offsetof(CUnit, standingHeight) == 0x32C);

// One AI map cell (9 bytes): a 4-bit passability field per unit size class, then a height level.
#pragma pack(push, 1)
struct AIMapCell {
    std::uint32_t passable;     // bits 4*sizeClass .. +3: non-zero = passable for that class
    std::uint16_t groundLevel;  // +4 terrain height in AI height units; blocks lines of sight
    std::uint8_t unknown_6[2];
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

    // 005BC3E0. Visibility from `from` to `to` (world positions): 0 when terrain blocks the line,
    // else the product of the transparencies of the objects it passes (at least 0.0001).
    float LineOfSight(const Vector3* from, const Vector3* to);

    // 005B6A80. Stamps (remove = 0) or clears (remove = 1) the object's footprint in the grid;
    // false if it does not fit. Not rewritten yet.
    bool MarkFootprint(CEngineObject* object, int remove);

    std::uint8_t unknown_000[0x120];
    float heightScale;                  // +0x120 world z to AI height units
    std::uint32_t unknown_124;
    int sectorsPerRow;                  // +0x128
    std::uint8_t unknown_12C[0x138 - 0x12C];
    std::uint8_t* sectors;              // +0x138 0x54-byte sector records (see LineOfSight)
    std::uint8_t unknown_13C[0x1F8 - 0x13C];
    std::int8_t** waterDepth;           // +0x1F8 rows of per-cell water depth
    std::uint8_t unknown_1FC[0x269C - 0x1FC];
    AIMapCell** cells;                  // +0x269C rows of cells
    int width;                          // +0x26A0
    int height;                         // +0x26A4
    std::uint32_t unknown_26A8;
    std::uint32_t** occluders;          // +0x26AC rows of per-cell occluder words (see LineOfSight)
};
static_assert(offsetof(CAIMap, cells) == 0x269C);
