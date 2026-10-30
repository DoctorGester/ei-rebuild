// The effect base (Effect) and the sound effects: EffectSound, EffectSoundSeq,
// EffectAmbientSound. See world_effect.h for the class map and docs/notes/effects.md.
#include "world_effect.h"

#include "console.h"
#include "game_singletons.h"
#include "net_delta.h"
#include "net_link.h"
#include "save_versions.h"
#include "sound.h"
#include "world_effect_internal.h"

#include <cstdint>
#include <cstring>
#include <new>
#include <string>

using namespace effect;

namespace {

// What every Reset does to the base part first: position, field streams, start tick, velocity, moving
// flag and stamp.
void ClearBase(Effect* e) {
    e->localPosition = Vector3{0.0f, 0.0f, 0.0f};
    EmptyField(&e->state);
    e->velocity = Vector3{0.0f, 0.0f, 0.0f};
    e->startTick = 0;
    e->moving = 0;
    e->changeStamp = -1;
}

// The local position moved by the velocity (each sum rounded to float, as stored).
void MoveByVelocity(Effect* e) {
    float x = e->localPosition.x + e->velocity.x;
    float y = e->localPosition.y + e->velocity.y;
    float z = e->localPosition.z + e->velocity.z;
    e->localPosition.x = x;
    e->localPosition.y = y;
    e->localPosition.z = z;
}

} // namespace


void Effect::SendFull() {
    DeltaField* field = state.BeginWriteB(0);
    Pack(field);
    state.EndField();
}

void Effect::SendDelta() {
    DeltaField* field = state.BeginWriteB(1);
    Pack(field);
    state.EndField();
}

int Effect::Receive(int update, MemoryStream* reader) {
    DeltaField* field = state.BeginReadB(update, reader);
    int result = Unpack(update, field);
    state.EndField();
    return result;
}

void Effect::Reset() { ClearBase(this); }

std::vector<std::string> ReadSavedStrings(ByteStream* stream) {
    int count;
    stream->ReadValue(count);
    std::vector<std::string> array;
    for (int i = 0; i < count; ++i) {
        array.push_back(stream->ReadString());
    }
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

double Effect::Radius() { return ConstAt(worlddata::DefaultRadius()); }


void EffectSound::Reset() {
    ClearBase(this);
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
        if (sound != 0) g_clientWorld.sounds->SetSoundPosition(sound, &localPosition.x, volume, radius);
    }
    return looping != 0 || wasFresh != 0;
}

void EffectSound::Start(int update) {
    if (update == 0) {
        sound = g_clientWorld.sounds->Play3D(name, noFade != 0 ? 1000 : 0, &localPosition.x,
                                            volume, radius, looping);
        return;
    }
    g_clientWorld.sounds->SetSoundPosition(sound, &localPosition.x, volume, radius);
}

int EffectSound::Unpack(int update, DeltaField* field) {
    Vector3 position;
    position = field->ReadPosition();
    field->ReadDelta(&looping, 4);
    field->ReadText(name, 0xFF);
    volume = field->ReadFloat16();
    radius = field->ReadFloat16();
    velocity = field->ReadFloat16x3();
    field->ReadDelta(&noFade, 1);
    moving = IsMoving(velocity);
    if (!SnapshotReadToEnd()) return 0;
    localPosition = position;
    Start(update);
    return 1;
}

void EffectSound::Pack(DeltaField* field) {
    Vector3 position = localPosition;
    field->WritePosition(position);
    int currentLooping = looping;
    field->WriteDelta(&currentLooping, 4);
    field->WriteStringBoth(name);
    field->WriteFloat16(Bits(volume));
    field->WriteFloat16(Bits(radius));
    field->WriteFloat16x3(&velocity.x);
    field->WriteDelta(&noFade, 1);
}

void EffectSound::Load(ByteStream* stream) {
    stream->ReadValue(localPosition);
    Read(stream, name, 0x100);
    stream->ReadValue(volume);
    stream->ReadValue(radius);
    stream->ReadValue(velocity);
    moving = IsMoving(velocity);
    if (Version(stream) < kVersionSoundLooping) return;
    stream->ReadValue(looping);
    if (Version(stream) < kVersionSoundNoFade) return;
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

double EffectSound::Radius() { return radius; }


void EffectSoundSeq::Reset() {
    ClearBase(this);
    if (sound != -1) g_clientWorld.sounds->RemoveSequence(sound);
    sound = -1;
    volume = 0.0f;
    radius = 0.0f;
    name = game::kEmptyText;
    names.clear();
    noFade = 0;
}

bool EffectSoundSeq::RunTick() {
    if (moving != 0) {
        MoveByVelocity(this);
        if (sound != 0)
            g_clientWorld.sounds->UpdateSequence(sound, &localPosition.x, volume, radius, -1);
    }
    return true;
}

void EffectSoundSeq::Pack(DeltaField* field) {
    Vector3 position = localPosition;
    field->WritePosition(position);
    field->WriteStringBoth(name);
    int count = static_cast<int>(names.size());
    field->WriteDelta(&count, 4);
    for (const std::string& entry : names) field->WriteStringBoth(entry);
    field->WriteFloat16(Bits(volume));
    field->WriteFloat16(Bits(radius));
    field->WriteFloat16x3(&velocity.x);
    field->WriteDelta(&noFade, 1);
}

void EffectSoundSeq::Load(ByteStream* stream) {
    stream->ReadValue(localPosition);
    stream->ReadValue(velocity);
    name = stream->ReadString();
    names = ReadSavedStrings(stream);
    stream->ReadValue(volume);
    stream->ReadValue(radius);
    moving = IsMoving(velocity);
    if (Version(stream) < kVersionSoundNoFade) return;
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

double EffectSoundSeq::Radius() { return radius; }


void EffectAmbientSound::Reset() {
    ClearBase(this);
    if (sound != -1) sound = g_clientWorld.sounds->RemoveEmitter(sound);
    names.clear();
    volume = 0.0f;
    radius = 0.0f;
    minInterval = 0;
    maxInterval = 0;
}

void EffectAmbientSound::Start(int update) {
    if (update == 0) {
        sound = g_clientWorld.sounds->AddEmitter(&names, &localPosition.x, volume, radius,
                             minInterval, maxInterval);
        return;
    }
    g_clientWorld.sounds->MoveEmitter(sound, &localPosition.x, volume, radius, minInterval,
                maxInterval);
}

int EffectAmbientSound::Unpack(int update, DeltaField* field) {
    Vector3 position;
    position = field->ReadPosition();
    std::uint8_t count = 0;   // (ReadDelta writes it either way)
    field->ReadDelta(&count, 1);
    names.resize(count);
    for (std::string& name : names) name = field->ReadString();
    volume = field->ReadFloat16();
    radius = field->ReadFloat16();
    field->ReadDelta(&minInterval, 4);
    field->ReadDelta(&maxInterval, 4);
    if (!SnapshotReadToEnd()) return 0;
    localPosition = position;
    Start(update);
    return 1;
}

void EffectAmbientSound::Pack(DeltaField* field) {
    Vector3 position = localPosition;
    field->WritePosition(position);
    std::uint8_t count = static_cast<std::uint8_t>(names.size());
    field->WriteDelta(&count, 1);
    for (const std::string& name : names) field->WriteStringBoth(name);
    field->WriteFloat16(Bits(volume));
    field->WriteFloat16(Bits(radius));
    int value = minInterval;
    field->WriteDelta(&value, 4);
    value = maxInterval;
    field->WriteDelta(&value, 4);
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

double EffectAmbientSound::Radius() { return radius; }

// --- The effects' lifetime and the state field's constructor and emptying --------------------------------

namespace {

} // namespace

// --- The effect base -----------------------------------------------------------------------------

Effect::Effect() {
    // Identity rotations (w first); the positions are not initialised.
    localRotation = Quaternion{1.0f, 0.0f, 0.0f, 0.0f};
    worldRotation = Quaternion{1.0f, 0.0f, 0.0f, 0.0f};
    velocity = Vector3{0.0f, 0.0f, 0.0f};
    startTick = 0;
    moving = 0;
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
    sound = -1;
    Reset();
}

EffectSound::~EffectSound() { Reset(); }

EffectSoundSeq::EffectSoundSeq() {
    sound = -1;
    Reset();
}

EffectSoundSeq::~EffectSoundSeq() { Reset(); }

EffectAmbientSound::EffectAmbientSound() {
    sound = -1;
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

// No Reset: only the width and the flag.
EffectLightning::EffectLightning() {
    width = 0.0f;
    flag = 0;
}

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
