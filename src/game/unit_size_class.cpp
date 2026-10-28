// A unit's size class, standing height and radius. See unit.h and docs/notes/units.md
// ("Creation, databases, save").
#include "unit.h"

#include "combat.h"
#include "unit_db.h"

#include <bit>
#include <cstdint>
#include <cstring>

namespace {

constexpr float kHeightScale = 0.9f;
constexpr float kRadiusExtra = 0.05f;
constexpr float kPlayerExtra = 0.2f;
constexpr float kMaxHeight = 2.0f;
constexpr float kDeepPrototype = 1.5f;
constexpr float kShallowHeight = 0.01f;
constexpr float kDefaultRadius = 0.75f;
constexpr std::uint8_t kMovesOnWater = 2;               // UnitParams::movementKind

} // namespace

void CUnit::UpdateSizeClass() {
    Controller()->UpdateMoveFactors();   // the server's controller; empty on the others
    if (controller->GetClassId() != kClassUnitServer) {
        sizeClass = params.movementKind == kMovesOnWater ? 0 : 3;
        unitRadius = kDefaultRadius;
        return;
    }
    // The server controller's size classes, read unsigned.
    const UnitStats& combat = ServerController()->stats;
    if (moveMode == 0) sizeClass = static_cast<std::uint16_t>(combat.sizeClass20);
    else if (moveMode == 1) sizeClass = static_cast<std::uint16_t>(combat.sizeClass60);
    else sizeClass = static_cast<std::uint16_t>(combat.sizeClass);

    // The larger half extent, selected bitwise: above.y when above.x is below it or NaN.
    float extentX = bounds.boxMax.x;
    float extentY = bounds.boxMax.y;
    float extent = extentX >= extentY ? extentX : extentY;
    float height = extent * kHeightScale;
    standingHeight = height;
    unitRadius = height + kRadiusExtra;
    if (player) standingHeight = height + kPlayerExtra;
    // At most 2, selected the same way: 2 when below the height or NaN.
    float limited = standingHeight;
    standingHeight = kMaxHeight >= limited ? limited : kMaxHeight;
    if (sizeClass != 0) return;
    // The prototype by prototypeId_; out of range it reads address 0x24 (kept).
    auto* prototype = unitdb::PrototypeRecord(prototypeId_);
    // Only for a value above 1.5 (NaN not).
    if (std::bit_cast<float>(prototype->height) > kDeepPrototype) standingHeight = kShallowHeight;
}
