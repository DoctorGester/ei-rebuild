// The effect base (CEffect) and the sound effects: EffectSound, EffectSoundSeq,
// EffectAmbientSound. See world_effect.h for the class map and docs/notes/effects.md.

#include "net_delta.h"
#include "sound.h"
#include "world_effect_internal.h"

#include <cstdint>
#include <cstring>
#include <string>
#include "game_singletons.h"
#include "save_versions.h"


using namespace effect;

namespace {

// What every Reset does to the base part first: position, field streams, start tick, velocity, moving
// flag and stamp.
void ClearBase(CEffect* e) {
    e->localPosition = Vector3{0.0f, 0.0f, 0.0f};
    EmptyField(&e->state);
    e->velocity = Vector3{0.0f, 0.0f, 0.0f};
    e->startTick = 0;
    e->moving = 0;
    e->changeStamp = -1;
}

// The local position moved by the velocity (each sum rounded to float, as stored).
void MoveByVelocity(CEffect* e) {
    float x = e->localPosition.x + e->velocity.x;
    float y = e->localPosition.y + e->velocity.y;
    float z = e->localPosition.z + e->velocity.z;
    e->localPosition.x = x;
    e->localPosition.y = y;
    e->localPosition.z = z;
}

} // namespace


void CEffect::SendFull() {
    DeltaField* field = state.BeginWriteB(0);
    Pack(field);
    state.EndField();
}

void CEffect::SendDelta() {
    DeltaField* field = state.BeginWriteB(1);
    Pack(field);
    state.EndField();
}

int CEffect::Receive(int update, MemoryStream* reader) {
    DeltaField* field = state.BeginReadB(update, reader);
    int result = Unpack(update, field);
    state.EndField();
    return result;
}

void CEffect::Reset() { ClearBase(this); }

void ReadSavedStrings(std::vector<std::string>* array, ByteStream* stream) {
    int count;
    stream->ReadValue(count);
    for (int i = 0; i < count; ++i) {
        array->push_back(stream->ReadString());
    }
}

void CEffect::MarkChanged() {
    flags |= 2;
    changeStamp = g_serverWorld.updateCount;
}

void CEffect::Create() {
    Reset();
    SendFull();
}

void CEffect::Destroy() { Reset(); }

void CEffect::SetVelocity(const Vector3& newVelocity) {
    velocity = newVelocity;
    moving = IsMoving(velocity);
    MarkChanged();
}

Vector3* CEffect::Position(const ScriptGroup*) { return &localPosition; }

double CEffect::Radius() { return ConstAt(worlddata::DefaultRadius()); }


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
    field->ReadPosition(&position);
    field->ReadDelta(&looping, 4);
    field->ReadText(name, 0xFF);
    field->ReadFloat16(&volume);
    field->ReadFloat16(&radius);
    field->ReadFloat16x3(&velocity.x);
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
    ReadSavedStrings(&names, stream);
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
    field->ReadPosition(&position);
    // The count is read into the low byte of the argument slot that held `field`.
    std::uint32_t countSlot = reinterpret_cast<std::uintptr_t>(field);
    field->ReadDelta(&countSlot, 1);
    names.resize(countSlot & 0xFF);
    for (std::string& name : names) name = field->ReadString();
    field->ReadFloat16(&volume);
    field->ReadFloat16(&radius);
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
    ReadSavedStrings(&names, stream);
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
