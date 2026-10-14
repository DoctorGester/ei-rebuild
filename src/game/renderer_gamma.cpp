// The display's gamma ramp (00504A60, virtual slot 22 of the renderer at 007AF060, thiscall, RET
// 0xC): brightness, contrast and gamma from the options become the primary surface's gamma ramp
// through IDirectDrawGammaControl. Called by WinMain (00456440) at start-up and with zeros at exit,
// and by the options screen (00650B6E, 00650CAB, 006510BA) when a value changes; no caller reads the
// result. See docs/notes/renderer.md.
#include "device.h"   // the old device's calls
#include "model.h"     // CSceneRenderer
#include "texture.h"   // CTextureDevice, CDXSurface

#include "ei/hook.h"
#include "ei/x87.h"

#include <ddraw.h>

#include <bit>
#include <cmath>
#include <cstdint>

namespace {

// IID_IDirectDrawGammaControl (the game's copy is at 00745B90).
constexpr GUID kGammaControlId = {0x69C11C3E, 0xB46B, 0x11D1, {0xAD, 0x7A, 0x00, 0xC0, 0x4F, 0xC2, 0x9B, 0x4E}};

const float kLevelStep = std::bit_cast<float>(0x3B808081u);   // 1/255 (0073C36C)
constexpr float kRampScale = 65280.0f;                         // 0073D9A8
constexpr int kLevels = 256;

// The original's branchless minimum / maximum (FCOMP; FNSTSW; the C0 bit as a mask): `limit` when
// it is below / above `v` or `v` is NaN.
float AtMost(float v, float limit) { return x87::IsNaN(v) || limit < v ? limit : v; }
float AtLeast(float v, float limit) { return x87::IsNaN(v) || limit < v ? v : limit; }
float Clamp(float v, float lo, float hi) { return AtLeast(AtMost(v, hi), lo); }

// The ramp of the clamped, halved settings: every channel the same.
void BuildRamp(DDGAMMARAMP& ramp, float brightness, float contrast, float gamma) {
    float scale = std::fabs(contrast) * 4.0f + 1.0f;
    if (x87::IsNaN(contrast) || contrast < 0.0f) scale = 1.0f / scale;
    float offset = (1.0f - scale) * 0.5f;
    float exponent = 1.0f;
    if (gamma > 0.0f) exponent = 1.0f / (gamma * 5.0f + 1.0f);
    else if (x87::IsNaN(gamma) || gamma < 0.0f) exponent = 1.0f / (gamma * 0.5f + 1.0f);

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
    DDCAPS driverCaps{}, emulatedCaps{};
    driverCaps.dwSize = sizeof(DDCAPS);
    emulatedCaps.dwSize = sizeof(DDCAPS);
    device::GetCaps(static_cast<IDirectDraw7*>(device->ddraw_), &driverCaps, &emulatedCaps);
    if (!(driverCaps.dwCaps2 & DDCAPS2_PRIMARYGAMMA)) return false;
    DWORD flags = (driverCaps.dwCaps2 & DDCAPS2_CANCALIBRATEGAMMA) ? DDSGR_CALIBRATE : 0;

    IDirectDrawGammaControl* control = nullptr;
    auto* primary = static_cast<IDirectDrawSurface7*>(device->primary_->surface);
    if (FAILED(primary->QueryInterface(kGammaControlId, reinterpret_cast<void**>(&control)))) return false;
    brightness_ = brightness;
    contrast_ = contrast;
    gamma_ = gamma;
    DDGAMMARAMP ramp;
    BuildRamp(ramp, Clamp(brightness, -1.0f, 1.0f) * 0.5f, Clamp(contrast, -1.0f, 1.0f) * 0.5f,
              Clamp(gamma, -1.0f, 1.0f) * 0.5f);
    HRESULT result = control->SetGammaRamp(flags, &ramp);
    control->Release();
    return result == DD_OK;
}
// Slot 22 is not declared virtual in CSceneRenderer, so the member is registered as it is.
EI_REPLACE_METHOD(0x00504A60, CSceneRenderer::SetGamma);
