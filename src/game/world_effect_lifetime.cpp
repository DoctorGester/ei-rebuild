// The effects' constructor and destructor bodies, their deleting destructors and the state field's
// constructor and emptying. See world_effect.h and docs/notes/effects.md ("Missiles and visibility").
#include "world_effect.h"

#include "console.h"
#include "net_link.h"

#include "../lib/crt.h"
#include "../lib/mfc.h"
#include "ei/hook.h"
#include "game_data.h"
#include "record_arrays.h"

#include <new>


namespace {

// A counted pointer let go (Release); the original tests it for null first.
void ReleaseIfSet(void* object) {
    if (object != nullptr) static_cast<CEngineObject*>(object)->Release();
}

} // namespace

// --- Streams and the state field -----------------------------------------------------------------

EI_REWRITTEN_SPECIAL(0x00677EC0, DeltaField::DeltaField);
EI_REWRITTEN_SPECIAL(0x0066A300, DeltaField::DeltaField);
EI_REWRITTEN_SPECIAL(0x00510340, DeltaField::~DeltaField);

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
EI_REWRITTEN_SPECIAL(0x0067B050, CEffect::CEffect);

CEffect::~CEffect() = default;
EI_REWRITTEN_SPECIAL(0x00675FB0, CEffect::~CEffect);
EI_REWRITTEN_SPECIAL(0x0067B390, CEffect::~CEffect);
EI_REWRITTEN_SPECIAL(0x00676910, CEffect::~CEffect);
EI_REWRITTEN_SPECIAL(0x0067B430, CEffect::~CEffect);

int CEffect::RunTick() { return 1; }
EI_REWRITTEN(0x00676900, CEffect::RunTick);

// --- The class names and ids (GetClassName / GetClassId; the exe's strings) ------------------------------------

const char* CEffectSound::GetClassName() { return "CEffectSound"; }
int CEffectSound::GetClassId() { return 0x48; }
EI_REWRITTEN(0x00676060, CEffectSound::GetClassName);
EI_REWRITTEN(0x00676070, CEffectSound::GetClassId);
const char* CEffectSoundSeq::GetClassName() { return "CEffectSoundSeq"; }
int CEffectSoundSeq::GetClassId() { return 0x63; }
EI_REWRITTEN(0x00676940, CEffectSoundSeq::GetClassName);
EI_REWRITTEN(0x00676950, CEffectSoundSeq::GetClassId);
const char* CEffectAmbientSound::GetClassName() { return "CEffectAmbientSound"; }
int CEffectAmbientSound::GetClassId() { return 0x49; }
EI_REWRITTEN(0x006776A0, CEffectAmbientSound::GetClassName);
EI_REWRITTEN(0x006776B0, CEffectAmbientSound::GetClassId);
const char* CEffectPointLight::GetClassName() { return "CEffectPointLight"; }
int CEffectPointLight::GetClassId() { return 0x4B; }
EI_REWRITTEN(0x00677F80, CEffectPointLight::GetClassName);
EI_REWRITTEN(0x00677F90, CEffectPointLight::GetClassId);
const char* CEffectParticle::GetClassName() { return "CEffectParticle"; }
int CEffectParticle::GetClassId() { return 0x4A; }
EI_REWRITTEN(0x00678950, CEffectParticle::GetClassName);
EI_REWRITTEN(0x00678960, CEffectParticle::GetClassId);
const char* CEffectWall::GetClassName() { return "CEffectWall"; }
int CEffectWall::GetClassId() { return 0x5A; }
EI_REWRITTEN(0x006799A0, CEffectWall::GetClassName);
EI_REWRITTEN(0x006799B0, CEffectWall::GetClassId);
const char* CEffectLightning::GetClassName() { return "CEffectLightning"; }
int CEffectLightning::GetClassId() { return 0x4D; }
EI_REWRITTEN(0x0067A070, CEffectLightning::GetClassName);
EI_REWRITTEN(0x0067A080, CEffectLightning::GetClassId);
const char* CEffectArrow::GetClassName() { return "CEffectArrow"; }
int CEffectArrow::GetClassId() { return 0x4E; }
EI_REWRITTEN(0x0067B180, CEffectArrow::GetClassName);
EI_REWRITTEN(0x0067B190, CEffectArrow::GetClassId);
const char* CEffectArrowFigure::GetClassName() { return "CEffectArrowFigure"; }
int CEffectArrowFigure::GetClassId() { return 0x5B; }
EI_REWRITTEN(0x0067BC90, CEffectArrowFigure::GetClassName);
EI_REWRITTEN(0x0067BCA0, CEffectArrowFigure::GetClassId);
const char* CEffectShowVisibility::GetClassName() { return "CEffectShowVisibility"; }
int CEffectShowVisibility::GetClassId() { return 0x5E; }
EI_REWRITTEN(0x0067C450, CEffectShowVisibility::GetClassName);
EI_REWRITTEN(0x0067C460, CEffectShowVisibility::GetClassId);
const char* CEffectMoshka::GetClassName() { return "CEffectMoshka"; }
int CEffectMoshka::GetClassId() { return 0x64; }
EI_REWRITTEN(0x0067CAB0, CEffectMoshka::GetClassName);
EI_REWRITTEN(0x0067CAC0, CEffectMoshka::GetClassId);
const char* CEffectTornado::GetClassName() { return "CEffectTornado"; }
int CEffectTornado::GetClassId() { return 0x65; }
EI_REWRITTEN(0x0067D7C0, CEffectTornado::GetClassName);
EI_REWRITTEN(0x0067D7D0, CEffectTornado::GetClassId);


CEffectSound::CEffectSound() {
    sound_ = -1;
    Reset();
}
EI_REWRITTEN_SPECIAL(0x00675DD0, CEffectSound::CEffectSound);

CEffectSound::~CEffectSound() {
    Reset();
    Reset();   // again, the virtual
}
EI_REWRITTEN_SPECIAL(0x006760A0, CEffectSound::~CEffectSound);
EI_REWRITTEN_SPECIAL(0x00676080, CEffectSound::~CEffectSound);


CEffectSoundSeq::CEffectSoundSeq() {
    sound_ = -1;
    Reset();
}
EI_REWRITTEN_SPECIAL(0x00676760, CEffectSoundSeq::CEffectSoundSeq);

CEffectSoundSeq::~CEffectSoundSeq() {
    Reset();
    Reset();   // again, the virtual
}
EI_REWRITTEN_SPECIAL(0x00676980, CEffectSoundSeq::~CEffectSoundSeq);
EI_REWRITTEN_SPECIAL(0x00676960, CEffectSoundSeq::~CEffectSoundSeq);


CEffectAmbientSound::CEffectAmbientSound() {
    sound_ = -1;
    Reset();
}
EI_REWRITTEN_SPECIAL(0x00677510, CEffectAmbientSound::CEffectAmbientSound);

CEffectAmbientSound::~CEffectAmbientSound() {
    Reset();
    Reset();   // again, the virtual
}
EI_REWRITTEN_SPECIAL(0x006776E0, CEffectAmbientSound::~CEffectAmbientSound);
EI_REWRITTEN_SPECIAL(0x006776C0, CEffectAmbientSound::~CEffectAmbientSound);


CEffectPointLight::CEffectPointLight() {
    light_ = nullptr;
    Reset();
}
EI_REWRITTEN_SPECIAL(0x00677D30, CEffectPointLight::CEffectPointLight);

CEffectPointLight::~CEffectPointLight() {
    Reset();
    ReleaseIfSet(light_);
}
EI_REWRITTEN_SPECIAL(0x00677FC0, CEffectPointLight::~CEffectPointLight);
EI_REWRITTEN_SPECIAL(0x00677FA0, CEffectPointLight::~CEffectPointLight);

// --- CEffectParticle and CEffectWall -------------------------------------------------------------

CEffectParticle::CEffectParticle() {
    source_ = nullptr;
    Reset();
}
EI_REWRITTEN_SPECIAL(0x006787B0, CEffectParticle::CEffectParticle);

CEffectParticle::~CEffectParticle() {
    Reset();
    ReleaseIfSet(source_);
}
EI_REWRITTEN_SPECIAL(0x00678990, CEffectParticle::~CEffectParticle);
EI_REWRITTEN_SPECIAL(0x00678970, CEffectParticle::~CEffectParticle);

// The particle's constructor resets as the particle, then the wall resets as itself; the
// destructors the other way round.
CEffectWall::CEffectWall() { Reset(); }
EI_REWRITTEN_SPECIAL(0x006797E0, CEffectWall::CEffectWall);

CEffectWall::~CEffectWall() { Reset(); }
EI_REWRITTEN_SPECIAL(0x006799E0, CEffectWall::~CEffectWall);
EI_REWRITTEN_SPECIAL(0x006799C0, CEffectWall::~CEffectWall);


// No Reset: only the bolt, the width and the flag.
CEffectLightning::CEffectLightning() {
    bolt_ = nullptr;
    width_ = 0.0f;
    flag_ = 0;
}
EI_REWRITTEN_SPECIAL(0x00679FC0, CEffectLightning::CEffectLightning);

CEffectLightning::~CEffectLightning() {
    Reset();
    ReleaseIfSet(bolt_);
}
EI_REWRITTEN_SPECIAL(0x0067A0B0, CEffectLightning::~CEffectLightning);
EI_REWRITTEN_SPECIAL(0x0067A090, CEffectLightning::~CEffectLightning);

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
EI_REWRITTEN(0x0067E170, CEffectManager::NewObject);

void DeltaFieldElements::Construct(DeltaField* fields, int count) {
    mfc::PlainElements<DeltaField>::Construct(fields, count);
    for (; count--; ++fields) new (fields) DeltaField();
}
void DeltaFieldElements::Destruct(DeltaField* fields, int count) {
    for (; count--; ++fields) fields->~DeltaField();
}
