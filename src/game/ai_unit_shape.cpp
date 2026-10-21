// The unit shapes (CUnitShape, ai_unit_layer.h): the 16 x 16 cell levels a unit of a radius stamps
// on the AI map's unit layers. Shared through the shape cache (007C2F70), whose slot 0 creates
// them. See docs/notes/ai.md.
#include "ai_unit_layer.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "ei/x87.h"
#include "lib/compiled_vtable.h"
#include "lib/crt.h"

#include <cstring>

namespace {

// 0073FD2C, 0073FD18 x 0073B86C (the cell size, halved), 0073CA98.
constexpr double kMargin = 2.0;
constexpr double kHalfCell = 1.0 * 0.5;
constexpr double kLevelsPerUnit = 16.0;
constexpr int kMiddle = 8;

// The shape's compiled table (C19) in place of game.exe's 0073FD5C: CRefObject's four slots.
class CUnitShapeClass final : public ei::CompiledTag<CUnitShapeClass> {
public:
    virtual void* DeleteScalar(unsigned flags) { return Shape()->RootDeletingDestruct(flags); }   // 0: 005B3570
    virtual void Serialize(ChunkArchive*) {}                     // 1: 00412580 (empty)
    virtual int OnShutdown() { return Shape()->Empty(); }        // 2: 005B3540
    virtual CRefObject* Clone() { return Shape()->Copy(); }      // 3: 005B34F0

private:
    CUnitShape* Shape() { return reinterpret_cast<CUnitShape*>(this); }
};

// operator new and the counted header's constructor: no references, no flags (the original
// writes them before it tests the allocation).
CUnitShape* NewShape() {
    auto* shape = gamecrt::operator_new_as<CUnitShape>();
    shape->refs_ = 0;
    shape->flags6_ = 0;
    shape->flags7_ = 0;
    return shape;
}

} // namespace

void* CUnitShape::Vtable() { return CUnitShapeClass::Vtable(); }

CUnitShape* CUnitShape::Construct(float newRadius) {
    radius = newRadius;
    ei::SetVtable(this, Vtable());
    std::memset(levels, 0, sizeof(levels));
    for (int dy = -kMiddle; dy < kMiddle; ++dy) {
        int rowSquared = dy * dy;
        for (int dx = -kMiddle; dx < kMiddle; ++dx) {
            double distance = std::sqrt(static_cast<double>(dx * dx + rowSquared));
            float level = static_cast<float>(((kMargin + radius) - distance * kHalfCell) * kLevelsPerUnit);
            int value = x87::fistp(level);
            if (value > 0) levels[dy + kMiddle][dx + kMiddle] = static_cast<std::uint8_t>(value);
        }
    }
    return this;
}
EI_REWRITTEN(0x005B3440, CUnitShape::Construct);

int CUnitShape::Empty() {
    ei::SetVtable(this, Vtable());
    std::memset(levels, 0, sizeof(levels));
    radius = -1.0f;
    return 0;
}
EI_REWRITTEN(0x005B3540, CUnitShape::Empty);

CUnitShape* CUnitShape::Copy() {
    CUnitShape* copy = NewShape();
    if (!copy) return nullptr;
    std::memcpy(copy->levels, levels, sizeof(levels));
    copy->radius = radius;
    ei::SetVtable(copy, Vtable());
    return copy;
}
EI_REWRITTEN(0x005B34F0, CUnitShape::Copy);

CUnitShape* __stdcall CreateUnitShape(const UnitShapeHandle* handle) {
    CUnitShape* shape = NewShape();
    if (!shape) return nullptr;
    return shape->Construct(handle->height);
}
EI_REWRITTEN(0x005B33D0, CreateUnitShape);
