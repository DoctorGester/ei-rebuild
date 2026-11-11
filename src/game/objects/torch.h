#pragma once

// Torches (ClassName "TorchObject", class id 0x57): a map object holding its light / flame description, a
// TorchObjectSpec (type 0x4B; 0x58 bytes, class id 0x42), the state object the server-specific manager makes for
// SideObjectType::Torch. objects/torch.cpp; TorchObject's Create is in map_object.cpp. Real virtual classes: the
// compiler makes their vtables.

#include "engine/core/engine_object.h"
#include "game/objects/map_object.h"
#include "game/sound/sound.h"

#include <cstddef>
#include <cstdint>
#include <string>

class ByteStream;
class DeltaReader;
class Light;
class ParticleSource;

class TorchObjectSpec : public EngineObject {
public:
    TorchObjectSpec();
    ~TorchObjectSpec() override;

    void Create() override { CreateSpec(); }
    void Destroy() override { DestroySpec(); }
    bool RunTick() override;                          // true
    void Load(ByteStream* stream) override;                // LoadSpec
    void Save(ByteStream* stream) override;                // SaveSpec
    // "TorchObjectSpec". 12: 0x42.
    const char* ClassName() override;
    ClassId GetClassId() override;
    // 13-15: empty here; the client half (objects/torch.h) shows its
    // effects, hides them and warms its particles up.
    virtual void ShowEffectsSlot() {}
    virtual void HideEffectsSlot() {}
    virtual void WarmUpParticlesSlot() {}
    // 16-18: the setters below; the client half rebuilds its effects there. Callers reach them
    // through the vtable (the network read, the world server's CreateObject).
    virtual void SetLightSlot(std::uint32_t kind, const float* colour, float value) { SetLight(kind, colour, value); }
    virtual void SetFlameSlot(const float* colour, float value, const float* newValues) { SetFlame(colour, value, newValues); }
    virtual void SetNameSlot(std::string_view newName) { SetName(newName); }

    // objects/torch.cpp. Create / Destroy (other names so they are no
    // overrides): Create clears +0x1C / +0x20 and the name, values 1, 10, 1, 1, 1, 1, both colours
    // the default colour (black), returns 1; Destroy clears +0x1C and the name. Load / Save:
    // +0x20, the name (written with its terminator), +0x28, +0x2C, the
    // four values +0x30 and both colours. The setters: set +0x20, the first
    // colour and +0x28; the second colour, +0x2C and the four values; the name.
    void CreateSpec();
    void DestroySpec();
    void LoadSpec(ByteStream* stream);
    void SaveSpec(ByteStream* stream);
    void SetLight(std::uint32_t kind, const float* colour, float value);
    void SetFlame(const float* colour, float value, const float* values);
    void SetName(std::string_view name);
    // objects/torch.cpp: the torch's network read. Reads the kind,
    // values[0], values[1], values[2..5], both vectors and the name from `field`, then applies them
    // through the virtual setters (a client half rebuilds its effects there), shows the
    // effects and warms the particles up.
    void ReadNetState(DeltaReader& field);

    MapObject* torch = nullptr;  // the torch it belongs to (set by the torch's attach); null when constructed
    std::uint32_t lightKind = 0;  // the light's kind (SetLight; saved and sent)
    std::string name;
    float values[6] = {};       // Create: 1, 10, 1, 1, 1, 1
    float colours[2][3] = {};   // Create: both black
};

class TorchObject : public MapObject {
public:
    TorchObject();
    // The spec released, then the map object's destructor.
    ~TorchObject() override;

    // The map object's Create, the spec released and cleared, 5 network fields (SetSize inlined), then
    // DeltaState::Reset (map_object.cpp).
    void Create() override;
    // The spec released and cleared, the network state reset, then the map object's Destroy.
    void Destroy() override;
    bool RunTick() override { return true; }   // no map object update
    // The map object part, then the spec's (which must exist).
    void Load(ByteStream* stream) override;
    void Save(ByteStream* stream) override;
    const char* ClassName() override;                         // "TorchObject"
    ClassId GetClassId() override;
    // A SideObjectType::Torch object (its type read before any null test) becomes the spec; anything else goes to
    // the map object's AttachObject.
    void AttachObject(EngineObject* object) override;
    bool IsSynchronised() override { return true; }
    // The map object's network state, then field 4: the spec's light kind, two values, four more, the two colours
    // and its name (both channels).
    void WriteNetState() override;
    // The map object's reader, then field 4 read into the spec (the spec's own reader, which applies it through
    // the spec's slots); the result and-ed with "the snapshot was consumed".
    bool ReadNetState(bool update) override;

    // `spec` (or null) replaces the spec: a reference taken, the old one released; the new one points back here.
    void SetSpec(TorchObjectSpec* spec);

    Ref<TorchObjectSpec> spec;
};

// The torch's client half, CTorchObjectClientSpecific (the exe's ClassName; class id
// 0x41; 19 slots; 0x64 bytes; a real virtual class: the compiler makes its vtable): the
// client-specific manager makes it for type 'K'.
// It extends the torch's state object (TorchObjectSpec) with what the torch shows: a
// particle source, a point light and a looping sound, all placed relative to the torch. The
// spec's setters are overridden to rebuild them; its show and hide virtuals act on them.
// objects/torch.cpp.
// The spec's fields, as the client reads them: unknown_20 the particle type, colours[0] its offset
// and values[0] its size scale; colours[1] the light's offset, values[1] its radius
// (also the sound's far distance divided by 5 / 3) and values[2..5] its colour; name the
// sound.

class TorchObjectClientSpec : public TorchObjectSpec {
public:
    // Defined in objects/torch.cpp: the members' destructors need Light and ParticleSource complete.
    TorchObjectClientSpec();
    ~TorchObjectClientSpec() override;

    // CreateSpec, the light and particles dropped (released), no sound.
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
    void SetNameSlot(std::string_view newName) override { SetSound(newName); }

    // HideEffects (through the vtable), the light dropped, the particles stopped
    // and dropped, DestroySpec.
    void DestroyClient();
    // The particles emit, a hidden light (flags bit 7) is shown again (the
    // light manager adds it), the sound restarts (stopped, then played looping at the torch with far
    // distance values[1] * 3).
    void ShowEffects();
    // The particles stop emitting, a shown light is hidden (flags bit 7 set,
    // removed from the light manager), the sound stops.
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
    void SetSound(std::string_view name);

    Ref<Light> light;
    ParticleSource* particles = nullptr;  // counted
    int sound = kNoSound;           // the looping sound's handle, kNoSound when none
};
