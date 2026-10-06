// Create and Destroy of the client controller (CUnitClientSpecific slots 2 / 3). See
// unit_client_models.h and docs/notes/units.md.
#include "unit_client_models.h"

#include "ai_map.h"
#include "effects.h"

#include "ei/hook.h"

namespace {
constexpr int kNone = -1;
constexpr std::uint32_t kNoModel = 0xFFFFFFFFu;

// Takes the face texture and releases it.
void ReleaseFace(CUnitClientSpecific* self) {
    CEngineObject* face = self->faceTexture;
    self->faceTexture = nullptr;
    if (face) face->Release();
}
} // namespace

int CUnitClientSpecific::CreateClient() {
    sources[0] = nullptr;
    sources[1] = nullptr;
    sources[2] = nullptr;
    lastTime = 0.0f;
    MotionPath()->Reset();
    lastAnimationTime = 0.0f;
    lastAnimation = nullptr;
    weaponHidden = 0;
    arrowHidden = 0;
    snapHeading = 1;
    fadeIn = 1.0f;
    idleSound = kNone;
    unknown_08C = kNone;
    speechSound = kNone;
    lastSpeech = nullptr;
    lastSpeechTime = 0;
    flashCountdown = 0;
    footprintRepeats = kNone;
    footprint = kNone;
    blinks = 0;
    blinkTimer = 0;
    eyesClosed = 0;
    for (std::uint32_t& id : armourIds) id = kNoModel;
    for (std::uint32_t& id : weaponIds) id = kNoModel;
    ReleaseFace(this);
    blinks = 0;
    return 1;
}
EI_REPLACE_METHOD(0x00533FE0, CUnitClientSpecific::CreateClient);

void CUnitClientSpecific::DestroyClient() {
    for (CParticleSource*& source : sources) {
        if (!source) continue;
        source->Stop();
        reinterpret_cast<CEngineObject*>(source)->Release();
        source = nullptr;
    }
    ReleaseFace(this);
    blinks = 0;
}
EI_REPLACE_METHOD(0x005340C0, CUnitClientSpecific::DestroyClient);
