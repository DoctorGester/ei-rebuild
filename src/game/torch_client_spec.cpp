// The torch's client half (CTorchObjectClientSpecific, 0052D0D0-0052D670; torch_client_spec.h):
// lifetime, its effects shown / hidden, and the setters that rebuild the particles, the light and
// the sound. A-U11.
#include "terrain.h"
#include "torch_client_spec.h"

#include "camera.h"        // TheTerrain (the light manager, 007AF738)
#include "core_maths.h"
#include "effects.h"       // CParticleSource, CParticleManager
#include "map_object.h"    // CMapObject::position
#include "sound.h"         // CSoundManager
#include "spell.h"         // CLight, CLightFactory
#include "originals_effects.h"   // ParticleManagerObject

#include "ei/hook.h"
#include "ei/vcall.h"      // SetVtable
#include "lib/crt.h"
#include "game_data.h"
#include "game_singletons.h"

#include <cstdint>
#include <cstring>
#include <new>

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

namespace {

constexpr std::uint32_t kLightHidden = 0x80;   // CEngineObject::flags_ of a light
constexpr int kPointLightClass = 0x31;         // the light factory's point lights
constexpr int kWarmUpTicks = 0x3C;
constexpr float kShowFarFactor = 3.0f;         // 0073BAA4
constexpr float kSetFarFactor = 5.0f;          // 0073BF34
constexpr float kSoundNear = 1.0f;
constexpr int kTorchClassId = 0x41;
// 0078F7BC: "CTorchObjectClientSpecific".
const char* ClassNameText() { return "CTorchObjectClientSpecific"; }

CLightFactory* Lights() { return ei::GameData<CLightFactory* const>(0x007AF748); }
CParticleManager* Particles() { return ParticleManagerObject(); }

// The sources are engine objects (vtable 0073CFD0): slot 4 releases a reference.
void ReleaseSource(CParticleSource* source) { reinterpret_cast<CEngineObject*>(source)->Release(); }

// The torch's position plus `offset`, each sum stored as a float.
Vector3 AtTorch(const CMapObject* torch, const float* offset) {
    return {torch->position.x + offset[0], torch->position.y + offset[1], torch->position.z + offset[2]};
}

} // namespace

CTorchObjectClientSpec::CTorchObjectClientSpec() {
    light_ = nullptr;
    particles_ = nullptr;
}

CTorchObjectClientSpec* CTorchObjectClientSpec::Construct() { return new (this) CTorchObjectClientSpec(); }
EI_REPLACE_METHOD(0x0052D0D0, CTorchObjectClientSpec::Construct);

CTorchObjectClientSpec::~CTorchObjectClientSpec() {
    HideEffects();
    if (CLight* old = light_) {
        light_ = nullptr;
        old->Release();
    }
    if (particles_) particles_->Stop();
    if (CParticleSource* old = particles_) {
        particles_ = nullptr;
        ReleaseSource(old);
    }
    DestroySpec();
    // The members' destructors find both null; then the spec's destructor.
}

void CTorchObjectClientSpec::DestructClient() { this->CTorchObjectClientSpec::~CTorchObjectClientSpec(); }
EI_REWRITTEN(0x0052D1D0, CTorchObjectClientSpec::DestructClient);

CTorchObjectClientSpec* CTorchObjectClientSpec::DeleteClient(unsigned flags) {
    this->CTorchObjectClientSpec::~CTorchObjectClientSpec();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REWRITTEN(0x0052D1B0, CTorchObjectClientSpec::DeleteClient);

int CTorchObjectClientSpec::CreateClient() {
    CreateSpec();
    if (CLight* old = light_) {
        light_ = nullptr;
        old->Release();
    }
    if (CParticleSource* old = particles_) {
        particles_ = nullptr;
        ReleaseSource(old);
    }
    sound_ = 0;
    return 1;
}
EI_REPLACE_METHOD(0x0052D2A0, CTorchObjectClientSpec::CreateClient);

void CTorchObjectClientSpec::DestroyClient() {
    HideEffectsSlot();
    if (CLight* old = light_) {
        light_ = nullptr;
        old->Release();
    }
    if (particles_) particles_->Stop();
    if (CParticleSource* old = particles_) {
        particles_ = nullptr;
        ReleaseSource(old);
    }
    DestroySpec();
}
EI_REPLACE_METHOD(0x0052D2E0, CTorchObjectClientSpec::DestroyClient);

void CTorchObjectClientSpec::ShowEffects() {
    if (particles_) particles_->StartEmitting();
    if (light_ && (light_->flags_ & kLightHidden)) {
        light_->flags_ &= ~kLightHidden;
        camera::TheTerrain()->AddLight(light_);
    }
    if (sound_) TheSoundManager()->StopSound(sound_);
    const float far = values_[1] * kShowFarFactor;
    sound_ = TheSoundManager()->Play3D(name_, 0, &torch_->position.x, kSoundNear, far, 1, 0);
}
EI_REWRITTEN(0x0052D580, CTorchObjectClientSpec::ShowEffects);

void CTorchObjectClientSpec::HideEffects() {
    if (particles_) particles_->StopEmitting();
    if (light_ && !(light_->flags_ & kLightHidden)) {
        light_->flags_ |= kLightHidden;
        camera::TheTerrain()->RemoveLight(light_);
    }
    if (sound_) {
        TheSoundManager()->StopSound(sound_);
        sound_ = 0;
    }
}
EI_REPLACE_METHOD(0x0052D610, CTorchObjectClientSpec::HideEffects);

void CTorchObjectClientSpec::WarmUpParticles() {
    if (!particles_) return;
    for (int i = 0; i < kWarmUpTicks; ++i) particles_->Tick();
}
EI_REPLACE_METHOD(0x0052D3F0, CTorchObjectClientSpec::WarmUpParticles);

void CTorchObjectClientSpec::SetParticles(std::uint32_t type, const float* offset, float scale) {
    SetLight(type, offset, scale);
    const Vector3 at = AtTorch(torch_, offset);
    if (particles_) {
        particles_->Stop();
        CParticleSource* old = particles_;
        particles_ = nullptr;
        if (old) ReleaseSource(old);
    }
    CParticleSource* source = Particles()->CreateSource(static_cast<int>(type));
    CParticleSource* old = particles_;
    particles_ = source;
    if (source) ++source->refCount_;
    if (old) ReleaseSource(old);
    particles_->position = at;
    particles_->ScaleSize(scale);
    particles_->Attach(nullptr);
}
EI_REPLACE_METHOD(0x0052D330, CTorchObjectClientSpec::SetParticles);

void CTorchObjectClientSpec::SetLightAt(const float* offset, float radius, const float* colour) {
    SetFlame(offset, radius, colour);
    const Vector3 at = AtTorch(torch_, offset);
    if (light_) {
        camera::TheTerrain()->RemoveLight(light_);
        CLight* old = light_;
        light_ = nullptr;
        if (old) old->Release();
    }
    auto* light = static_cast<CLight*>(Lights()->CreateObject(kPointLightClass, -1));
    CLight* old = light_;
    light_ = light;
    if (light) ++light->refCount_;
    if (old) old->Release();
    const Vector3 rgb = {colour[0], colour[1], colour[2]};
    light_->SetPoint(&rgb, &at);
    light_->range_ = radius;
    // The square is stored (FST) and 1 divided by the same register value.
    const float squared = static_cast<float>(radius * radius);
    light_->rangeSquared_ = squared;
    light_->inverseRangeSquared_ = 1.0f / squared;
    camera::TheTerrain()->AddLight(light_);
}
EI_REPLACE_METHOD(0x0052D410, CTorchObjectClientSpec::SetLightAt);

void CTorchObjectClientSpec::SetSound(const char* name) {
    SetName(name);
    if (sound_) TheSoundManager()->StopSound(sound_);
    const float far = values_[1] * kSetFarFactor;
    sound_ = TheSoundManager()->Play3D(name_, 0, &torch_->position.x, kSoundNear, far, 1, 0);
}
EI_REWRITTEN(0x0052D520, CTorchObjectClientSpec::SetSound);

// Slots 6, 11 and 12 (0052D180 / 0052D190 / 0052D1A0): the update returns 1, the class name and id.
namespace {
int __fastcall TorchClientUpdate(CTorchObjectClientSpec* self) { return self->CTorchObjectClientSpec::RunTick(); }
const char* __fastcall TorchClientClassName(CTorchObjectClientSpec* self) {
    return self->CTorchObjectClientSpec::GetClassName();
}
int __fastcall TorchClientClassId(CTorchObjectClientSpec* self) { return self->CTorchObjectClientSpec::GetClassId(); }
} // namespace

int CTorchObjectClientSpec::RunTick() { return 1; }
const char* CTorchObjectClientSpec::GetClassName() { return ClassNameText(); }
int CTorchObjectClientSpec::GetClassId() { return kTorchClassId; }
EI_REPLACE(0x0052D180, TorchClientUpdate);
EI_REPLACE(0x0052D190, TorchClientClassName);
EI_REPLACE(0x0052D1A0, TorchClientClassId);
