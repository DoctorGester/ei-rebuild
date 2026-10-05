// A unit's size class, standing height and radius (00530570). See unit.h and docs/notes/units.md
// ("Seventh round (U5)").
#include "unit.h"

#include "ei/hook.h"
#include "ei/vcall.h"

#include <cstdint>
#include <cstring>

namespace {

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(object) + offset);
}

constexpr int kClassUnitServer = 0x3E;
constexpr int kControllerSlotMoveFactors = 0xB4 / 4;   // the server's 005536B0; empty (00533D10) on the others
// The server controller's size classes (its CombatStats + 0x68 / 0x6C / 0x6E), read unsigned.
constexpr int kServerSizeClass = 0x248;
constexpr int kServerSizeClassMode0 = 0x24C;
constexpr int kServerSizeClassMode1 = 0x24E;
constexpr int kExtentX = 0x144;           // the unit's half extents (UnitActionPoint's offsets)
constexpr int kExtentY = 0x148;
constexpr int kRadius = 0x328;            // virtual slot 60
constexpr int kPrototypeDepth = 0x24;     // unit prototype: compared with 1.5 for size class 0

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
    ei::VCall<void>(controller, kControllerSlotMoveFactors);
    if (controller->GetClassId() != kClassUnitServer) {
        sizeClass = movementKind == kMovesOnWater ? 0 : 3;
        At<std::uint32_t>(this, kRadius) = kDefaultRadius;
        return;
    }
    void* server = controller;
    if (moveMode == 0) sizeClass = At<std::uint16_t>(server, kServerSizeClassMode0);
    else if (moveMode == 1) sizeClass = At<std::uint16_t>(server, kServerSizeClassMode1);
    else sizeClass = At<std::uint16_t>(server, kServerSizeClass);

    // The larger extent, selected bitwise by FCOMP's C0: +0x148 when +0x144 is below it or NaN.
    float extentX = At<float>(this, kExtentX);
    float extentY = At<float>(this, kExtentY);
    float extent = !(extentX >= extentY) ? extentY : extentX;
    float height = extent * kHeightScale;
    standingHeight = height;
    At<float>(this, kRadius) = height + kRadiusExtra;
    if (player) standingHeight = height + kPlayerExtra;
    // At most 2, selected the same way: 2 when below the height or NaN.
    float limited = standingHeight;
    standingHeight = !(kMaxHeight >= limited) ? kMaxHeight : limited;
    if (sizeClass != 0) return;
    // The prototype by unknown_18; out of range the original reads address 0x24 (kept).
    const char* prototype = unitdb::Prototypes().At(unknown_18, unitdb::kPrototypeSize);
    // FCOMP; TEST AH,0x41: only for a value above 1.5.
    if (Field<float>(prototype, kPrototypeDepth) > kDeepPrototype) std::memcpy(&standingHeight, &kShallowHeight, 4);
}
EI_REPLACE_METHOD(0x00530570, CUnit::UpdateSizeClass);
