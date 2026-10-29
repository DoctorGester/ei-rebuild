// The effects' constructor and destructor bodies, their deleting destructors and the state field's
// constructor and emptying. See world_effect.h and docs/notes/effects.md ("Missiles and visibility").
#include "console.h"
#include "net_link.h"
#include "world_effect.h"

#include <new>

namespace {

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

bool CEffect::RunTick() { return true; }

// --- The class names and ids (ClassName / GetClassId; the exe's strings) ------------------------------------

const char* CEffectSound::ClassName() { return "CEffectSound"; }
ClassId CEffectSound::GetClassId() { return ClassId::EffectSound; }
const char* CEffectSoundSeq::ClassName() { return "CEffectSoundSeq"; }
ClassId CEffectSoundSeq::GetClassId() { return ClassId::EffectSoundSeq; }
const char* CEffectAmbientSound::ClassName() { return "CEffectAmbientSound"; }
ClassId CEffectAmbientSound::GetClassId() { return ClassId::EffectAmbientSound; }
const char* CEffectPointLight::ClassName() { return "CEffectPointLight"; }
ClassId CEffectPointLight::GetClassId() { return ClassId::EffectPointLight; }
const char* CEffectParticle::ClassName() { return "CEffectParticle"; }
ClassId CEffectParticle::GetClassId() { return ClassId::EffectParticle; }
const char* CEffectWall::ClassName() { return "CEffectWall"; }
ClassId CEffectWall::GetClassId() { return ClassId::EffectWall; }
const char* CEffectLightning::ClassName() { return "CEffectLightning"; }
ClassId CEffectLightning::GetClassId() { return ClassId::EffectLightning; }
const char* CEffectArrow::ClassName() { return "CEffectArrow"; }
ClassId CEffectArrow::GetClassId() { return ClassId::EffectArrow; }
const char* CEffectArrowFigure::ClassName() { return "CEffectArrowFigure"; }
ClassId CEffectArrowFigure::GetClassId() { return ClassId::EffectArrowFigure; }
const char* CEffectShowVisibility::ClassName() { return "CEffectShowVisibility"; }
ClassId CEffectShowVisibility::GetClassId() { return ClassId::EffectShowVisibility; }
const char* CEffectMoshka::ClassName() { return "CEffectMoshka"; }
ClassId CEffectMoshka::GetClassId() { return ClassId::EffectMoshka; }
const char* CEffectTornado::ClassName() { return "CEffectTornado"; }
ClassId CEffectTornado::GetClassId() { return ClassId::EffectTornado; }

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
    Reset();
}

CEffectPointLight::~CEffectPointLight() { Reset(); }

// --- CEffectParticle and CEffectWall -------------------------------------------------------------

CEffectParticle::CEffectParticle() {
    Reset();
}

CEffectParticle::~CEffectParticle() { Reset(); }

// The particle's constructor resets as the particle, then the wall resets as itself; the
// destructors the other way round.
CEffectWall::CEffectWall() { Reset(); }

CEffectWall::~CEffectWall() { Reset(); }

// No Reset: only the width and the flag.
CEffectLightning::CEffectLightning() {
    width_ = 0.0f;
    flag_ = 0;
}

CEffectLightning::~CEffectLightning() { Reset(); }

// --- The effect manager's factory ----------------------------------------------------------------

CEngineObject* CEffectManager::NewObject(int type) {
    CEngineObject* object;
    switch (static_cast<ClassId>(type)) {
    case ClassId::EffectSound: object = new CEffectSound(); break;
    case ClassId::EffectAmbientSound: object = new CEffectAmbientSound(); break;
    case ClassId::EffectParticle: object = new CEffectParticle(); break;
    case ClassId::EffectPointLight: object = new CEffectPointLight(); break;
    case ClassId::EffectLightning: object = new CEffectLightning(); break;
    case ClassId::EffectArrow: object = new CEffectArrow(); break;
    case ClassId::EffectWall: object = new CEffectWall(); break;
    case ClassId::EffectArrowFigure: object = new CEffectArrowFigure(); break;
    case ClassId::EffectShowVisibility: object = new CEffectShowVisibility(); break;
    case ClassId::EffectSoundSeq: object = new CEffectSoundSeq(); break;
    case ClassId::EffectMoshka: object = new CEffectMoshka(); break;
    case ClassId::EffectTornado: object = new CEffectTornado(); break;
    default: return nullptr;
    }
    if (object != nullptr) object->type_ = type;
    return object;
}


