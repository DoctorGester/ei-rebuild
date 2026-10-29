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
    // Identity rotations (w first); the positions are not initialised.
    localRotation = Quaternion{1.0f, 0.0f, 0.0f, 0.0f};
    worldRotation = Quaternion{1.0f, 0.0f, 0.0f, 0.0f};
    velocity = Vector3{0.0f, 0.0f, 0.0f};
    startTick = 0;
    moving = 0;
}

CEffect::~CEffect() = default;

bool CEffect::RunTick() { return true; }

// --- The class names and ids (ClassName / GetClassId; the exe's strings) ------------------------------------

const char* EffectSound::ClassName() { return "CEffectSound"; }
ClassId EffectSound::GetClassId() { return ClassId::EffectSound; }
const char* EffectSoundSeq::ClassName() { return "CEffectSoundSeq"; }
ClassId EffectSoundSeq::GetClassId() { return ClassId::EffectSoundSeq; }
const char* EffectAmbientSound::ClassName() { return "CEffectAmbientSound"; }
ClassId EffectAmbientSound::GetClassId() { return ClassId::EffectAmbientSound; }
const char* EffectPointLight::ClassName() { return "CEffectPointLight"; }
ClassId EffectPointLight::GetClassId() { return ClassId::EffectPointLight; }
const char* CEffectParticle::ClassName() { return "CEffectParticle"; }
ClassId CEffectParticle::GetClassId() { return ClassId::EffectParticle; }
const char* EffectWall::ClassName() { return "CEffectWall"; }
ClassId EffectWall::GetClassId() { return ClassId::EffectWall; }
const char* EffectLightning::ClassName() { return "CEffectLightning"; }
ClassId EffectLightning::GetClassId() { return ClassId::EffectLightning; }
const char* EffectArrow::ClassName() { return "CEffectArrow"; }
ClassId EffectArrow::GetClassId() { return ClassId::EffectArrow; }
const char* EffectArrowFigure::ClassName() { return "CEffectArrowFigure"; }
ClassId EffectArrowFigure::GetClassId() { return ClassId::EffectArrowFigure; }
const char* EffectShowVisibility::ClassName() { return "CEffectShowVisibility"; }
ClassId EffectShowVisibility::GetClassId() { return ClassId::EffectShowVisibility; }
const char* EffectMoshka::ClassName() { return "CEffectMoshka"; }
ClassId EffectMoshka::GetClassId() { return ClassId::EffectMoshka; }
const char* EffectTornado::ClassName() { return "CEffectTornado"; }
ClassId EffectTornado::GetClassId() { return ClassId::EffectTornado; }

EffectSound::EffectSound() {
    sound = -1;
    Reset();
}

EffectSound::~EffectSound() { Reset(); }

EffectSoundSeq::EffectSoundSeq() {
    sound = -1;
    Reset();
}

EffectSoundSeq::~EffectSoundSeq() { Reset(); }

EffectAmbientSound::EffectAmbientSound() {
    sound = -1;
    Reset();
}

EffectAmbientSound::~EffectAmbientSound() { Reset(); }

EffectPointLight::EffectPointLight() {
    Reset();
}

EffectPointLight::~EffectPointLight() { Reset(); }

// --- CEffectParticle and EffectWall -------------------------------------------------------------

CEffectParticle::CEffectParticle() {
    Reset();
}

CEffectParticle::~CEffectParticle() { Reset(); }

// The particle's constructor resets as the particle, then the wall resets as itself; the
// destructors the other way round.
EffectWall::EffectWall() { Reset(); }

EffectWall::~EffectWall() { Reset(); }

// No Reset: only the width and the flag.
EffectLightning::EffectLightning() {
    width = 0.0f;
    flag = 0;
}

EffectLightning::~EffectLightning() { Reset(); }

// --- The effect manager's factory ----------------------------------------------------------------

EngineObject* EffectManager::NewObject(int objectType) {
    EngineObject* object;
    switch (static_cast<ClassId>(objectType)) {
    case ClassId::EffectSound: object = new EffectSound(); break;
    case ClassId::EffectAmbientSound: object = new EffectAmbientSound(); break;
    case ClassId::EffectParticle: object = new CEffectParticle(); break;
    case ClassId::EffectPointLight: object = new EffectPointLight(); break;
    case ClassId::EffectLightning: object = new EffectLightning(); break;
    case ClassId::EffectArrow: object = new EffectArrow(); break;
    case ClassId::EffectWall: object = new EffectWall(); break;
    case ClassId::EffectArrowFigure: object = new EffectArrowFigure(); break;
    case ClassId::EffectShowVisibility: object = new EffectShowVisibility(); break;
    case ClassId::EffectSoundSeq: object = new EffectSoundSeq(); break;
    case ClassId::EffectMoshka: object = new EffectMoshka(); break;
    case ClassId::EffectTornado: object = new EffectTornado(); break;
    default: return nullptr;
    }
    if (object != nullptr) object->type = objectType;
    return object;
}


