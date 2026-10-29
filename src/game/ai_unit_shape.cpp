// The unit shapes (UnitShape, ai_unit_layer.h): the 16 x 16 cell levels a unit of a radius stamps on the AI
// map's unit layers. Shared through the shape cache, whose creator virtual makes them. See docs/notes/ai.md.
#include "ai_unit_layer.h"

#include "ei/x87.h"

#include <cstring>

namespace {

// Constants of the shape maths (the cell size, halved, among them).
constexpr double kMargin = 2.0;
constexpr double kHalfCell = 0.5;
constexpr double kLevelsPerUnit = 16.0;
constexpr int kMiddle = 8;

} // namespace

// No references, no flags.
UnitShape::UnitShape(float newRadius) {
    refs = 0;
    flags6 = 0;
    flags7 = 0;
    radius = newRadius;
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
}

UnitShape::UnitShape() {
    refs = 0;
    flags6 = 0;
    flags7 = 0;
    OnShutdown();
}

void UnitShape::OnShutdown() {
    std::memset(levels, 0, sizeof(levels));
    radius = -1.0f;
}

UnitShape* UnitShape::Clone() {
    auto* copy = new UnitShape();
    std::memcpy(copy->levels, levels, sizeof(levels));
    copy->radius = radius;
    return copy;
}

UnitShape* CreateUnitShape(const ShapeCacheHandle* handle) { return new UnitShape(handle->height); }
