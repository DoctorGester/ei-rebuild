// The unit shapes (CUnitShape, ai_unit_layer.h): the 16 x 16 cell levels a unit of a radius stamps
// on the AI map's unit layers. Shared through the shape cache (007C2F70), whose slot 0 creates
// them. See docs/notes/ai.md.
#include "ai_unit_layer.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/crt.h"

#include <cstring>

namespace {

// 0073FD2C, 0073FD18 x 0073B86C (the cell size, halved), 0073CA98.
constexpr double kMargin = 2.0;
constexpr double kHalfCell = 1.0 * 0.5;
constexpr double kLevelsPerUnit = 16.0;
constexpr int kMiddle = 8;

void SetVtable(CUnitShape* shape, std::uintptr_t vtable) { std::memcpy(static_cast<void*>(shape), &vtable, sizeof(vtable)); }

// operator new and the counted header's constructor: no references, no flags (the original
// writes them before it tests the allocation).
CUnitShape* NewShape() {
    auto* shape = static_cast<CUnitShape*>(gamecrt::operator_new(sizeof(CUnitShape)));
    shape->refs_ = 0;
    shape->flags6_ = 0;
    shape->flags7_ = 0;
    return shape;
}

} // namespace

CUnitShape* CUnitShape::Construct(float newRadius) {
    radius = newRadius;
    SetVtable(this, kUnitShapeVtable);
    std::memset(levels, 0, sizeof(levels));
    for (int dy = -kMiddle; dy < kMiddle; ++dy) {
        int rowSquared = dy * dy;
        for (int dx = -kMiddle; dx < kMiddle; ++dx) {
            double distance = x87::fsqrt(static_cast<double>(dx * dx + rowSquared));
            float level = static_cast<float>(((kMargin + radius) - distance * kHalfCell) * kLevelsPerUnit);
            int value = x87::fistp(level);
            if (value > 0) levels[dy + kMiddle][dx + kMiddle] = static_cast<std::uint8_t>(value);
        }
    }
    return this;
}
EI_REPLACE_METHOD(0x005B3440, CUnitShape::Construct);

int CUnitShape::Empty() {
    SetVtable(this, kUnitShapeVtable);
    std::memset(levels, 0, sizeof(levels));
    radius = -1.0f;
    return 0;
}
EI_REPLACE_METHOD(0x005B3540, CUnitShape::Empty);

CUnitShape* CUnitShape::Copy() {
    CUnitShape* copy = NewShape();
    if (!copy) return nullptr;
    std::memcpy(copy->levels, levels, sizeof(levels));
    copy->radius = radius;
    SetVtable(copy, kUnitShapeVtable);
    return copy;
}
EI_REPLACE_METHOD(0x005B34F0, CUnitShape::Copy);

CUnitShape* __fastcall CreateUnitShape(void*, void*, const UnitShapeHandle* handle) {
    CUnitShape* shape = NewShape();
    if (!shape) return nullptr;
    return shape->Construct(handle->radius);
}
EI_REPLACE(0x005B33D0, CreateUnitShape);
