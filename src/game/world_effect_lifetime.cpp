// The effects' constructor and destructor bodies, their deleting destructors and the state field's
// constructor and emptying. See world_effect.h and docs/notes/effects.md ("Missiles and visibility").
#include "console.h"
#include "net_link.h"
#include "world_effect.h"

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

// --- The class names and ids (ClassName / GetClassId; the exe's strings) ------------------------------------

const char* CEffectSound::ClassName() { return "CEffectSound"; }
ClassId CEffectSound::GetClassId() { return kClassEffectSound; }
const char* CEffectSoundSeq::ClassName() { return "CEffectSoundSeq"; }
ClassId CEffectSoundSeq::GetClassId() { return kClassEffectSoundSeq; }
const char* CEffectAmbientSound::ClassName() { return "CEffectAmbientSound"; }
ClassId CEffectAmbientSound::GetClassId() { return kClassEffectAmbientSound; }
const char* CEffectPointLight::ClassName() { return "CEffectPointLight"; }
ClassId CEffectPointLight::GetClassId() { return kClassEffectPointLight; }
const char* CEffectParticle::ClassName() { return "CEffectParticle"; }
ClassId CEffectParticle::GetClassId() { return kClassEffectParticle; }
const char* CEffectWall::ClassName() { return "CEffectWall"; }
ClassId CEffectWall::GetClassId() { return kClassEffectWall; }
const char* CEffectLightning::ClassName() { return "CEffectLightning"; }
ClassId CEffectLightning::GetClassId() { return kClassEffectLightning; }
const char* CEffectArrow::ClassName() { return "CEffectArrow"; }
ClassId CEffectArrow::GetClassId() { return kClassEffectArrow; }
const char* CEffectArrowFigure::ClassName() { return "CEffectArrowFigure"; }
ClassId CEffectArrowFigure::GetClassId() { return kClassEffectArrowFigure; }
const char* CEffectShowVisibility::ClassName() { return "CEffectShowVisibility"; }
ClassId CEffectShowVisibility::GetClassId() { return kClassEffectShowVisibility; }
const char* CEffectMoshka::ClassName() { return "CEffectMoshka"; }
ClassId CEffectMoshka::GetClassId() { return kClassEffectMoshka; }
const char* CEffectTornado::ClassName() { return "CEffectTornado"; }
ClassId CEffectTornado::GetClassId() { return kClassEffectTornado; }

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
    case kClassEffectSound: object = new CEffectSound(); break;
    case kClassEffectAmbientSound: object = new CEffectAmbientSound(); break;
    case kClassEffectParticle: object = new CEffectParticle(); break;
    case kClassEffectPointLight: object = new CEffectPointLight(); break;
    case kClassEffectLightning: object = new CEffectLightning(); break;
    case kClassEffectArrow: object = new CEffectArrow(); break;
    case kClassEffectWall: object = new CEffectWall(); break;
    case kClassEffectArrowFigure: object = new CEffectArrowFigure(); break;
    case kClassEffectShowVisibility: object = new CEffectShowVisibility(); break;
    case kClassEffectSoundSeq: object = new CEffectSoundSeq(); break;
    case kClassEffectMoshka: object = new CEffectMoshka(); break;
    case kClassEffectTornado: object = new CEffectTornado(); break;
    default: return nullptr;
    }
    if (object != nullptr) object->type_ = type;
    return object;
}


