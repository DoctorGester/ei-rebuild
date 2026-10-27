#pragma once

// The torch's client half, CTorchObjectClientSpecific (the exe's GetClassName, 0078F7BC; class id
// 0x41; 19 slots; 0x64 bytes; a real virtual class: the compiler makes its vtable, C19, game.exe's
// 0073E954 and its tail 0073E98C are retired): the client-specific manager makes it for type 'K'
//. It extends the torch's state object (CTorchObjectSpec) with what the torch shows: a
// particle source, a point light and a looping sound, all placed relative to the torch. The
// spec's setters (slots 16-18) are overridden to rebuild them; slots 13 / 14 show and hide them.
// torch_client_spec.cpp (A-U11).
// The spec's fields, as the client reads them: unknown_20 the particle type, colours_[0] its offset
// and values_[0] its size scale (slot 16); colours_[1] the light's offset, values_[1] its radius
// (also the sound's far distance divided by 5 / 3) and values_[2..5] its colour (slot 17); name_ the
// sound (slot 18).

#include "server_specific_manager.h"   // CTorchObjectSpec

#include <cstddef>
#include <cstdint>

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

class CLight;            // spell.h
class CParticleSource;   // effects.h

class CTorchObjectClientSpec : public CTorchObjectSpec {
public:
    // The spec's constructor (CreateSpec), no light or particles. sound_ stays unwritten.
    CTorchObjectClientSpec();
    // The client's Destroy inlined (HideEffects, the light dropped, the particles stopped
    // and dropped, DestroySpec), then the spec's destructor.
    ~CTorchObjectClientSpec() override;
    // The deleting destructor (slot 0, 0052D1B0) frees into the game's heap.

    int Create() override { return CreateClient(); }
    void Destroy() override { DestroyClient(); }
    int RunTick() override;                             // 6: 0052D180 (1)
    const char* GetClassName() override;                // 11: 0052D190 "CTorchObjectClientSpecific"
    int GetClassId() override;                          // 12: 0052D1A0 (0x41)
    void ShowEffectsSlot() override { ShowEffects(); }             // 13
    void HideEffectsSlot() override { HideEffects(); }             // 14
    void WarmUpParticlesSlot() override { WarmUpParticles(); }     // 15
    void SetLightSlot(std::uint32_t kind, const float* colour, float value) override { SetParticles(kind, colour, value); }   // 16
    void SetFlameSlot(const float* colour, float value, const float* values) override { SetLightAt(colour, value, values); }   // 17
    void SetNameSlot(const char* name) override { SetSound(name); }   // 18

    // CreateSpec, the light and particles dropped (released), sound_ 0. Returns 1.
    int CreateClient();
    // HideEffects (through the vtable), the light dropped, the particles stopped
    // and dropped, DestroySpec.
    void DestroyClient();
    // The particles emit, a hidden light (flags_ bit 7) is shown again (the
    // light manager adds it), the sound restarts (stopped, then played looping at the torch with far
    // distance values_[1] * 3).
    void ShowEffects();
    // The particles stop emitting, a shown light is hidden (flags_ bit 7 set,
    // removed from the light manager), the sound stops (sound_ 0).
    void HideEffects();
    // The particle source ticks 60 times (its slot 6) when there is one.
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
    // it, with far distance values_[1] * 5.
    void SetSound(const char* name);

    CLight* light_;                 // +0x58 counted
    CParticleSource* particles_;    // +0x5C counted
    int sound_;                     // +0x60 the looping sound's handle, 0 when none
};
static_assert(offsetof(CTorchObjectClientSpec, light_) == 0x58 && offsetof(CTorchObjectClientSpec, sound_) == 0x60);
static_assert(sizeof(CTorchObjectClientSpec) == 0x64);
