// The effects' constructor and destructor bodies, their slot 0 (deleting destructor) and the state
// field's constructor and emptying. See world_effect.h and docs/notes/effects.md ("Missiles and visibility").
#include "world_effect.h"

#include "console.h"
#include "net_link.h"

#include "../lib/crt.h"
#include "../lib/mfc.h"
#include "ei/hook.h"
#include "ei/vcall.h"

#include <new>

using ei::SetVtable;

namespace {

// The effect base's vtable (slots 11, 12, 21-23 pure), stored while the base part is built.
std::uintptr_t EffectBaseVtable() { return static_cast<std::uintptr_t>(0x007434AC); }

// A counted pointer let go (slot 4); the original tests it for null first.
void ReleaseIfSet(void* object) {
    if (object != nullptr) static_cast<CEngineObject*>(object)->Release();
}

// Slot 0: the destructor body, then operator delete when bit 0 of `flags` is set.
template <typename T>
T* DeleteAfter(T* object, unsigned flags) {
    object->Destruct();
    if (flags & 1) gamecrt::operator_delete(object);
    return object;
}

} // namespace

// --- Streams and the state field -----------------------------------------------------------------

CMemoryStream* CMemoryStream::ConstructQueueStream() {
    Construct();
    SetVtable(this, netlink::kNodeStreamVtable);
    return this;
}
EI_REPLACE_METHOD(0x0066C4F0, CMemoryStream::ConstructQueueStream);

DeltaField* DeltaField::Construct() {
    snapshot[0].Construct();
    snapshot[1].Construct();
    out[0].ConstructQueueStream();
    out[1].ConstructQueueStream();
    return this;
}
EI_REPLACE_METHOD(0x00677EC0, DeltaField::Construct);
EI_REPLACE_METHOD(0x0066A300, DeltaField::Construct);

void DeltaField::Empty() {
    snapshot[0].Resize(0);
    snapshot[1].Resize(0);
    out[0].Resize(0);
    out[1].Resize(0);
}
EI_REPLACE_METHOD(0x00675F70, DeltaField::Empty);

void DeltaField::Destruct() {
    out[1].Destruct();
    out[0].Destruct();
    snapshot[1].Destruct();
    snapshot[0].Destruct();
}

// --- The effect base -----------------------------------------------------------------------------

void CEffect::ConstructEffect() {
    ConstructBase();
    velocity_ = Vector3{0.0f, 0.0f, 0.0f};
    state_.Construct();
    startTick_ = 0;
    moving_ = 0;
    SetVtable(this, EffectBaseVtable());
}

void CEffect::DestructEffect() {
    state_.Destruct();
    SetVtable(this, CTransformObject::kVtableAddress);
    DestructBase();
}
EI_REPLACE_METHOD(0x00675FB0, CEffect::DestructEffect);
EI_REPLACE_METHOD(0x0067B390, CEffect::DestructEffect);   // the missile base's copy

CEffect* CEffect::Construct() {
    ConstructEffect();
    return this;
}
EI_REPLACE_METHOD(0x0067B050, CEffect::Construct);

CEffect* CEffect::DeletingDestructEffect(unsigned flags) {
    DestructEffect();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x00676910, CEffect::DeletingDestructEffect);
EI_REPLACE_METHOD(0x0067B430, CEffect::DeletingDestructEffect);

int CEffect::TickDefault() { return 1; }
EI_REPLACE_METHOD(0x00676900, CEffect::TickDefault);

// --- CEffectSound --------------------------------------------------------------------------------

std::uintptr_t CEffectSound::Vtable() { return static_cast<std::uintptr_t>(0x007433EC); }

CEffectSound* CEffectSound::Construct() {
    ConstructEffect();
    SetVtable(this, Vtable());
    sound_ = -1;
    Reset();
    return this;
}
EI_REPLACE_METHOD(0x00675DD0, CEffectSound::Construct);

void CEffectSound::Destruct() {
    SetVtable(this, Vtable());
    Reset();
    Reset();   // again through slot 18
    DestructEffect();
}
EI_REPLACE_METHOD(0x006760A0, CEffectSound::Destruct);

CEffectSound* CEffectSound::DeletingDestruct(unsigned flags) { return DeleteAfter(this, flags); }
EI_REPLACE_METHOD(0x00676080, CEffectSound::DeletingDestruct);

// --- CEffectSoundSeq -----------------------------------------------------------------------------

std::uintptr_t CEffectSoundSeq::Vtable() { return static_cast<std::uintptr_t>(0x0074344C); }

CEffectSoundSeq* CEffectSoundSeq::Construct() {
    ConstructEffect();
    new (&name_) mfc::CString;
    names_.Construct();
    SetVtable(this, Vtable());
    sound_ = -1;
    Reset();
    return this;
}
EI_REPLACE_METHOD(0x00676760, CEffectSoundSeq::Construct);

void CEffectSoundSeq::Destruct() {
    SetVtable(this, Vtable());
    Reset();
    Reset();   // again through slot 18
    names_.Destruct();
    name_.~CString();
    DestructEffect();
}
EI_REPLACE_METHOD(0x00676980, CEffectSoundSeq::Destruct);

CEffectSoundSeq* CEffectSoundSeq::DeletingDestruct(unsigned flags) { return DeleteAfter(this, flags); }
EI_REPLACE_METHOD(0x00676960, CEffectSoundSeq::DeletingDestruct);

// --- CEffectAmbientSound -------------------------------------------------------------------------

std::uintptr_t CEffectAmbientSound::Vtable() { return static_cast<std::uintptr_t>(0x0074350C); }

CEffectAmbientSound* CEffectAmbientSound::Construct() {
    ConstructEffect();
    names_.Construct();
    SetVtable(this, Vtable());
    sound_ = -1;
    Reset();
    return this;
}
EI_REPLACE_METHOD(0x00677510, CEffectAmbientSound::Construct);

void CEffectAmbientSound::Destruct() {
    SetVtable(this, Vtable());
    Reset();
    Reset();   // again through slot 18
    names_.Destruct();
    DestructEffect();
}
EI_REPLACE_METHOD(0x006776E0, CEffectAmbientSound::Destruct);

CEffectAmbientSound* CEffectAmbientSound::DeletingDestruct(unsigned flags) { return DeleteAfter(this, flags); }
EI_REPLACE_METHOD(0x006776C0, CEffectAmbientSound::DeletingDestruct);

// --- CEffectPointLight ---------------------------------------------------------------------------

std::uintptr_t CEffectPointLight::Vtable() { return static_cast<std::uintptr_t>(0x0074356C); }

CEffectPointLight* CEffectPointLight::Construct() {
    ConstructEffect();
    light_ = nullptr;
    SetVtable(this, Vtable());
    Reset();
    return this;
}
EI_REPLACE_METHOD(0x00677D30, CEffectPointLight::Construct);

void CEffectPointLight::Destruct() {
    SetVtable(this, Vtable());
    Reset();
    ReleaseIfSet(light_);
    DestructEffect();
}
EI_REPLACE_METHOD(0x00677FC0, CEffectPointLight::Destruct);

CEffectPointLight* CEffectPointLight::DeletingDestruct(unsigned flags) { return DeleteAfter(this, flags); }
EI_REPLACE_METHOD(0x00677FA0, CEffectPointLight::DeletingDestruct);

// --- CEffectParticle and CEffectWall -------------------------------------------------------------

std::uintptr_t CEffectParticle::Vtable() { return static_cast<std::uintptr_t>(0x007435D0); }

CEffectParticle* CEffectParticle::Construct() {
    ConstructEffect();
    source_ = nullptr;
    SetVtable(this, Vtable());
    Reset();
    return this;
}
EI_REPLACE_METHOD(0x006787B0, CEffectParticle::Construct);

void CEffectParticle::Destruct() {
    SetVtable(this, Vtable());
    Reset();
    ReleaseIfSet(source_);
    DestructEffect();
}
EI_REPLACE_METHOD(0x00678990, CEffectParticle::Destruct);

CEffectParticle* CEffectParticle::DeletingDestruct(unsigned flags) { return DeleteAfter(this, flags); }
EI_REPLACE_METHOD(0x00678970, CEffectParticle::DeletingDestruct);

std::uintptr_t CEffectWall::Vtable() { return static_cast<std::uintptr_t>(0x00743630); }

CEffectWall* CEffectWall::Construct() {
    ConstructEffect();
    source_ = nullptr;
    SetVtable(this, CEffectParticle::Vtable());
    CEffectParticle::Reset();
    SetVtable(this, Vtable());
    Reset();
    return this;
}
EI_REPLACE_METHOD(0x006797E0, CEffectWall::Construct);

void CEffectWall::Destruct() {
    SetVtable(this, Vtable());
    Reset();
    SetVtable(this, CEffectParticle::Vtable());
    CEffectParticle::Reset();
    ReleaseIfSet(source_);
    DestructEffect();
}
EI_REPLACE_METHOD(0x006799E0, CEffectWall::Destruct);

CEffectWall* CEffectWall::DeletingDestruct(unsigned flags) { return DeleteAfter(this, flags); }
EI_REPLACE_METHOD(0x006799C0, CEffectWall::DeletingDestruct);

// --- CEffectLightning ----------------------------------------------------------------------------

std::uintptr_t CEffectLightning::Vtable() { return static_cast<std::uintptr_t>(0x00743690); }

CEffectLightning* CEffectLightning::Construct() {
    ConstructEffect();
    bolt_ = nullptr;
    SetVtable(this, Vtable());
    width_ = 0.0f;
    flag_ = 0;
    return this;
}
EI_REPLACE_METHOD(0x00679FC0, CEffectLightning::Construct);

void CEffectLightning::Destruct() {
    SetVtable(this, Vtable());
    Reset();
    ReleaseIfSet(bolt_);
    DestructEffect();
}
EI_REPLACE_METHOD(0x0067A0B0, CEffectLightning::Destruct);

CEffectLightning* CEffectLightning::DeletingDestruct(unsigned flags) { return DeleteAfter(this, flags); }
EI_REPLACE_METHOD(0x0067A090, CEffectLightning::DeletingDestruct);

// --- The effect manager's factory ----------------------------------------------------------------

namespace {

template <typename T>
CEngineObject* NewEffect() {
    void* memory = gamecrt::operator_new(sizeof(T));
    return memory != nullptr ? static_cast<T*>(memory)->Construct() : nullptr;
}

} // namespace

CEngineObject* __fastcall EffectNewObject(CBaseManager*, void*, int type) {
    CEngineObject* object;
    switch (type) {
    case 0x48: object = NewEffect<CEffectSound>(); break;
    case 0x49: object = NewEffect<CEffectAmbientSound>(); break;
    case 0x4A: object = NewEffect<CEffectParticle>(); break;
    case 0x4B: object = NewEffect<CEffectPointLight>(); break;
    case 0x4D: object = NewEffect<CEffectLightning>(); break;
    case 0x4E: object = NewEffect<CEffectArrow>(); break;
    case 0x5A: object = NewEffect<CEffectWall>(); break;
    case 0x5B: object = NewEffect<CEffectArrowFigure>(); break;
    case 0x5E: object = NewEffect<CEffectShowVisibility>(); break;
    case 99: object = NewEffect<CEffectSoundSeq>(); break;
    case 100: object = NewEffect<CEffectMoshka>(); break;
    case 0x65: object = NewEffect<CEffectTornado>(); break;
    default: return nullptr;
    }
    if (object != nullptr) object->type_ = type;
    return object;
}
EI_REPLACE(0x0067E170, EffectNewObject);
