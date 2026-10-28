// The lever map object's own functions: lifetime, update, save / load, network state, the zone record set-up
// and the small slots. SetState and UseSpots are in See docs/notes/units.md ("Levers").
#include "lever.h"

#include "core_maths.h"
#include "core_streams.h"
#include "map_object_figures.h"
#include "net_delta.h"
#include "sound.h"
#include "world.h"

#include "game_singletons.h"
#include "lib/nan_audit.h"

#include <initializer_list>
#include <new>

#undef GetClassName   // windows.h's macro would rename CEngineObject::GetClassName

namespace {
constexpr int kLeverNetFields = 6;
constexpr int kLeverField = 5;            // the network field and the MarkChanged part
constexpr int kPartScale = 1;
constexpr std::uint32_t kDisabled = 0x80000;   // flags_
constexpr std::uint32_t kKeyById = 8;           // lock mask bit: the id names the key
constexpr int kClassLever = 0x5D;
constexpr float kNextStateDuration = -10.0f;   // SetState(-1, -10): the model's default
constexpr float kSoundNear = 8.0f;
constexpr float kSoundFar = 40.0f;

const char* const kLeverClassName = "CLeverObject";

// The tail the constructor and Create share (after the map object's Create).
void ResetLever(Lever* self) {
    self->rate = 0.0f;
    self->endTick = 0.0f;
    self->to = 0.0f;
    self->from = 0.0f;
    self->NetState()->SetFieldCount(kLeverNetFields);
    self->lock = LockKey{1, 0, 0};
    self->state = 0;
    self->stateCount = 1;
    self->cycling = 0;
    self->directUseBlocked = 0;
    self->keepsMapFlag = 0;
}
} // namespace

Lever::Lever() {
    lock = LockKey{1, 0, 0};
    CreateMapObject();
    ResetLever(this);
}

Lever::~Lever() { DestroyMapObject(); }

int Lever::CreateLever() {
    int result = CreateMapObject();
    ResetLever(this);
    return result;
}

int Lever::UpdateLever() {
    Update();
    if (WorldContext()->IsServer()) return 0;
    Vector3 shown = {scale.x, scale.y, scale.z};
    double t = (endTick - static_cast<double>(CurrentClientTick())) * rate;
    if (t > 0.0) {
        shown.y = static_cast<float>(to - (to - from) * t);
        Object3D()->SetFigureScale(&shown.x);
        TheClientWorld()->RefreshPlatforms(this);
        return 1;
    }
    EI_NAN_AUDIT(shown.y, to);
    if (shown.y != to) {
        shown.y = to;
        Object3D()->SetFigureScale(&shown.x);
        TheClientWorld()->RefreshPlatforms(this);
    }
    return 0;
}

void Lever::LoadLever(CStream* stream) {
    LoadMapObject(stream);
    stream->Read(&state, 4);
    stream->Read(&stateCount, 4);
    stream->Read(&cycling, 4);
    lock.ReadFrom(stream);
    stream->Read(&directUseBlocked, 1);
    stream->Read(&keepsMapFlag, 1);
}

void Lever::SaveLever(CStream* stream) {
    SaveMapObject(stream);
    std::uint32_t value = static_cast<std::uint32_t>(state);
    stream->Write(&value, 4);
    value = static_cast<std::uint32_t>(stateCount);
    stream->Write(&value, 4);
    value = static_cast<std::uint32_t>(cycling);
    stream->Write(&value, 4);
    lock.WriteTo(stream);
    std::uint8_t byte = directUseBlocked;
    stream->Write(&byte, 1);
    byte = keepsMapFlag;
    stream->Write(&byte, 1);
}

void Lever::Load(CStream* stream) { LoadLever(stream); }
void Lever::Save(CStream* stream) { SaveLever(stream); }
bool Lever::ApplyScience(const LockKey* key, CEngineObject*) { return ApplyKey(key, 0); }
void Lever::UseSpots(UseSpotArray* out, float userRadius) { CollectUseSpots(out, userRadius); }

void LockKey::ReadFrom(CStream* stream) {
    stream->Read(&mask, 4);
    stream->Read(&id, 4);
    stream->Read(&level, 4);
}

void LockKey::WriteTo(CStream* stream) const {
    for (std::uint32_t word : {mask, id, level}) stream->Write(&word, 4);
}

void Lever::WriteLeverNetState() {
    CMapObject::WriteNetState();
    DeltaField* field = NetState()->BeginField(kLeverField);
    if (!field) return;
    EI_NAN_AUDIT(to, scale.y);
    if (to != scale.y) {
        rate = 0.0f;
        to = scale.y;
    }
    field->WriteDelta(&lock, sizeof(LockKey));
    for (float value : {from, to, endTick, rate}) field->WriteDelta(&value, 4);
    std::uint8_t disabled = static_cast<std::uint8_t>((flags_ >> 19) & 1);
    field->WriteDelta(&disabled, 1);
    field->EndField();
}

std::uint32_t Lever::ReadLeverNetState(int first) {
    std::uint32_t complete = CMapObject::ReadNetState(first);
    DeltaField* field = NetState()->BeginField(kLeverField);
    if (!field) return complete;
    field->ReadDelta(&lock, sizeof(LockKey));
    field->ReadDelta(&from, 4);
    field->ReadDelta(&to, 4);
    field->ReadDelta(&endTick, 4);
    field->ReadDelta(&rate, 4);
    std::uint8_t disabled;
    field->ReadDelta(&disabled, 1);
    BitStream* snapshot = delta::g_readSnapshot;
    std::uint32_t readTo = snapshot->Position();
    std::uint32_t size = delta::g_readSnapshot->Size();
    complete &= readTo == size ? 1u : 0u;
    field->EndField();
    if (disabled) flags_ |= kDisabled;
    else flags_ &= ~kDisabled;
    TheClientWorld()->QueueSynchronised(this);
    if (first == 1) {
        const LeverRecord* record = TheLeverTable().Named(name.c_str());
        EI_NAN_AUDIT(from, to);
        if (record && from != to)
            TheSoundManager()->Play3D(record->sound.c_str(), 0, &localPosition_.x, kSoundNear, kSoundFar, 0, 0);
    }
    return complete;
}

void Lever::SetUp(const LeverSetup* setup) {
    state = setup->state;
    stateCount = setup->stateCount;
    cycling = setup->cycling != 0 ? 1 : 0;
    lock = setup->lock;
    directUseBlocked = setup->directUseBlocked;
    keepsMapFlag = setup->keepsMapFlag;
    if (stateCount > 1) scale.y = static_cast<float>(static_cast<double>(state) / (stateCount - 1));
    else scale.y = 0.0f;
    MarkChanged(kPartScale);
}

void Lever::Enable(bool on) {
    if (on) flags_ &= ~kDisabled;
    else flags_ |= kDisabled;
    MarkChanged(kLeverField);
}

void Lever::SetLock(const LockKey* key) {
    lock = *key;
    MarkChanged(kLeverField);
}

bool Lever::Slot29True() { return true; }

bool Lever::KeepsMapFlagSlot() { return keepsMapFlag != 0; }

int Lever::NeedsKey() {
    if (flags_ & kDisabled) return 0;
    return lock.mask != 0 ? 1 : 0;
}

std::uint32_t Lever::KeyId() { return (lock.mask & kKeyById) ? lock.id : 0; }

bool Lever::ApplyKey(const LockKey* key, int) {
    if (!key->Opens(&lock)) return false;
    SetState(-1, kNextStateDuration);
    flags_ |= kDisabled;
    MarkChanged(kLeverField);
    return true;
}

BOOL Lever::KeyOpens(const LockKey* key) { return key->Opens(&lock); }

const char* Lever::GetClassName() { return kLeverClassName; }
int Lever::GetClassId() { return kClassLever; }
