// The torch's client half (CTorchObjectClientSpecific; torch_client_spec.h):
// lifetime, its effects shown / hidden, and the setters that rebuild the particles, the light and
// the sound.
#include "terrain.h"
#include "torch_client_spec.h"

#include "core_maths.h"
#include "effects.h"       // CParticleSource, CParticleManager
#include "map_object.h"    // CMapObject::position
#include "sound.h"         // CSoundManager
#include "spell.h"         // CLight, CLightFactory

#include "game_singletons.h"

#include <cstdint>
#include <cstring>
#include <new>

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

namespace {

constexpr std::uint32_t kLightHidden = 0x80;   // CEngineObject::flags_ of a light
constexpr int kPointLightClass = 0x31;         // the light factory's point lights
constexpr int kWarmUpTicks = 0x3C;
constexpr float kShowFarFactor = 3.0f;
constexpr float kSetFarFactor = 5.0f;
constexpr float kSoundNear = 1.0f;
constexpr int kTorchClassId = 0x41;
// "CTorchObjectClientSpecific".
const char* ClassNameText() { return "CTorchObjectClientSpecific"; }


// The sources are engine objects: slot 4 releases a reference.
void ReleaseSource(CParticleSource* source) { source->Release(); }

// The torch's position plus `offset`, each sum stored as a float.
Vector3 AtTorch(const CMapObject* torch, const float* offset) {
    return {torch->localPosition_.x + offset[0], torch->localPosition_.y + offset[1], torch->localPosition_.z + offset[2]};
}

} // namespace

CTorchObjectClientSpec::CTorchObjectClientSpec() {
    light_ = nullptr;
    particles_ = nullptr;
}

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

void CTorchObjectClientSpec::ShowEffects() {
    if (particles_) particles_->StartEmitting();
    if (light_ && (light_->flags_ & kLightHidden)) {
        light_->flags_ &= ~kLightHidden;
        TheTerrain()->AddLight(light_);
    }
    if (sound_) TheSoundManager()->StopSound(sound_);
    const float farRadius = values_[1] * kShowFarFactor;
    sound_ = TheSoundManager()->Play3D(name_.c_str(), 0, &torch_->localPosition_.x, kSoundNear, farRadius, 1, 0);
}

void CTorchObjectClientSpec::HideEffects() {
    if (particles_) particles_->StopEmitting();
    if (light_ && !(light_->flags_ & kLightHidden)) {
        light_->flags_ |= kLightHidden;
        TheTerrain()->RemoveLight(light_);
    }
    if (sound_) {
        TheSoundManager()->StopSound(sound_);
        sound_ = 0;
    }
}

void CTorchObjectClientSpec::WarmUpParticles() {
    if (!particles_) return;
    for (int i = 0; i < kWarmUpTicks; ++i) particles_->RunTick();
}

void CTorchObjectClientSpec::SetParticles(std::uint32_t type, const float* offset, float scale) {
    SetLight(type, offset, scale);
    const Vector3 at = AtTorch(torch_, offset);
    if (particles_) {
        particles_->Stop();
        CParticleSource* old = particles_;
        particles_ = nullptr;
        if (old) ReleaseSource(old);
    }
    CParticleSource* source = TheParticleManager()->CreateSource(static_cast<int>(type));
    CParticleSource* old = particles_;
    particles_ = source;
    if (source) ++source->refCount_;
    if (old) ReleaseSource(old);
    particles_->localPosition_ = at;
    particles_->ScaleSize(scale);
    particles_->Attach(nullptr);
}

void CTorchObjectClientSpec::SetLightAt(const float* offset, float radius, const float* colour) {
    SetFlame(offset, radius, colour);
    const Vector3 at = AtTorch(torch_, offset);
    if (light_) {
        TheTerrain()->RemoveLight(light_);
        CLight* old = light_;
        light_ = nullptr;
        if (old) old->Release();
    }
    CLight* light = TheLightFactory()->CreateLight(kPointLightClass);
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
    TheTerrain()->AddLight(light_);
}

void CTorchObjectClientSpec::SetSound(const char* name) {
    SetName(name);
    if (sound_) TheSoundManager()->StopSound(sound_);
    const float farRadius = values_[1] * kSetFarFactor;
    sound_ = TheSoundManager()->Play3D(name_.c_str(), 0, &torch_->localPosition_.x, kSoundNear, farRadius, 1, 0);
}

// Slots 6, 11 and 12: the update returns 1, the class name and id.

int CTorchObjectClientSpec::RunTick() { return 1; }
const char* CTorchObjectClientSpec::GetClassName() { return ClassNameText(); }
int CTorchObjectClientSpec::GetClassId() { return kTorchClassId; }
