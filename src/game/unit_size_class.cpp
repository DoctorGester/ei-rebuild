// A unit's size class, standing height and radius (00530570). See unit.h and docs/notes/units.md
// ("Creation, databases, save").
#include "unit.h"

#include "combat.h"
#include "unit_db.h"

#include "ei/hook.h"

#include <bit>
#include <cstdint>
#include <cstring>

namespace {

constexpr float kHeightScale = 0.9f;      // 0073CD70
constexpr float kRadiusExtra = 0.05f;     // 0073C52C
constexpr float kPlayerExtra = 0.2f;      // 0073BAD4
constexpr float kMaxHeight = 2.0f;        // 0073E9E0
constexpr float kDeepPrototype = 1.5f;    // 0073C51C
constexpr std::uint32_t kShallowHeight = 0x3C23D70A;   // 0.01f
constexpr std::uint32_t kDefaultRadius = 0x3F400000;   // 0.75f
constexpr std::uint8_t kMovesOnWater = 2;               // movementKind

} // namespace

void CUnit::UpdateSizeClass() {
    Controller()->UpdateMoveFactors();   // the server's 005536B0; empty (00533D10) on the others
    if (controller->GetClassId() != kClassUnitServer) {
        sizeClass = movementKind == kMovesOnWater ? 0 : 3;
        std::memcpy(&unitRadius, &kDefaultRadius, 4);
        return;
    }
    // The server controller's size classes, read unsigned.
    CombatStats& combat = static_cast<CUnitServer*>(controller)->Combat();
    if (moveMode == 0) sizeClass = static_cast<std::uint16_t>(combat.sizeClass20);
    else if (moveMode == 1) sizeClass = static_cast<std::uint16_t>(combat.sizeClass60);
    else sizeClass = static_cast<std::uint16_t>(combat.sizeClass);

    // The larger half extent, selected bitwise by FCOMP's C0: above.y when above.x is below it or NaN.
    float extentX = above.x;
    float extentY = above.y;
    float extent = extentX >= extentY ? extentX : extentY;
    float height = extent * kHeightScale;
    standingHeight = height;
    unitRadius = height + kRadiusExtra;
    if (player) standingHeight = height + kPlayerExtra;
    // At most 2, selected the same way: 2 when below the height or NaN.
    float limited = standingHeight;
    standingHeight = kMaxHeight >= limited ? limited : kMaxHeight;
    if (sizeClass != 0) return;
    // The prototype by prototypeId_; out of range the original reads address 0x24 (kept).
    auto* prototype = reinterpret_cast<const UnitPrototypeRecord*>(unitdb::Prototypes().RecordAt(prototypeId_, unitdb::kPrototypeSize));
    // FCOMP; TEST AH,0x41: only for a value above 1.5.
    if (std::bit_cast<float>(prototype->height) > kDeepPrototype) std::memcpy(&standingHeight, &kShallowHeight, 4);
}
EI_REPLACE_METHOD(0x00530570, CUnit::UpdateSizeClass);
