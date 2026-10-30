// The torch map object: lifetime, update, save / load, its spec and the
// network state. See torch.h and docs/notes/units.md.
#include "core_streams.h"

#include "core_maths.h"
#include "effects.h"   // ParticleSource, ParticleManager
#include "game_singletons.h"
#include "map_object.h"   // MapObject::position
#include "net_delta.h"
#include "server_specific_manager.h"
#include "sound.h"        // SoundManager
#include "spell.h"        // Light, LightFactory
#include "terrain.h"
#include "torch.h"
#include "torch_client_spec.h"
#include "world.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <new>
#include <string>

namespace {
const char* const kTorchClassName = "CTorchObject";
constexpr int kTorchField = 4;
} // namespace

TorchObject::TorchObject() { spec = nullptr; }

TorchObject::~TorchObject() {
    if (spec) spec->Release();
}

void TorchObject::DestroyTorch() {
    TorchObjectSpec* old = spec;
    spec = nullptr;
    if (old) old->Release();
    netState.Reset();
    DestroyMapObject();
}

void TorchObject::LoadTorch(ByteStream* stream) {
    LoadMapObject(stream);
    spec->Load(stream);
}

void TorchObject::SaveTorch(ByteStream* stream) {
    SaveMapObject(stream);
    spec->Save(stream);
}

void TorchObject::Load(ByteStream* stream) { LoadTorch(stream); }
void TorchObject::Save(ByteStream* stream) { SaveTorch(stream); }

void TorchObject::AttachTorchObject(EngineObject* object) {
    if (SideTypeOf(object) != SideObjectType::Torch) {
        MapObject::AttachObject(object);
        return;
    }
    TorchObjectSpec* old = spec;
    spec = static_cast<TorchObjectSpec*>(object);
    if (spec) spec->AddRef();
    if (old) old->Release();
    if (spec) spec->torch = this;    // the spec's owner
}

void TorchObject::WriteTorchNetState() {
    MapObject::WriteNetState();
    DeltaField* field = netState.BeginField(kTorchField);
    if (!field) return;
    std::uint32_t value = spec->lightKind;
    field->WriteDelta(&value, 4);
    for (float v : {spec->values[0], spec->values[1]}) field->WriteDelta(&v, 4);
    field->WriteDelta(&spec->values[2], 0x10);
    field->WriteDelta(spec->colours[0], 0xC);
    field->WriteDelta(spec->colours[1], 0xC);
    field->WriteStringBoth(spec->name);
    field->EndField();
}

std::uint32_t TorchObject::ReadTorchNetState(int first) {
    std::uint32_t complete = MapObject::ReadNetState(first);
    DeltaField* field = netState.BeginField(kTorchField);
    if (!field) return complete;
    spec->ReadNetState(field);
    std::uint32_t readTo = delta::g_readSnapshot->Position();
    std::uint32_t size = delta::g_readSnapshot->Size();
    complete &= readTo == size ? 1u : 0u;
    field->EndField();
    return complete;
}

const char* TorchObject::ClassName() { return kTorchClassName; }
ClassId TorchObject::GetClassId() { return ClassId::TorchObject; }

// --- Torch spec --------------------------------------------------------------------------------------

// The torch's state object, TorchObjectSpec (server_specific_manager.h): its Create / Destroy,
// Load / Save and setter slots. The network reader stays
// the game's own code (it applies the values through its virtual setters, which a client-side spec may override).

namespace {
constexpr float kDefaultValue = 1.0f;
constexpr float kDefaultValue2C = 10.0f;
// The default colour both colours start from (a static initialiser: black).
constexpr float kDefaultColour[3] = /*007AF2D0*/ {};
} // namespace

void TorchObjectSpec::CreateSpec() {
    torch = nullptr;
    lightKind = 0;
    name.clear();
    values[1] = kDefaultValue2C;
    values[0] = kDefaultValue;
    for (int i = 2; i < 6; ++i) values[i] = kDefaultValue;
    std::memcpy(colours[0], kDefaultColour, sizeof(colours[0]));
    std::memcpy(colours[1], kDefaultColour, sizeof(colours[1]));
}

void TorchObjectSpec::DestroySpec() {
    torch = nullptr;
    name.clear();
}

void TorchObjectSpec::LoadSpec(ByteStream* stream) {
    stream->ReadValue(lightKind);
    name = stream->ReadString();
    stream->ReadValue(values[0]);
    stream->ReadValue(values[1]);
    stream->Read(&values[2], 0x10);
    stream->Read(colours[0], 0xC);
    stream->Read(colours[1], 0xC);
}

void TorchObjectSpec::SaveSpec(ByteStream* stream) {
    stream->WriteValue(lightKind);
    stream->WriteString(name);
    std::uint32_t value;
    value = std::bit_cast<std::uint32_t>(values[0]);
    stream->WriteValue(value);
    value = std::bit_cast<std::uint32_t>(values[1]);
    stream->WriteValue(value);
    stream->Write(&values[2], 0x10);
    stream->Write(colours[0], 0xC);
    stream->Write(colours[1], 0xC);
}

void TorchObjectSpec::Load(ByteStream* stream) { LoadSpec(stream); }
void TorchObjectSpec::Save(ByteStream* stream) { SaveSpec(stream); }

void TorchObjectSpec::SetLight(std::uint32_t kind, const float* colour, float value) {
    lightKind = kind;
    std::memcpy(colours[0], colour, sizeof(colours[0]));
    values[0] = value;
}

void TorchObjectSpec::SetFlame(const float* colour, float value, const float* newValues) {
    std::memcpy(colours[1], colour, sizeof(colours[1]));
    values[1] = value;
    std::memcpy(&values[2], newValues, 4 * sizeof(float));
}

void TorchObjectSpec::SetName(const char* newName) { name = newName; }

void TorchObjectSpec::ReadNetState(DeltaField* field) {
    std::uint32_t kind;
    field->ReadDelta(&kind, 4);
    lightKind = kind;
    field->ReadDelta(&values[0], 4);
    field->ReadDelta(&values[1], 4);
    field->ReadDelta(&values[2], 0x10);
    field->ReadDelta(colours[0], 0xC);
    field->ReadDelta(colours[1], 0xC);
    name = field->ReadString();
    SetLightSlot(lightKind, colours[0], values[0]);
    SetFlameSlot(colours[1], values[1], &values[2]);
    SetNameSlot(name.c_str());
    ShowEffectsSlot();
    WarmUpParticlesSlot();
}

// --- Torch client spec -------------------------------------------------------------------------------

// The torch's client half (CTorchObjectClientSpecific; torch_client_spec.h):
// lifetime, its effects shown / hidden, and the setters that rebuild the particles, the light and
// the sound.

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
