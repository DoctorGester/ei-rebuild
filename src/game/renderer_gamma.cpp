// The display's gamma ramp: brightness, contrast and gamma from the options become the gamma ramp (game.exe's
// primary surface's, through IDirectDrawGammaControl; ours the new renderer's, applied as the frame goes to
// the window). Called by WinMain at start-up and with zeros at exit, and by the options screen when a value
// changes; no caller reads the result. See docs/notes/renderer.md.
#include "model.h"     // CSceneRenderer
#include "texture.h"   // CTextureDevice, CDXSurface

#include "render/render.h"
#include "lib/nan_audit.h"

#include <ddraw.h>

#include <bit>
#include <cmath>
#include <cstdint>

namespace {

const float kLevelStep = std::bit_cast<float>(0x3B808081u);   // 1/255
constexpr float kRampScale = 65280.0f;
constexpr int kLevels = 256;

// A branchless minimum / maximum (the comparison's bit as a mask): `limit` when it is below / above `v`.
float AtMost(float v, float limit) {
    EI_NAN_AUDIT(v);
    return limit < v ? limit : v;
}
float AtLeast(float v, float limit) {
    EI_NAN_AUDIT(v);
    return limit < v ? v : limit;
}
float Clamp(float v, float lo, float hi) { return AtLeast(AtMost(v, hi), lo); }

// The ramp of the clamped, halved settings: every channel the same.
void BuildRamp(DDGAMMARAMP& ramp, float brightness, float contrast, float gamma) {
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
        auto level = static_cast<WORD>(static_cast<int>(value * kRampScale));   // _ftol: truncated
        ramp.red[i] = level;
        ramp.green[i] = level;
        ramp.blue[i] = level;
    }
}

} // namespace

bool CSceneRenderer::SetGamma(float brightness, float contrast, float gamma) {
    CTextureDevice* device = device_;
    if (!device) return false;
    // No primary surface: the new renderer applies the ramp as the frame goes to the window
    // (render::SetGammaRamp), so there is always gamma control (DirectDraw answers false without
    // DDCAPS2_PRIMARYGAMMA, else SetGammaRamp's success).
    brightness_ = brightness;
    contrast_ = contrast;
    gamma_ = gamma;
    DDGAMMARAMP ramp;
    BuildRamp(ramp, Clamp(brightness, -1.0f, 1.0f) * 0.5f, Clamp(contrast, -1.0f, 1.0f) * 0.5f,
              Clamp(gamma, -1.0f, 1.0f) * 0.5f);
    render::SetGammaRamp(ramp.red, ramp.green, ramp.blue);
    return true;
}
