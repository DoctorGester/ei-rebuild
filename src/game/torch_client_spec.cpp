// The torch's client half (CTorchObjectClientSpecific; torch_client_spec.h):
// lifetime, its effects shown / hidden, and the setters that rebuild the particles, the light and
// the sound.
#include "torch_client_spec.h"

#include "core_maths.h"
#include "effects.h"   // ParticleSource, ParticleManager
#include "game_singletons.h"
#include "map_object.h"   // MapObject::position
#include "sound.h"        // SoundManager
#include "spell.h"        // Light, LightFactory
#include "terrain.h"
#include "world.h"

#include <cstdint>
#include <cstring>
#include <new>

namespace {

constexpr int kPointLightClass = 0x31;         // the light factory's point lights
constexpr int kWarmUpTicks = 0x3C;
constexpr float kShowFarFactor = 3.0f;
constexpr float kSetFarFactor = 5.0f;
constexpr float kSoundNear = 1.0f;
// "CTorchObjectClientSpecific".
const char* ClassNameText() { return "CTorchObjectClientSpecific"; }


// The sources are engine objects: Release drops a reference.
void ReleaseSource(ParticleSource* source) { source->Release(); }

// The torch's position plus `offset`, each sum stored as a float.
Vector3 AtTorch(const MapObject* torch, const float* offset) {
    return {torch->localPosition.x + offset[0], torch->localPosition.y + offset[1], torch->localPosition.z + offset[2]};
}

} // namespace

TorchObjectClientSpec::TorchObjectClientSpec() {
    particles = nullptr;
}

TorchObjectClientSpec::~TorchObjectClientSpec() {
    HideEffects();
    light.Reset();
    if (particles) particles->Stop();
    if (ParticleSource* old = particles) {
        particles = nullptr;
        ReleaseSource(old);
    }
    DestroySpec();
    // The members' destructors find both null; then the spec's destructor.
}

void TorchObjectClientSpec::Create() {
    CreateSpec();
    light.Reset();
    if (ParticleSource* old = particles) {
        particles = nullptr;
        ReleaseSource(old);
    }
    sound = 0;
}

void TorchObjectClientSpec::DestroyClient() {
    HideEffectsSlot();
    light.Reset();
    if (particles) particles->Stop();
    if (ParticleSource* old = particles) {
        particles = nullptr;
        ReleaseSource(old);
    }
    DestroySpec();
}

void TorchObjectClientSpec::ShowEffects() {
    if (particles) particles->StartEmitting();
    if (light && (light->flags & LightFlag::Hidden)) {
        light->flags &= ~LightFlag::Hidden;
        g_clientWorld.terrain->AddLight(light);
    }
    if (sound) g_clientWorld.sounds->StopSound(sound);
    const float farRadius = values[1] * kShowFarFactor;
    sound = g_clientWorld.sounds->Play3D(name, 0, &torch->localPosition.x, kSoundNear, farRadius, 1);
}

void TorchObjectClientSpec::HideEffects() {
    if (particles) particles->StopEmitting();
    if (light && !(light->flags & LightFlag::Hidden)) {
        light->flags |= LightFlag::Hidden;
        g_clientWorld.terrain->RemoveLight(light);
    }
    if (sound) {
        g_clientWorld.sounds->StopSound(sound);
        sound = 0;
    }
}

void TorchObjectClientSpec::WarmUpParticles() {
    if (!particles) return;
    for (int i = 0; i < kWarmUpTicks; ++i) particles->RunTick();
}

void TorchObjectClientSpec::SetParticles(std::uint32_t newType, const float* offset, float scale) {
    SetLight(newType, offset, scale);
    const Vector3 at = AtTorch(torch, offset);
    if (particles) {
        particles->Stop();
        ParticleSource* old = particles;
        particles = nullptr;
        if (old) ReleaseSource(old);
    }
    ParticleSource* source = g_clientWorld.particleManager->CreateSource(static_cast<int>(newType));
    ParticleSource* old = particles;
    particles = source;
    if (source) source->AddRef();
    if (old) ReleaseSource(old);
    particles->localPosition = at;
    particles->ScaleSize(scale);
    particles->Attach(nullptr);
}

void TorchObjectClientSpec::SetLightAt(const float* offset, float radius, const float* colour) {
    SetFlame(offset, radius, colour);
    const Vector3 at = AtTorch(torch, offset);
    if (light) {
        g_clientWorld.terrain->RemoveLight(light);
        light.Reset();
    }
    Light* newLight = g_clientWorld.lightFactory->CreateLight(kPointLightClass);
    light.Assign(newLight);
    const Vector3 rgb = {colour[0], colour[1], colour[2]};
    light->SetPoint(rgb, at);
    light->range = radius;
    const float squared = radius * radius;
    light->rangeSquared = squared;
    light->inverseRangeSquared = 1.0f / squared;
    g_clientWorld.terrain->AddLight(light);
}

void TorchObjectClientSpec::SetSound(const char* newName) {
    SetName(newName);
    if (sound) g_clientWorld.sounds->StopSound(sound);
    const float farRadius = values[1] * kSetFarFactor;
    sound = g_clientWorld.sounds->Play3D(name, 0, &torch->localPosition.x, kSoundNear, farRadius, 1);
}

// The update returns 1; the class name and id.

bool TorchObjectClientSpec::RunTick() { return true; }
const char* TorchObjectClientSpec::ClassName() { return ClassNameText(); }
ClassId TorchObjectClientSpec::GetClassId() { return ClassId::TorchObjectClientSpec; }
