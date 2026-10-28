// The unit shapes (CUnitShape, ai_unit_layer.h): the 16 x 16 cell levels a unit of a radius stamps on the AI
// map's unit layers. Shared through the shape cache, whose creator virtual makes them. See docs/notes/ai.md.
#include "ai_unit_layer.h"

#include "ei/x87.h"

#include <cstring>

namespace {

// Constants of the shape maths (the cell size, halved, among them).
constexpr double kMargin = 2.0;
constexpr double kHalfCell = 1.0 * 0.5;
constexpr double kLevelsPerUnit = 16.0;
constexpr int kMiddle = 8;

} // namespace

// No references, no flags (the original's creators write them before they test the allocation).
CUnitShape::CUnitShape(float newRadius) {
    refs_ = 0;
    flags6_ = 0;
    flags7_ = 0;
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

CUnitShape::CUnitShape() {
    refs_ = 0;
    flags6_ = 0;
    flags7_ = 0;
    OnShutdown();
}

int CUnitShape::OnShutdown() {
    std::memset(levels, 0, sizeof(levels));
    radius = -1.0f;
    return 0;
}

CUnitShape* CUnitShape::Clone() {
    auto* copy = new CUnitShape();
    if (!copy) return nullptr;
    std::memcpy(copy->levels, levels, sizeof(levels));
    copy->radius = radius;
    return copy;
}

CUnitShape* CreateUnitShape(const ShapeCacheHandle* handle) { return new CUnitShape(handle->height); }
