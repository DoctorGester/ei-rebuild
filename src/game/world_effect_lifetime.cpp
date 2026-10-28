// The effects' constructor and destructor bodies, their deleting destructors and the state field's
// constructor and emptying. See world_effect.h and docs/notes/effects.md ("Missiles and visibility").
#include "world_effect.h"

#include "console.h"
#include "net_link.h"

#include "../lib/crt.h"
#include "../lib/mfc.h"
#include "game_data.h"

#include <new>


namespace {

// A counted pointer let go (Release); the original tests it for null first.
void ReleaseIfSet(void* object) {
    if (object != nullptr) static_cast<CEngineObject*>(object)->Release();
}

} // namespace

// --- The effect base -----------------------------------------------------------------------------

CEffect::CEffect() {
    ClearFields();
    // Identity rotations (w first); the positions are not initialised.
    localRotation_ = Quaternion{1.0f, 0.0f, 0.0f, 0.0f};
    worldRotation_ = Quaternion{1.0f, 0.0f, 0.0f, 0.0f};
    velocity_ = Vector3{0.0f, 0.0f, 0.0f};
    startTick_ = 0;
    moving_ = 0;
}

CEffect::~CEffect() = default;

int CEffect::RunTick() { return 1; }

// --- The class names and ids (GetClassName / GetClassId; the exe's strings) ------------------------------------

const char* CEffectSound::GetClassName() { return "CEffectSound"; }
int CEffectSound::GetClassId() { return 0x48; }
const char* CEffectSoundSeq::GetClassName() { return "CEffectSoundSeq"; }
int CEffectSoundSeq::GetClassId() { return 0x63; }
const char* CEffectAmbientSound::GetClassName() { return "CEffectAmbientSound"; }
int CEffectAmbientSound::GetClassId() { return 0x49; }
const char* CEffectPointLight::GetClassName() { return "CEffectPointLight"; }
int CEffectPointLight::GetClassId() { return 0x4B; }
const char* CEffectParticle::GetClassName() { return "CEffectParticle"; }
int CEffectParticle::GetClassId() { return 0x4A; }
const char* CEffectWall::GetClassName() { return "CEffectWall"; }
int CEffectWall::GetClassId() { return 0x5A; }
const char* CEffectLightning::GetClassName() { return "CEffectLightning"; }
int CEffectLightning::GetClassId() { return 0x4D; }
const char* CEffectArrow::GetClassName() { return "CEffectArrow"; }
int CEffectArrow::GetClassId() { return 0x4E; }
const char* CEffectArrowFigure::GetClassName() { return "CEffectArrowFigure"; }
int CEffectArrowFigure::GetClassId() { return 0x5B; }
const char* CEffectShowVisibility::GetClassName() { return "CEffectShowVisibility"; }
int CEffectShowVisibility::GetClassId() { return 0x5E; }
const char* CEffectMoshka::GetClassName() { return "CEffectMoshka"; }
int CEffectMoshka::GetClassId() { return 0x64; }
const char* CEffectTornado::GetClassName() { return "CEffectTornado"; }
int CEffectTornado::GetClassId() { return 0x65; }

CEffectSound::CEffectSound() {
    sound_ = -1;
    Reset();
}

CEffectSound::~CEffectSound() {
    Reset();
    Reset();   // again, the virtual
}

CEffectSoundSeq::CEffectSoundSeq() {
    sound_ = -1;
    Reset();
}

CEffectSoundSeq::~CEffectSoundSeq() {
    Reset();
    Reset();   // again, the virtual
}

CEffectAmbientSound::CEffectAmbientSound() {
    sound_ = -1;
    Reset();
}

CEffectAmbientSound::~CEffectAmbientSound() {
    Reset();
    Reset();   // again, the virtual
}

CEffectPointLight::CEffectPointLight() {
    light_ = nullptr;
    Reset();
}

CEffectPointLight::~CEffectPointLight() {
    Reset();
    ReleaseIfSet(light_);
}

// --- CEffectParticle and CEffectWall -------------------------------------------------------------

CEffectParticle::CEffectParticle() {
    source_ = nullptr;
    Reset();
}

CEffectParticle::~CEffectParticle() {
    Reset();
    ReleaseIfSet(source_);
}

// The particle's constructor resets as the particle, then the wall resets as itself; the
// destructors the other way round.
CEffectWall::CEffectWall() { Reset(); }

CEffectWall::~CEffectWall() { Reset(); }

// No Reset: only the bolt, the width and the flag.
CEffectLightning::CEffectLightning() {
    bolt_ = nullptr;
    width_ = 0.0f;
    flag_ = 0;
}

CEffectLightning::~CEffectLightning() {
    Reset();
    ReleaseIfSet(bolt_);
}

// --- The effect manager's factory ----------------------------------------------------------------

CEngineObject* CEffectManager::NewObject(int type) {
    CEngineObject* object;
    switch (type) {
    case 0x48: object = new CEffectSound(); break;
    case 0x49: object = new CEffectAmbientSound(); break;
    case 0x4A: object = new CEffectParticle(); break;
    case 0x4B: object = new CEffectPointLight(); break;
    case 0x4D: object = new CEffectLightning(); break;
    case 0x4E: object = new CEffectArrow(); break;
    case 0x5A: object = new CEffectWall(); break;
    case 0x5B: object = new CEffectArrowFigure(); break;
    case 0x5E: object = new CEffectShowVisibility(); break;
    case 99: object = new CEffectSoundSeq(); break;
    case 100: object = new CEffectMoshka(); break;
    case 0x65: object = new CEffectTornado(); break;
    default: return nullptr;
    }
    if (object != nullptr) object->type_ = type;
    return object;
}


