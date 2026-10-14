// The effect base (CEffect) and the sound effects: CEffectSound, CEffectSoundSeq,
// CEffectAmbientSound. See world_effect.h for the class map and docs/notes/effects.md.

#include "world_effect_internal.h"

#include "lib/mfc.h"
#include "originals_effects.h"

#include <cstring>

using namespace effect;

namespace {

// The sound system (*007AF784), thiscall. Volumes and radii are passed as the floats' bits.
// 006B8600: plays `name` at `position`; returns the handle.
__declspec(naked) int __fastcall PlaySound(void*, void*, const char* /*name*/, int /*flags*/, const Vector3* /*position*/, std::uint32_t /*volume*/, std::uint32_t /*radius*/, int /*looping*/, int)
    EI_ORIGINAL(0x006B8600)
// 006B92F0: moves a playing sound.
__declspec(naked) void __fastcall MoveSound(void*, void*, int /*sound*/, const Vector3* /*position*/, std::uint32_t /*volume*/, std::uint32_t /*radius*/) EI_ORIGINAL(0x006B92F0)
// 006B94A0: stops a sound.
// 006BA5E0 / 006BA7C0: moves / stops a sound sequence.
__declspec(naked) void __fastcall MoveSequence(void*, void*, int /*sound*/, const Vector3* /*position*/, std::uint32_t /*volume*/, std::uint32_t /*radius*/, int) EI_ORIGINAL(0x006BA5E0)
__declspec(naked) void __fastcall StopSequence(void*, void*, int /*sound*/) EI_ORIGINAL(0x006BA7C0)
// 006B9950 / 006B9A30: starts / moves an ambient sound (a list of names); 006B9AA0 stops it and
// returns the handle to keep (-1).
__declspec(naked) int __fastcall PlayAmbient(void*, void*, mfc::CStringArray* /*names*/, const Vector3* /*position*/, std::uint32_t /*volume*/, std::uint32_t /*radius*/, int, int) EI_ORIGINAL(0x006B9950)
__declspec(naked) void __fastcall MoveAmbient(void*, void*, int /*sound*/, const Vector3* /*position*/, std::uint32_t /*volume*/, std::uint32_t /*radius*/, int, int) EI_ORIGINAL(0x006B9A30)
__declspec(naked) int __fastcall StopAmbient(void*, void*, int /*sound*/) EI_ORIGINAL(0x006B9AA0)

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
    DeltaField* field = BeginWrite(&state_, nullptr, 0);
    EffectSlots(this)->Pack(field);
    EndField(&state_);
}

void CEffect::SendDelta() {
    DeltaField* field = BeginWrite(&state_, nullptr, 1);
    EffectSlots(this)->Pack(field);
    EndField(&state_);
}

int CEffect::Receive(int update, BitStream* reader) {
    DeltaField* field = BeginRead(&state_, nullptr, update, reader);
    int result = EffectSlots(this)->Unpack(update, field);
    EndField(&state_);
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
EI_REPLACE(0x006773A0, ReadSavedStrings);

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

double CEffect::DefaultRadius() { return ConstAt(kDefaultRadius); }

// --- CEffectSound --------------------------------------------------------------------------------

void CEffectSound::Reset() {
    ClearBase(this);
    if (sound_ != -1 && looping_ != 0) SoundStop(SoundSystem(), nullptr, sound_);
    sound_ = -1;
    looping_ = 1;
    name_[0] = '\0';
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
        if (sound_ != 0) MoveSound(SoundSystem(), nullptr, sound_, &localPosition_, Bits(volume_), Bits(radius_));
    }
    return looping_ == 0 && wasFresh == 0 ? 0 : 1;
}

void CEffectSound::Start(int update) {
    if (update == 0) {
        sound_ = PlaySound(SoundSystem(), nullptr, name_, noFade_ != 0 ? 1000 : 0, &localPosition_, Bits(volume_),
                           Bits(radius_), looping_, 0);
        return;
    }
    MoveSound(SoundSystem(), nullptr, sound_, &localPosition_, Bits(volume_), Bits(radius_));
}

int CEffectSound::Unpack(int update, DeltaField* field) {
    Vector3 position;
    ReadPosition(field, nullptr, &position);
    ReadDelta(field, nullptr, &looping_, 4);
    ReadText(field, nullptr, name_, 0xFF);
    ReadFloat16(field, nullptr, &volume_);
    ReadFloat16(field, nullptr, &radius_);
    ReadFloat16x3(field, nullptr, &velocity_);
    ReadDelta(field, nullptr, &noFade_, 1);
    moving_ = IsMoving(velocity_);
    if (!SnapshotReadToEnd()) return 0;
    localPosition_ = position;
    EffectSlots(this)->Start(update);
    return 1;
}

void CEffectSound::Pack(DeltaField* field) {
    Vector3 position = localPosition_;
    WritePosition(field, nullptr, &position);
    int looping = looping_;
    WriteDelta(field, nullptr, &looping, 4);
    WriteString(field, nullptr, name_);
    WriteFloat16(field, nullptr, Bits(volume_));
    WriteFloat16(field, nullptr, Bits(radius_));
    WriteFloat16x3(field, nullptr, &velocity_);
    WriteDelta(field, nullptr, &noFade_, 1);
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
    if (sound_ != -1) StopSequence(SoundSystem(), nullptr, sound_);
    sound_ = -1;
    volume_ = 0.0f;
    radius_ = 0.0f;
    *reinterpret_cast<mfc::CString*>(&name_) = EmptyCString();
    StringArraySetSize(&names_, nullptr, 0, -1);
    noFade_ = 0;
}

int CEffectSoundSeq::Tick() {
    if (moving_ != 0) {
        MoveByVelocity(this);
        if (sound_ != 0)
            MoveSequence(SoundSystem(), nullptr, sound_, &localPosition_, Bits(volume_), Bits(radius_), -1);
    }
    return 1;
}

void CEffectSoundSeq::Pack(DeltaField* field) {
    Vector3 position = localPosition_;
    WritePosition(field, nullptr, &position);
    WriteString(field, nullptr, name_);
    int count = names_.size;
    WriteDelta(field, nullptr, &count, 4);
    for (int i = 0; i < names_.size; ++i) WriteString(field, nullptr, names_[i]);
    WriteFloat16(field, nullptr, Bits(volume_));
    WriteFloat16(field, nullptr, Bits(radius_));
    WriteFloat16x3(field, nullptr, &velocity_);
    WriteDelta(field, nullptr, &noFade_, 1);
}

void CEffectSoundSeq::LoadState(void* stream) {
    Read(stream, &localPosition_, 12);
    Read(stream, &velocity_, 12);
    ReadStreamCString(stream, nullptr, &name_);
    ReadStringArray(&names_, stream);
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
    if (sound_ != -1) sound_ = StopAmbient(SoundSystem(), nullptr, sound_);
    StringArraySetSize(&names_, nullptr, 0, -1);
    volume_ = 0.0f;
    radius_ = 0.0f;
    unknown_168 = 0;
    unknown_16C = 0;
}

void CEffectAmbientSound::Start(int update) {
    if (update == 0) {
        sound_ = PlayAmbient(SoundSystem(), nullptr, &names_, &localPosition_, Bits(volume_), Bits(radius_),
                             unknown_168, unknown_16C);
        return;
    }
    MoveAmbient(SoundSystem(), nullptr, sound_, &localPosition_, Bits(volume_), Bits(radius_), unknown_168,
                unknown_16C);
}

int CEffectAmbientSound::Unpack(int update, DeltaField* field) {
    Vector3 position;
    ReadPosition(field, nullptr, &position);
    // The count is read into the low byte of the argument slot that held `field`.
    std::uint32_t countSlot = reinterpret_cast<std::uintptr_t>(field);
    ReadDelta(field, nullptr, &countSlot, 1);
    StringArraySetSize(&names_, nullptr, static_cast<int>(countSlot & 0xFF), -1);
    for (int i = 0; i < static_cast<int>(countSlot & 0xFF); ++i) {
        alignas(4) unsigned char raw[sizeof(mfc::CString)];
        auto* text = static_cast<mfc::CString*>(FieldReadString(field, nullptr, raw));
        names_[i] = *text;
        text->~CString();
    }
    ReadFloat16(field, nullptr, &volume_);
    ReadFloat16(field, nullptr, &radius_);
    ReadDelta(field, nullptr, &unknown_168, 4);
    ReadDelta(field, nullptr, &unknown_16C, 4);
    if (!SnapshotReadToEnd()) return 0;
    localPosition_ = position;
    EffectSlots(this)->Start(update);
    return 1;
}

void CEffectAmbientSound::Pack(DeltaField* field) {
    Vector3 position = localPosition_;
    WritePosition(field, nullptr, &position);
    std::uint8_t count = static_cast<std::uint8_t>(names_.size);
    WriteDelta(field, nullptr, &count, 1);
    for (int i = 0; i < names_.size; ++i) WriteString(field, nullptr, names_[i]);
    WriteFloat16(field, nullptr, Bits(volume_));
    WriteFloat16(field, nullptr, Bits(radius_));
    int value = unknown_168;
    WriteDelta(field, nullptr, &value, 4);
    value = unknown_16C;
    WriteDelta(field, nullptr, &value, 4);
}

void CEffectAmbientSound::LoadState(void* stream) {
    Read(stream, &localPosition_, 12);
    ReadStringArray(&names_, stream);
    Read(stream, &volume_, 4);
    Read(stream, &radius_, 4);
    Read(stream, &unknown_168, 4);
    Read(stream, &unknown_16C, 4);
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
    WriteCopy(stream, &unknown_168);
    WriteCopy(stream, &unknown_16C);
}

double CEffectAmbientSound::Radius() { return radius_; }

// --- Hooks ---------------------------------------------------------------------------------------

EI_REPLACE_METHOD(0x00675B40, CEffect::SendFull);
EI_REPLACE_METHOD(0x00675B70, CEffect::SendDelta);
EI_REPLACE_METHOD(0x00675BA0, CEffect::Receive);
EI_REPLACE_METHOD(0x00675BE0, CEffect::ResetBase);
EI_REPLACE_METHOD(0x00675CA0, CEffect::MarkChanged);
EI_REPLACE_METHOD(0x00675D10, CEffect::CreateEffect);
EI_REPLACE_METHOD(0x00675D40, CEffect::DestroyEffect);
EI_REPLACE_METHOD(0x00675D50, CEffect::SetVelocity);
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
