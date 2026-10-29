// The display's gamma ramp: brightness, contrast and gamma from the options become the gamma ramp (the new
// renderer's, applied as the frame goes to the window). Called by WinMain at start-up and with zeros at exit, and by the options screen when a value
// changes; no caller reads the result. See docs/notes/renderer.md.
#include "model.h"     // SceneRenderer
#include "texture.h"   // TextureDevice, Surface

#include "render/render.h"
#include "lib/nan_audit.h"
#include "lib/float_select.h"

#include <bit>
#include <cmath>
#include <cstdint>

namespace {

constexpr float kLevelStep = 0.003921569f;   // 1/255
static_assert(std::bit_cast<std::uint32_t>(kLevelStep) == 0x3B808081u);
constexpr float kRampScale = 65280.0f;
constexpr int kLevels = 256;

// A branchless minimum / maximum (the comparison's bit as a mask): `limit` when it is below / above `v`.
float Clamp(float v, float lo, float hi) { return MaxKeepA(lo, MinKeepB(hi, v)); }

// The ramp of the clamped, halved settings (one table: every channel the same).
void BuildRamp(std::uint16_t (&ramp)[kLevels], float brightness, float contrast, float gamma) {
    float scale = std::fabs(contrast) * 4.0f + 1.0f;
    EI_NAN_AUDIT(contrast, gamma);
    if (contrast < 0.0f) scale = 1.0f / scale;
    float offset = (1.0f - scale) * 0.5f;
    float exponent = 1.0f;
    if (gamma > 0.0f) exponent = 1.0f / (gamma * 5.0f + 1.0f);
    else if (gamma < 0.0f) exponent = 1.0f / (gamma * 0.5f + 1.0f);

    for (int i = 0; i < kLevels; ++i) {
        float value = static_cast<float>(std::pow(static_cast<double>(i) * kLevelStep, static_cast<double>(exponent)) *
                                             scale + offset);
        value = Clamp(value, 0.0f, 1.0f);
        value = Clamp(value + brightness, 0.0f, 1.0f);
        auto level = static_cast<std::uint16_t>(static_cast<int>(value * kRampScale));   // _ftol: truncated
        ramp[i] = level;
    }
}

} // namespace

bool SceneRenderer::SetGamma(float newBrightness, float newContrast, float newGamma) {
    TextureDevice* currentDevice = device;
    if (!currentDevice) return false;
    // The renderer applies the ramp as the frame goes to the window (render::SetGammaRamp), so there is
    // always gamma control.
    brightness = newBrightness;
    contrast = newContrast;
    gamma = newGamma;
    std::uint16_t ramp[kLevels];
    BuildRamp(ramp, Clamp(newBrightness, -1.0f, 1.0f) * 0.5f, Clamp(newContrast, -1.0f, 1.0f) * 0.5f,
              Clamp(newGamma, -1.0f, 1.0f) * 0.5f);
    render::SetGammaRamp(ramp, ramp, ramp);
    return true;
}
