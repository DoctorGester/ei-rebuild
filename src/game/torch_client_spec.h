#pragma once

// The torch's client half, CTorchObjectClientSpecific (the exe's ClassName; class id
// 0x41; 19 slots; 0x64 bytes; a real virtual class: the compiler makes its vtable): the
// client-specific manager makes it for type 'K'.
// It extends the torch's state object (CTorchObjectSpec) with what the torch shows: a
// particle source, a point light and a looping sound, all placed relative to the torch. The
// spec's setters are overridden to rebuild them; its show and hide virtuals act on them.
// torch_client_spec.cpp.
// The spec's fields, as the client reads them: unknown_20 the particle type, colours[0] its offset
// and values[0] its size scale; colours[1] the light's offset, values[1] its radius
// (also the sound's far distance divided by 5 / 3) and values[2..5] its colour; name the
// sound.

#include "server_specific_manager.h"   // CTorchObjectSpec

#include <cstddef>
#include <cstdint>

class CLight;
class CParticleSource;

class CTorchObjectClientSpec : public CTorchObjectSpec {
public:
    CTorchObjectClientSpec();
    ~CTorchObjectClientSpec() override;

    // CreateSpec, the light and particles dropped (released), sound 0.
    void Create() override;
    void Destroy() override { DestroyClient(); }
    bool RunTick() override;                             // true
    const char* ClassName() override;                   // "CTorchObjectClientSpecific"
    ClassId GetClassId() override;
    void ShowEffectsSlot() override { ShowEffects(); }
    void HideEffectsSlot() override { HideEffects(); }
    void WarmUpParticlesSlot() override { WarmUpParticles(); }
    void SetLightSlot(std::uint32_t kind, const float* colour, float value) override { SetParticles(kind, colour, value); }
    void SetFlameSlot(const float* colour, float value, const float* newValues) override { SetLightAt(colour, value, newValues); }
    void SetNameSlot(const char* newName) override { SetSound(newName); }

    // HideEffects (through the vtable), the light dropped, the particles stopped
    // and dropped, DestroySpec.
    void DestroyClient();
    // The particles emit, a hidden light (flags bit 7) is shown again (the
    // light manager adds it), the sound restarts (stopped, then played looping at the torch with far
    // distance values[1] * 3).
    void ShowEffects();
    // The particles stop emitting, a shown light is hidden (flags bit 7 set,
    // removed from the light manager), the sound stops (sound 0).
    void HideEffects();
    // The particle source ticks 60 times (its tick) when there is one.
    void WarmUpParticles();
    // The spec's SetLight, then a new particle source of `type` (the
    // old one stopped and dropped) at the torch plus `offset`, its size scaled by `scale`, attached
    // to nothing.
    void SetParticles(std::uint32_t type, const float* offset, float scale);
    // The spec's SetFlame, then a new point light (the light factory's
    // class 0x31; the old one removed from the light manager and dropped) of colour `colour` at the
    // torch plus `offset`, range `radius` (its square and inverse square kept), added to the light
    // manager.
    void SetLightAt(const float* offset, float radius, const float* colour);
    // The spec's SetName, then the sound restarts as ShowEffects starts
    // it, with far distance values[1] * 5.
    void SetSound(const char* name);

    Ref<CLight> light;
    CParticleSource* particles;     // counted
    int sound;                      // the looping sound's handle, 0 when none
};
