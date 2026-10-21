// The scene renderer's members (CSceneRenderer, model.h) that are game code rather than device code.
#include "model.h"

#include "ei/hook.h"
#include "ei/x87.h"

#include <cstdint>

namespace {
constexpr float kColourScale = 255.0f;   // 0073C370
} // namespace

void CSceneRenderer::SetFogColour(float r, float g, float b) {
    // Each product is stored as a float, then FISTP rounds it to the nearest integer.
    const std::uint32_t red = static_cast<std::uint32_t>(x87::fistp(r * kColourScale));
    const std::uint32_t green = static_cast<std::uint32_t>(x87::fistp(g * kColourScale));
    const std::uint32_t blue = static_cast<std::uint32_t>(x87::fistp(b * kColourScale));
    const std::uint32_t colour = (((red | 0xFFFFFF00u) << 8 | green) << 8) | blue;
    fogColour_ = colour;
    SetFogColourState(colour);
}
EI_REWRITTEN(0x004BA120, CSceneRenderer::SetFogColour);
