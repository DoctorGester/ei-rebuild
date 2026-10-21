// The effect base (CEffect) and the sound effects: CEffectSound, CEffectSoundSeq,
// CEffectAmbientSound. See world_effect.h for the class map and docs/notes/effects.md.

#include "net_delta.h"
#include "sound.h"
#include "world_effect_internal.h"

#include "lib/mfc.h"
#include "originals_effects.h"
#include "game_singletons.h"

#include <cstring>

using namespace effect;

namespace {

// The sound system (*007AF784), thiscall (006B8600 plays a sound: CSoundManager::Play3D). The
// mover takes the volume and radius as the floats' bits.
// 006B92F0: moves a playing sound.
// 006B94A0: stops a sound.
// 006BA5E0 / 006BA7C0: moves / stops a sound sequence.
// 006B9950 / 006B9A30: starts / moves an ambient sound (a list of names); 006B9AA0 stops it and
// returns the handle to keep (-1).

// What every Reset (slot 18) does to the base part first: position, field streams, start tick,
// velocity, moving flag and stamp.
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

// --- CEffect -------------------------------------------------------------------------------------

void CEffect::SendFull() {
    DeltaField* field = state_.BeginWriteB(0);
    EffectSlots(this)->Pack(field);
    state_.EndField();
}

void CEffect::SendDelta() {
    DeltaField* field = state_.BeginWriteB(1);
    EffectSlots(this)->Pack(field);
    state_.EndField();
}

int CEffect::Receive(int update, BitStream* reader) {
    DeltaField* field = state_.BeginReadB(update, reader);
    int result = EffectSlots(this)->Unpack(update, field);
    state_.EndField();
    return result;
}

void CEffect::ResetBase() { ClearBase(this); }

void __fastcall ReadSavedStrings(mfc::CStringArray* array, CStream* stream) {
    int count;
    stream->Read(&count, 4);
    for (int i = 0; i < count; ++i) {
        alignas(mfc::CString) unsigned char slot[sizeof(mfc::CString)];
        mfc::CString* text = stream->ReadString(slot);
        array->SetAtGrow(array->size, *text);
        text->~CString();
    }
}
EI_REWRITTEN(0x006773A0, ReadSavedStrings);

void CEffect::MarkChanged() {
    flags_ |= 2;
    changeStamp_ = ChangeStamp();
}

void CEffect::UpdateMoving() { moving_ = IsMoving(velocity_); }

int CEffect::CreateEffect() {
    TimedSlots(this)->Reset();
    SendFull();
    return 1;
}

void CEffect::DestroyEffect() { TimedSlots(this)->Reset(); }

void CEffect::SetVelocity(const Vector3* velocity) {
    std::memcpy(&velocity_, velocity, sizeof(Vector3));
    moving_ = IsMoving(velocity_);
    MarkChanged();
}

Vector3* CEffect::Position(void*) { return &localPosition_; }

double CEffect::DefaultRadius() { return ConstAt(worlddata::DefaultRadius()); }

// --- CEffectSound --------------------------------------------------------------------------------

void CEffectSound::Reset() {
    ClearBase(this);
    if (sound_ != -1 && looping_ != 0) TheSoundManager()->StopSound(sound_);
    sound_ = -1;
    looping_ = 1;
    std::memset(name_, 0, sizeof(name_));   // SV7b2a: the original clears name_[0] (the harness all of it)
    volume_ = 0.0f;
    radius_ = 0.0f;
    noFade_ = 0;
    fresh_ = 1;
}

int CEffectSound::Tick() {
    std::uint8_t wasFresh = fresh_;
    fresh_ = 0;
    if (moving_ != 0) {
        MoveByVelocity(this);
        // Tests the handle against 0, not -1 (a stopped sound is moved too).
        if (sound_ != 0) TheSoundManager()->SetSoundPosition(sound_, &localPosition_.x, volume_, radius_);
    }
    return looping_ == 0 && wasFresh == 0 ? 0 : 1;
}

void CEffectSound::Start(int update) {
    if (update == 0) {
        sound_ = TheSoundManager()->Play3D(name_, noFade_ != 0 ? 1000 : 0, &localPosition_.x,
                                            volume_, radius_, looping_, 0);
        return;
    }
    TheSoundManager()->SetSoundPosition(sound_, &localPosition_.x, volume_, radius_);
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
    EffectSlots(this)->Start(update);
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

void CEffectSound::LoadState(void* stream) {
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

void CEffectSound::SaveState(void* stream) {
    Write(stream, &localPosition_, 12);
    Write(stream, name_, 0x100);
    WriteCopy(stream, &volume_);
    WriteCopy(stream, &radius_);
    Write(stream, &velocity_, 12);
    Write(stream, &looping_, 4);
    Write(stream, &noFade_, 1);
}

double CEffectSound::Radius() { return radius_; }

// --- CEffectSoundSeq -----------------------------------------------------------------------------

void CEffectSoundSeq::Reset() {
    ClearBase(this);
    if (sound_ != -1) TheSoundManager()->RemoveSequence(sound_);
    sound_ = -1;
    volume_ = 0.0f;
    radius_ = 0.0f;
    name_ = EmptyCString();
    names_.SetSize(0, -1);
    noFade_ = 0;
}

int CEffectSoundSeq::Tick() {
    if (moving_ != 0) {
        MoveByVelocity(this);
        if (sound_ != 0)
            TheSoundManager()->UpdateSequence(sound_, &localPosition_.x, volume_, radius_, -1);
    }
    return 1;
}

void CEffectSoundSeq::Pack(DeltaField* field) {
    Vector3 position = localPosition_;
    field->WritePosition(&position);
    field->WriteStringBoth(name_);
    int count = names_.size;
    field->WriteDelta(&count, 4);
    for (int i = 0; i < names_.size; ++i) field->WriteStringBoth(names_[i]);
    field->WriteFloat16(Bits(volume_));
    field->WriteFloat16(Bits(radius_));
    field->WriteFloat16x3(&velocity_.x);
    field->WriteDelta(&noFade_, 1);
}

void CEffectSoundSeq::LoadState(void* stream) {
    Read(stream, &localPosition_, 12);
    Read(stream, &velocity_, 12);
    static_cast<CStream*>(stream)->ReadInto(&name_);
    ReadSavedStrings(&names_, static_cast<CStream*>(stream));
    Read(stream, &volume_, 4);
    Read(stream, &radius_, 4);
    moving_ = IsMoving(velocity_);
    if (Version(stream) < 0x6D) return;
    Read(stream, &noFade_, 1);
}

void CEffectSoundSeq::SaveState(void* stream) {
    Write(stream, &localPosition_, 12);
    Write(stream, &velocity_, 12);
    Write(stream, name_, static_cast<std::uint32_t>(std::strlen(name_) + 1));
    int count = names_.size;
    Write(stream, &count, 4);
    for (int i = 0; i < names_.size; ++i) {
        const char* name = names_[i];
        Write(stream, name, static_cast<std::uint32_t>(std::strlen(name) + 1));
    }
    WriteCopy(stream, &volume_);
    WriteCopy(stream, &radius_);
    Write(stream, &noFade_, 1);
}

double CEffectSoundSeq::Radius() { return radius_; }

// --- CEffectAmbientSound -------------------------------------------------------------------------

void CEffectAmbientSound::Reset() {
    ClearBase(this);
    if (sound_ != -1) sound_ = TheSoundManager()->RemoveEmitter(sound_);
    names_.SetSize(0, -1);
    volume_ = 0.0f;
    radius_ = 0.0f;
    minInterval_ = 0;
    maxInterval_ = 0;
}

void CEffectAmbientSound::Start(int update) {
    if (update == 0) {
        sound_ = TheSoundManager()->AddEmitter(&names_, &localPosition_.x, volume_, radius_,
                             minInterval_, maxInterval_);
        return;
    }
    TheSoundManager()->MoveEmitter(sound_, &localPosition_.x, volume_, radius_, minInterval_,
                maxInterval_);
}

int CEffectAmbientSound::Unpack(int update, DeltaField* field) {
    Vector3 position;
    field->ReadPosition(&position);
    // The count is read into the low byte of the argument slot that held `field`.
    std::uint32_t countSlot = reinterpret_cast<std::uintptr_t>(field);
    field->ReadDelta(&countSlot, 1);
    names_.SetSize(static_cast<int>(countSlot & 0xFF), -1);
    for (int i = 0; i < static_cast<int>(countSlot & 0xFF); ++i) {
        alignas(4) unsigned char raw[sizeof(mfc::CString)];
        auto* text = static_cast<mfc::CString*>(field->ReadString(raw));
        names_[i] = *text;
        text->~CString();
    }
    field->ReadFloat16(&volume_);
    field->ReadFloat16(&radius_);
    field->ReadDelta(&minInterval_, 4);
    field->ReadDelta(&maxInterval_, 4);
    if (!SnapshotReadToEnd()) return 0;
    localPosition_ = position;
    EffectSlots(this)->Start(update);
    return 1;
}

void CEffectAmbientSound::Pack(DeltaField* field) {
    Vector3 position = localPosition_;
    field->WritePosition(&position);
    std::uint8_t count = static_cast<std::uint8_t>(names_.size);
    field->WriteDelta(&count, 1);
    for (int i = 0; i < names_.size; ++i) field->WriteStringBoth(names_[i]);
    field->WriteFloat16(Bits(volume_));
    field->WriteFloat16(Bits(radius_));
    int value = minInterval_;
    field->WriteDelta(&value, 4);
    value = maxInterval_;
    field->WriteDelta(&value, 4);
}

void CEffectAmbientSound::LoadState(void* stream) {
    Read(stream, &localPosition_, 12);
    ReadSavedStrings(&names_, static_cast<CStream*>(stream));
    Read(stream, &volume_, 4);
    Read(stream, &radius_, 4);
    Read(stream, &minInterval_, 4);
    Read(stream, &maxInterval_, 4);
}

void CEffectAmbientSound::SaveState(void* stream) {
    Write(stream, &localPosition_, 12);
    int count = names_.size;
    Write(stream, &count, 4);
    for (int i = 0; i < names_.size; ++i) {
        const char* name = names_[i];
        Write(stream, name, static_cast<std::uint32_t>(std::strlen(name) + 1));
    }
    WriteCopy(stream, &volume_);
    WriteCopy(stream, &radius_);
    WriteCopy(stream, &minInterval_);
    WriteCopy(stream, &maxInterval_);
}

double CEffectAmbientSound::Radius() { return radius_; }

// --- Hooks ---------------------------------------------------------------------------------------

EI_REPLACE_METHOD(0x00675BE0, CEffect::ResetBase);
EI_REPLACE_METHOD(0x00675D10, CEffect::CreateEffect);
EI_REPLACE_DISPATCH(0x00675D40, CEffect::DestroyEffect);
EI_REPLACE_METHOD(0x00675FA0, CEffect::Position);
EI_REPLACE_METHOD(0x006768F0, CEffect::DefaultRadius);

EI_REPLACE_METHOD(0x00676200, CEffectSound::Reset);
EI_REPLACE_METHOD(0x006762F0, CEffectSound::Tick);
EI_REPLACE_METHOD(0x006763A0, CEffectSound::Start);
EI_REPLACE_METHOD(0x00676430, CEffectSound::Unpack);
EI_REPLACE_METHOD(0x00676550, CEffectSound::Pack);
EI_REPLACE_METHOD(0x006765F0, CEffectSound::LoadState);
EI_REPLACE_METHOD(0x006766D0, CEffectSound::SaveState);
EI_REPLACE_METHOD(0x00676050, CEffectSound::Radius);

EI_REPLACE_METHOD(0x00676B10, CEffectSoundSeq::Reset);
EI_REPLACE_METHOD(0x00677250, CEffectSoundSeq::Tick);
EI_REPLACE_METHOD(0x00677180, CEffectSoundSeq::Pack);
EI_REPLACE_METHOD(0x006772D0, CEffectSoundSeq::LoadState);
EI_REPLACE_METHOD(0x00677430, CEffectSoundSeq::SaveState);
EI_REPLACE_METHOD(0x00676930, CEffectSoundSeq::Radius);

EI_REPLACE_METHOD(0x00677870, CEffectAmbientSound::Reset);
EI_REPLACE_METHOD(0x00677950, CEffectAmbientSound::Start);
EI_REPLACE_METHOD(0x006779D0, CEffectAmbientSound::Unpack);
EI_REPLACE_METHOD(0x00677B20, CEffectAmbientSound::Pack);
EI_REPLACE_METHOD(0x00677BF0, CEffectAmbientSound::LoadState);
EI_REPLACE_METHOD(0x00677C60, CEffectAmbientSound::SaveState);
EI_REPLACE_METHOD(0x00677690, CEffectAmbientSound::Radius);
