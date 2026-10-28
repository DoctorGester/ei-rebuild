// The effect base (CEffect) and the sound effects: CEffectSound, CEffectSoundSeq,
// CEffectAmbientSound. See world_effect.h for the class map and docs/notes/effects.md.

#include "net_delta.h"
#include "sound.h"
#include "world_effect_internal.h"

#include <cstdint>
#include <cstring>
#include <string>
#include "game_singletons.h"


using namespace effect;

namespace {

// What every Reset does to the base part first: position, field streams, start tick, velocity, moving
// flag and stamp.
void ClearBase(CEffect* e) {
    e->localPosition_ = Vector3{0.0f, 0.0f, 0.0f};
    EmptyField(&e->state_);
    e->velocity_ = Vector3{0.0f, 0.0f, 0.0f};
    e->startTick_ = 0;
    e->moving_ = 0;
    e->changeStamp_ = -1;
}

// The local position moved by the velocity (each sum rounded to float, as stored).
void MoveByVelocity(CEffect* e) {
    float x = e->localPosition_.x + e->velocity_.x;
    float y = e->localPosition_.y + e->velocity_.y;
    float z = e->localPosition_.z + e->velocity_.z;
    e->localPosition_.x = x;
    e->localPosition_.y = y;
    e->localPosition_.z = z;
}

} // namespace


void CEffect::SendFull() {
    DeltaField* field = state_.BeginWriteB(0);
    Pack(field);
    state_.EndField();
}

void CEffect::SendDelta() {
    DeltaField* field = state_.BeginWriteB(1);
    Pack(field);
    state_.EndField();
}

int CEffect::Receive(int update, CMemoryStream* reader) {
    DeltaField* field = state_.BeginReadB(update, reader);
    int result = Unpack(update, field);
    state_.EndField();
    return result;
}

void CEffect::Reset() { ClearBase(this); }

void ReadSavedStrings(std::vector<std::string>* array, CStream* stream) {
    int count;
    stream->ReadValue(count);
    for (int i = 0; i < count; ++i) {
        std::string text;
        array->push_back(stream->ReadString(&text)->c_str());
    }
}

void CEffect::MarkChanged() {
    flags_ |= 2;
    changeStamp_ = g_serverWorld->updateCount_;
}

int CEffect::Create() {
    Reset();
    SendFull();
    return 1;
}

void CEffect::Destroy() { Reset(); }

void CEffect::SetVelocity(const Vector3* velocity) {
    velocity_ = *velocity;
    moving_ = IsMoving(velocity_);
    MarkChanged();
}

Vector3* CEffect::Position(const ScriptGroup*) { return &localPosition_; }

double CEffect::Radius() { return ConstAt(worlddata::DefaultRadius()); }


void CEffectSound::Reset() {
    ClearBase(this);
    if (sound_ != -1 && looping_ != 0) g_clientWorld->sounds_->StopSound(sound_);
    sound_ = -1;
    looping_ = 1;
    std::memset(name_, 0, sizeof(name_));   // the original clears name_[0] only (the harness all of it)
    volume_ = 0.0f;
    radius_ = 0.0f;
    noFade_ = 0;
    fresh_ = 1;
}

int CEffectSound::RunTick() {
    std::uint8_t wasFresh = fresh_;
    fresh_ = 0;
    if (moving_ != 0) {
        MoveByVelocity(this);
        // Tests the handle against 0, not -1 (a stopped sound is moved too).
        if (sound_ != 0) g_clientWorld->sounds_->SetSoundPosition(sound_, &localPosition_.x, volume_, radius_);
    }
    return looping_ == 0 && wasFresh == 0 ? 0 : 1;
}

void CEffectSound::Start(int update) {
    if (update == 0) {
        sound_ = g_clientWorld->sounds_->Play3D(name_, noFade_ != 0 ? 1000 : 0, &localPosition_.x,
                                            volume_, radius_, looping_, 0);
        return;
    }
    g_clientWorld->sounds_->SetSoundPosition(sound_, &localPosition_.x, volume_, radius_);
}

int CEffectSound::Unpack(int update, DeltaField* field) {
    Vector3 position;
    field->ReadPosition(&position);
    field->ReadDelta(&looping_, 4);
    field->ReadText(name_, 0xFF);
    field->ReadFloat16(&volume_);
    field->ReadFloat16(&radius_);
    field->ReadFloat16x3(&velocity_.x);
    field->ReadDelta(&noFade_, 1);
    moving_ = IsMoving(velocity_);
    if (!SnapshotReadToEnd()) return 0;
    localPosition_ = position;
    Start(update);
    return 1;
}

void CEffectSound::Pack(DeltaField* field) {
    Vector3 position = localPosition_;
    field->WritePosition(&position);
    int looping = looping_;
    field->WriteDelta(&looping, 4);
    field->WriteStringBoth(name_);
    field->WriteFloat16(Bits(volume_));
    field->WriteFloat16(Bits(radius_));
    field->WriteFloat16x3(&velocity_.x);
    field->WriteDelta(&noFade_, 1);
}

void CEffectSound::Load(CStream* stream) {
    stream->ReadValue(localPosition_);
    Read(stream, name_, 0x100);
    stream->ReadValue(volume_);
    stream->ReadValue(radius_);
    stream->ReadValue(velocity_);
    moving_ = IsMoving(velocity_);
    if (Version(stream) < 0x5C) return;
    stream->ReadValue(looping_);
    if (Version(stream) < 0x6D) return;
    stream->ReadValue(noFade_);
}

void CEffectSound::Save(CStream* stream) {
    stream->WriteValue(localPosition_);
    Write(stream, name_, 0x100);
    stream->WriteValue(volume_);
    stream->WriteValue(radius_);
    stream->WriteValue(velocity_);
    stream->WriteValue(looping_);
    stream->WriteValue(noFade_);
}

double CEffectSound::Radius() { return radius_; }


void CEffectSoundSeq::Reset() {
    ClearBase(this);
    if (sound_ != -1) g_clientWorld->sounds_->RemoveSequence(sound_);
    sound_ = -1;
    volume_ = 0.0f;
    radius_ = 0.0f;
    name_ = game::kEmptyText;
    names_.clear();
    noFade_ = 0;
}

int CEffectSoundSeq::RunTick() {
    if (moving_ != 0) {
        MoveByVelocity(this);
        if (sound_ != 0)
            g_clientWorld->sounds_->UpdateSequence(sound_, &localPosition_.x, volume_, radius_, -1);
    }
    return 1;
}

void CEffectSoundSeq::Pack(DeltaField* field) {
    Vector3 position = localPosition_;
    field->WritePosition(&position);
    field->WriteStringBoth(name_);
    int count = static_cast<int>(names_.size());
    field->WriteDelta(&count, 4);
    for (const std::string& name : names_) field->WriteStringBoth(name);
    field->WriteFloat16(Bits(volume_));
    field->WriteFloat16(Bits(radius_));
    field->WriteFloat16x3(&velocity_.x);
    field->WriteDelta(&noFade_, 1);
}

void CEffectSoundSeq::Load(CStream* stream) {
    stream->ReadValue(localPosition_);
    stream->ReadValue(velocity_);
    stream->ReadInto(&name_);
    ReadSavedStrings(&names_, stream);
    stream->ReadValue(volume_);
    stream->ReadValue(radius_);
    moving_ = IsMoving(velocity_);
    if (Version(stream) < 0x6D) return;
    stream->ReadValue(noFade_);
}

void CEffectSoundSeq::Save(CStream* stream) {
    stream->WriteValue(localPosition_);
    stream->WriteValue(velocity_);
    stream->WriteString(name_);
    int count = static_cast<int>(names_.size());
    stream->WriteValue(count);
    for (const std::string& name : names_) stream->WriteString(name);
    stream->WriteValue(volume_);
    stream->WriteValue(radius_);
    stream->WriteValue(noFade_);
}

double CEffectSoundSeq::Radius() { return radius_; }


void CEffectAmbientSound::Reset() {
    ClearBase(this);
    if (sound_ != -1) sound_ = g_clientWorld->sounds_->RemoveEmitter(sound_);
    names_.clear();
    volume_ = 0.0f;
    radius_ = 0.0f;
    minInterval_ = 0;
    maxInterval_ = 0;
}

void CEffectAmbientSound::Start(int update) {
    if (update == 0) {
        sound_ = g_clientWorld->sounds_->AddEmitter(&names_, &localPosition_.x, volume_, radius_,
                             minInterval_, maxInterval_);
        return;
    }
    g_clientWorld->sounds_->MoveEmitter(sound_, &localPosition_.x, volume_, radius_, minInterval_,
                maxInterval_);
}

int CEffectAmbientSound::Unpack(int update, DeltaField* field) {
    Vector3 position;
    field->ReadPosition(&position);
    // The count is read into the low byte of the argument slot that held `field`.
    std::uint32_t countSlot = reinterpret_cast<std::uintptr_t>(field);
    field->ReadDelta(&countSlot, 1);
    names_.resize(countSlot & 0xFF);
    for (std::string& name : names_) {
        std::string text;
        name = field->ReadString(&text)->c_str();
    }
    field->ReadFloat16(&volume_);
    field->ReadFloat16(&radius_);
    field->ReadDelta(&minInterval_, 4);
    field->ReadDelta(&maxInterval_, 4);
    if (!SnapshotReadToEnd()) return 0;
    localPosition_ = position;
    Start(update);
    return 1;
}

void CEffectAmbientSound::Pack(DeltaField* field) {
    Vector3 position = localPosition_;
    field->WritePosition(&position);
    std::uint8_t count = static_cast<std::uint8_t>(names_.size());
    field->WriteDelta(&count, 1);
    for (const std::string& name : names_) field->WriteStringBoth(name);
    field->WriteFloat16(Bits(volume_));
    field->WriteFloat16(Bits(radius_));
    int value = minInterval_;
    field->WriteDelta(&value, 4);
    value = maxInterval_;
    field->WriteDelta(&value, 4);
}

void CEffectAmbientSound::Load(CStream* stream) {
    stream->ReadValue(localPosition_);
    ReadSavedStrings(&names_, stream);
    stream->ReadValue(volume_);
    stream->ReadValue(radius_);
    stream->ReadValue(minInterval_);
    stream->ReadValue(maxInterval_);
}

void CEffectAmbientSound::Save(CStream* stream) {
    stream->WriteValue(localPosition_);
    int count = static_cast<int>(names_.size());
    stream->WriteValue(count);
    for (const std::string& name : names_) stream->WriteString(name);
    stream->WriteValue(volume_);
    stream->WriteValue(radius_);
    stream->WriteValue(minInterval_);
    stream->WriteValue(maxInterval_);
}

double CEffectAmbientSound::Radius() { return radius_; }
