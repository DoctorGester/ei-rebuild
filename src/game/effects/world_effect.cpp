// The effect base (Effect) and the sound effects: EffectSound, EffectSoundSeq,
// EffectAmbientSound. See world_effect.h for the class map and docs/notes/effects.md.
#include "game/effects/world_effect.h"

#include "game/app/singletons.h"
#include "game/console/console.h"
#include "game/effects/world_effect_internal.h"
#include "game/net/net_delta.h"
#include "game/net/net_link.h"
#include "game/saves/save_versions.h"
#include "game/sound/sound.h"
#include "game/world/world.h"

#include "lib/str.h"

#include <cstdint>
#include <cstring>
#include <new>
#include <string>

using namespace effect;

namespace {

// --- The effects' movement ---

// The local position moved by the velocity.
void MoveByVelocity(Effect* e) { e->localPosition += e->velocity; }

} // namespace


void Effect::SendFull() {
    DeltaWriter field(state, DeltaWrite::Full);
    Pack(field);
    field.End();
}

void Effect::SendDelta() {
    DeltaWriter field(state, DeltaWrite::Changes);
    Pack(field);
    field.End();
}

int Effect::Receive(int update, MemoryStream* reader) {
    DeltaReader field(state, update, reader);
    int result = Unpack(update, field);
    field.End();
    return result;
}

void Effect::Reset() { ClearBase(this); }

std::vector<std::string> ReadSavedStrings(ByteStream* stream) {
    std::vector<std::string> array;
    for (int count = stream->ReadCount(); count > 0; --count) array.push_back(stream->ReadString());
    return array;
}

void Effect::MarkChanged() {
    flags |= 2;
    changeStamp = g_serverWorld.updateCount;
}

void Effect::Create() {
    Reset();
    SendFull();
}

void Effect::Destroy() { Reset(); }

void Effect::SetVelocity(const Vector3& newVelocity) {
    velocity = newVelocity;
    moving = IsMoving(velocity);
    MarkChanged();
}

Vector3* Effect::Position(const ScriptGroup*) { return &localPosition; }

float Effect::Radius() { return 100.0f; }


void EffectSound::Reset() {
    Effect::Reset();
    if (sound != -1 && looping != 0) g_clientWorld.sounds->StopSound(sound);
    sound = -1;
    looping = 1;
    std::memset(name, 0, sizeof(name));     // the original clears name[0] only (the harness all of it)
    volume = 0.0f;
    radius = 0.0f;
    noFade = 0;
    fresh = 1;
}

bool EffectSound::RunTick() {
    std::uint8_t wasFresh = fresh;
    fresh = 0;
    if (moving != 0) {
        MoveByVelocity(this);
        // Tests the handle against 0, not -1 (a stopped sound is moved too).
        if (sound != 0) g_clientWorld.sounds->SetSoundPosition(sound, localPosition, volume, radius);
    }
    return looping != 0 || wasFresh != 0;
}

void EffectSound::Start(int update) {
    if (update == 0) {
        sound = g_clientWorld.sounds->Play3D(name, noFade != 0 ? 1000 : 0, localPosition,
                                            volume, radius, looping != 0);
        return;
    }
    g_clientWorld.sounds->SetSoundPosition(sound, localPosition, volume, radius);
}

int EffectSound::Unpack(int update, DeltaReader& field) {
    Vector3 position;
    position = field.ReadPosition();
    field.ReadDelta(&looping, 4);
    field.ReadText(name, 0xFF);
    return UnpackSoundTail(*this, field, position, update);
}

void EffectSound::Pack(DeltaWriter& field) {
    Vector3 position = localPosition;
    field.WritePosition(position);
    int currentLooping = looping;
    field.WriteDelta(&currentLooping, 4);
    field.WriteStringBoth(name);
    field.WriteFloat16(volume);
    field.WriteFloat16(radius);
    field.WriteFloat16x3(velocity);
    field.WriteDelta(&noFade, 1);
}

void EffectSound::Load(ByteStream* stream) {
    stream->ReadValue(localPosition);
    Read(stream, name, 0x100);
    stream->ReadValue(volume);
    stream->ReadValue(radius);
    stream->ReadValue(velocity);
    moving = IsMoving(velocity);
    if (stream->version < kVersionSoundLooping) return;
    stream->ReadValue(looping);
    if (stream->version < kVersionSoundNoFade) return;
    stream->ReadValue(noFade);
}

void EffectSound::Save(ByteStream* stream) {
    stream->WriteValue(localPosition);
    Write(stream, name, 0x100);
    stream->WriteValue(volume);
    stream->WriteValue(radius);
    stream->WriteValue(velocity);
    stream->WriteValue(looping);
    stream->WriteValue(noFade);
}

float EffectSound::Radius() { return radius; }


void EffectSoundSeq::Reset() {
    Effect::Reset();
    if (sound != -1) g_clientWorld.sounds->RemoveSequence(sound);
    sound = -1;
    volume = 0.0f;
    radius = 0.0f;
    name.clear();
    names.clear();
    noFade = 0;
}

bool EffectSoundSeq::RunTick() {
    if (moving != 0) {
        MoveByVelocity(this);
        if (sound != 0)
            g_clientWorld.sounds->UpdateSequence(sound, localPosition, volume, radius, -1);
    }
    return true;
}

void EffectSoundSeq::Pack(DeltaWriter& field) {
    Vector3 position = localPosition;
    field.WritePosition(position);
    field.WriteStringBoth(name);
    int count = static_cast<int>(names.size());
    field.WriteDelta(&count, 4);
    for (const std::string& entry : names) field.WriteStringBoth(entry);
    field.WriteFloat16(volume);
    field.WriteFloat16(radius);
    field.WriteFloat16x3(velocity);
    field.WriteDelta(&noFade, 1);
}

void EffectSoundSeq::Load(ByteStream* stream) {
    stream->ReadValue(localPosition);
    stream->ReadValue(velocity);
    name = stream->ReadString();
    names = ReadSavedStrings(stream);
    stream->ReadValue(volume);
    stream->ReadValue(radius);
    moving = IsMoving(velocity);
    if (stream->version < kVersionSoundNoFade) return;
    stream->ReadValue(noFade);
}

void EffectSoundSeq::Save(ByteStream* stream) {
    stream->WriteValue(localPosition);
    stream->WriteValue(velocity);
    stream->WriteString(name);
    int count = static_cast<int>(names.size());
    stream->WriteValue(count);
    for (const std::string& entry : names) stream->WriteString(entry);
    stream->WriteValue(volume);
    stream->WriteValue(radius);
    stream->WriteValue(noFade);
}

float EffectSoundSeq::Radius() { return radius; }


void EffectAmbientSound::Reset() {
    Effect::Reset();
    if (sound != -1) {
        g_clientWorld.sounds->RemoveEmitter(sound);
        sound = -1;
    }
    names.clear();
    volume = 0.0f;
    radius = 0.0f;
    minInterval = 0;
    maxInterval = 0;
}

void EffectAmbientSound::Start(int update) {
    if (update == 0) {
        sound = g_clientWorld.sounds->AddEmitter(names, localPosition, volume, radius,
                             minInterval, maxInterval);
        return;
    }
    g_clientWorld.sounds->MoveEmitter(sound, localPosition, volume, radius, minInterval,
                maxInterval);
}

int EffectAmbientSound::Unpack(int update, DeltaReader& field) {
    Vector3 position;
    position = field.ReadPosition();
    std::uint8_t count = 0;   // (ReadDelta writes it either way)
    field.ReadDelta(&count, 1);
    names.resize(count);
    for (std::string& name : names) name = field.ReadString();
    volume = field.ReadFloat16();
    radius = field.ReadFloat16();
    field.ReadDelta(&minInterval, 4);
    field.ReadDelta(&maxInterval, 4);
    if (!field.SnapshotConsumed()) return 0;
    localPosition = position;
    Start(update);
    return 1;
}

void EffectAmbientSound::Pack(DeltaWriter& field) {
    Vector3 position = localPosition;
    field.WritePosition(position);
    std::uint8_t count = static_cast<std::uint8_t>(names.size());
    field.WriteDelta(&count, 1);
    for (const std::string& name : names) field.WriteStringBoth(name);
    field.WriteFloat16(volume);
    field.WriteFloat16(radius);
    int value = minInterval;
    field.WriteDelta(&value, 4);
    value = maxInterval;
    field.WriteDelta(&value, 4);
}

void EffectAmbientSound::Load(ByteStream* stream) {
    stream->ReadValue(localPosition);
    names = ReadSavedStrings(stream);
    stream->ReadValue(volume);
    stream->ReadValue(radius);
    stream->ReadValue(minInterval);
    stream->ReadValue(maxInterval);
}

void EffectAmbientSound::Save(ByteStream* stream) {
    stream->WriteValue(localPosition);
    int count = static_cast<int>(names.size());
    stream->WriteValue(count);
    for (const std::string& name : names) stream->WriteString(name);
    stream->WriteValue(volume);
    stream->WriteValue(radius);
    stream->WriteValue(minInterval);
    stream->WriteValue(maxInterval);
}

float EffectAmbientSound::Radius() { return radius; }

// --- The effects' lifetime and the state field's constructor and emptying --------------------------------

// --- The effect base -----------------------------------------------------------------------------

Effect::Effect() {
    // Identity rotations (w first); the positions are not initialised.
    localRotation = Quaternion{1.0f, 0.0f, 0.0f, 0.0f};
    worldRotation = Quaternion{1.0f, 0.0f, 0.0f, 0.0f};
    velocity = Vector3{0.0f, 0.0f, 0.0f};
}

Effect::~Effect() = default;

bool Effect::RunTick() { return true; }

// --- The class names and ids (ClassName / GetClassId; the exe's strings) ------------------------------------

const char* EffectSound::ClassName() { return "CEffectSound"; }
ClassId EffectSound::GetClassId() { return ClassId::EffectSound; }
const char* EffectSoundSeq::ClassName() { return "CEffectSoundSeq"; }
ClassId EffectSoundSeq::GetClassId() { return ClassId::EffectSoundSeq; }
const char* EffectAmbientSound::ClassName() { return "CEffectAmbientSound"; }
ClassId EffectAmbientSound::GetClassId() { return ClassId::EffectAmbientSound; }
const char* EffectPointLight::ClassName() { return "CEffectPointLight"; }
ClassId EffectPointLight::GetClassId() { return ClassId::EffectPointLight; }
const char* EffectParticle::ClassName() { return "CEffectParticle"; }
ClassId EffectParticle::GetClassId() { return ClassId::EffectParticle; }
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
    Reset();
}

EffectSound::~EffectSound() { Reset(); }

EffectSoundSeq::EffectSoundSeq() {
    Reset();
}

EffectSoundSeq::~EffectSoundSeq() { Reset(); }

EffectAmbientSound::EffectAmbientSound() {
    Reset();
}

EffectAmbientSound::~EffectAmbientSound() { Reset(); }

EffectPointLight::EffectPointLight() {
    Reset();
}

EffectPointLight::~EffectPointLight() { Reset(); }

// --- EffectParticle and EffectWall -------------------------------------------------------------

EffectParticle::EffectParticle() {
    Reset();
}

EffectParticle::~EffectParticle() { Reset(); }

// The particle's constructor resets as the particle, then the wall resets as itself; the
// destructors the other way round.
EffectWall::EffectWall() { Reset(); }

EffectWall::~EffectWall() { Reset(); }

EffectLightning::~EffectLightning() { Reset(); }

// --- The effect manager's factory ----------------------------------------------------------------

EngineObject* EffectManager::NewObject(int objectType) {
    EngineObject* object;
    switch (static_cast<ClassId>(objectType)) {
    case ClassId::EffectSound: object = new EffectSound(); break;
    case ClassId::EffectAmbientSound: object = new EffectAmbientSound(); break;
    case ClassId::EffectParticle: object = new EffectParticle(); break;
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

// --- EffectSoundSeq's Start and Unpack ---

// An update moves the sequence through the single-sound call (SetSoundPosition with the sequence id,
// like the original). A new start lists the first name (when not empty) and the others, and starts the
// sequence with the chooser for their count.
void EffectSoundSeq::Start(int update) {
    SoundManager* sounds = g_clientWorld.sounds.get();
    if (update != 0) {
        sounds->SetSoundPosition(sound, localPosition, volume, radius);
        return;
    }
    std::vector<std::string> list;
    const std::string& first = name;
    const std::vector<std::string>& others = names;
    bool withFirst = str::Compare(first, "") != 0;
    if (withFirst) list.push_back(first);
    SequenceChooser chooser = ChooserFor(static_cast<int>(others.size()), withFirst);
    for (const std::string& other : others) list.push_back(other);
    sound = sounds->AddSequence(list, noFade != 0 ? 1000 : 0,
                                 localPosition, volume, radius, false, -1, chooser);
}

int EffectSoundSeq::Unpack(int update, DeltaReader& field) {
    Vector3 position;
    position = field.ReadPosition();
    name = field.ReadString();
    int count = 0;   // ReadDelta always stores all four bytes
    field.ReadDelta(&count, 4);
    std::vector<std::string>& others = names;
    others.resize(static_cast<std::size_t>(count));
    for (std::string& other : others) other = field.ReadString();
    return effect::UnpackSoundTail(*this, field, position, update);
}
