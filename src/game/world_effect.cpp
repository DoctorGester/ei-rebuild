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
    stream->Read(&count, 4);
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
    Read(stream, &localPosition_, 12);
    Read(stream, name_, 0x100);
    Read(stream, &volume_, 4);
    Read(stream, &radius_, 4);
    Read(stream, &velocity_, 12);
    moving_ = IsMoving(velocity_);
    if (Version(stream) < 0x5C) return;
    Read(stream, &looping_, 4);
    if (Version(stream) < 0x6D) return;
    Read(stream, &noFade_, 1);
}

void CEffectSound::Save(CStream* stream) {
    Write(stream, &localPosition_, 12);
    Write(stream, name_, 0x100);
    WriteCopy(stream, &volume_);
    WriteCopy(stream, &radius_);
    Write(stream, &velocity_, 12);
    Write(stream, &looping_, 4);
    Write(stream, &noFade_, 1);
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
    Read(stream, &localPosition_, 12);
    Read(stream, &velocity_, 12);
    stream->ReadInto(&name_);
    ReadSavedStrings(&names_, stream);
    Read(stream, &volume_, 4);
    Read(stream, &radius_, 4);
    moving_ = IsMoving(velocity_);
    if (Version(stream) < 0x6D) return;
    Read(stream, &noFade_, 1);
}

void CEffectSoundSeq::Save(CStream* stream) {
    Write(stream, &localPosition_, 12);
    Write(stream, &velocity_, 12);
    stream->WriteString(name_);
    int count = static_cast<int>(names_.size());
    Write(stream, &count, 4);
    for (const std::string& name : names_) stream->WriteString(name);
    WriteCopy(stream, &volume_);
    WriteCopy(stream, &radius_);
    Write(stream, &noFade_, 1);
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
    Read(stream, &localPosition_, 12);
    ReadSavedStrings(&names_, stream);
    Read(stream, &volume_, 4);
    Read(stream, &radius_, 4);
    Read(stream, &minInterval_, 4);
    Read(stream, &maxInterval_, 4);
}

void CEffectAmbientSound::Save(CStream* stream) {
    Write(stream, &localPosition_, 12);
    int count = static_cast<int>(names_.size());
    Write(stream, &count, 4);
    for (const std::string& name : names_) stream->WriteString(name);
    WriteCopy(stream, &volume_);
    WriteCopy(stream, &radius_);
    WriteCopy(stream, &minInterval_);
    WriteCopy(stream, &maxInterval_);
}

double CEffectAmbientSound::Radius() { return radius_; }
