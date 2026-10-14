// Checks for the display's gamma ramp (renderer_gamma.cpp).
#include "verify.h"

#include "../../src/game/device_switchover.h"
#include "../../src/game/model.h"
#include "../../src/game/texture.h"

#include <ddraw.h>

#include <cstdint>
#include <cstring>

namespace {

constexpr GUID kGammaControlId = {0x69C11C3E, 0xB46B, 0x11D1, {0xAD, 0x7A, 0x00, 0xC0, 0x4F, 0xC2, 0x9B, 0x4E}};

// The primary surface's ramp as the driver reports it; false without one.
bool ReadRamp(const CSceneRenderer* renderer, DDGAMMARAMP* ramp) {
    std::memset(ramp, 0, sizeof(*ramp));
    if (!renderer->device_ || !renderer->device_->primary_ || !renderer->device_->primary_->surface) return false;
    auto* primary = static_cast<IDirectDrawSurface7*>(renderer->device_->primary_->surface);
    IDirectDrawGammaControl* control = nullptr;
    if (FAILED(primary->QueryInterface(kGammaControlId, reinterpret_cast<void**>(&control)))) return false;
    bool ok = SUCCEEDED(control->GetGammaRamp(0, ramp));
    control->Release();
    return ok;
}

// The largest difference of two ramps' entries.
int RampDifference(const DDGAMMARAMP& a, const DDGAMMARAMP& b) {
    int worst = 0;
    const WORD* x = a.red;
    const WORD* y = b.red;
    for (int i = 0; i < 3 * 256; ++i) {
        int d = static_cast<int>(x[i]) - static_cast<int>(y[i]);
        if (d < 0) d = -d;
        if (d > worst) worst = d;
    }
    return worst;
}

} // namespace

// 00504A60: the original runs, the ramp is read back (GetGammaRamp), then ours runs and the ramp is
// read again: the results (AL), the three kept values and both ramps must agree; ramp entries may
// differ by 1 (std::pow against the game's _CIpow before a truncation). Ours runs last, so its
// ramp stays. Reached at every start-up (WinMain) and at exit.
static void* s_gamma;
static bool __fastcall SetGammaVerify(CSceneRenderer* renderer, void*, float brightness, float contrast, float gamma) {
    using Original = bool(__fastcall*)(CSceneRenderer*, void*, float, float, float);
    if (device::WithoutDirectDraw()) {
        // The original would ask the stand-in for the capabilities: ours only (R9b). No gamma ramp
        // without DirectDraw: it returns false before it keeps the three values.
        const float before[3] = {renderer->brightness_, renderer->contrast_, renderer->gamma_};
        bool result = renderer->SetGamma(brightness, contrast, gamma);
        const float after[3] = {renderer->brightness_, renderer->contrast_, renderer->gamma_};
        ei::verify::Check("SetGamma", true, !result && std::memcmp(before, after, sizeof(before)) == 0,
                          "without DirectDraw (ours only): (%g, %g, %g) result %d", brightness, contrast, gamma, result);
        return result;
    }
    bool expected = reinterpret_cast<Original>(s_gamma)(renderer, nullptr, brightness, contrast, gamma);
    float kept[3] = {renderer->brightness_, renderer->contrast_, renderer->gamma_};
    DDGAMMARAMP before, after;
    bool hadRamp = ReadRamp(renderer, &before);
    bool result = renderer->SetGamma(brightness, contrast, gamma);
    float ours[3] = {renderer->brightness_, renderer->contrast_, renderer->gamma_};
    bool hasRamp = ReadRamp(renderer, &after);
    int difference = hadRamp && hasRamp ? RampDifference(before, after) : 0;
    bool ok = expected == result && std::memcmp(kept, ours, sizeof(kept)) == 0 && hadRamp == hasRamp && difference <= 1;
    ei::verify::Check("SetGamma", true, ok, "(%g, %g, %g) result %d / %d ramp %d / %d, difference %d", brightness,
                      contrast, gamma, expected, result, hadRamp, hasRamp, difference);
    return result;
}
EI_VERIFY(0x00504A60, SetGammaVerify, 9, s_gamma);
