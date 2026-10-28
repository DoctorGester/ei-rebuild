// Create and Destroy of the client controller. See unit_client_models.h and docs/notes/units.md.
#include "unit_client_models.h"

#include "ai_map.h"
#include "effects.h"
#include "texture.h"   // CTexture

namespace {
constexpr int kNone = -1;
constexpr std::uint32_t kNoModel = 0xFFFFFFFFu;

// Takes the face texture and releases it.
void ReleaseFace(CUnitClientSpecific* self) {
    CTexture* face = self->faceTexture;
    self->faceTexture = nullptr;
    if (face) face->Release();
}
} // namespace

int CUnitClientSpecific::Create() {
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
    otherSound = kNone;
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

void CUnitClientSpecific::Destroy() {
    for (CParticleSource*& source : sources) {
        if (!source) continue;
        source->Stop();
        source->Release();
        source = nullptr;
    }
    ReleaseFace(this);
    blinks = 0;
}

CUnitClientSpecific::~CUnitClientSpecific() {
    for (CParticleSource*& source : sources) {
        if (!source) continue;
        source->Stop();
        source->Release();
        source = nullptr;
    }
    ReleaseFace(this);   // with null; the release test after it finds null
    blinks = 0;
    motion.steps.clear();
}

CUnitClientSpecific::CUnitClientSpecific() {
    faceTexture = nullptr;
    fadeIn = 1.0f;
}
