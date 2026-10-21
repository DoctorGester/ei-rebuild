// The lever map object's own functions (0052B1F0-0052BE40, A-U6): lifetime, update, save /
// load, network state, the zone record set-up and the small slots. SetState and UseSpots are in
// unit_lever.cpp. See docs/notes/units.md ("Levers").
#include "lever.h"

#include "core_maths.h"
#include "core_streams.h"
#include "map_object_figures.h"
#include "net_delta.h"
#include "sound.h"
#include "world.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/stlp_hash.h"
#include "originals_library.h"
#include "originals_units.h"
#include "originals_world.h"
#include "game_data.h"
#include "game_singletons.h"
#include "lib/nan_audit.h"

#include <initializer_list>
#include <new>

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

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
    self->flag260 = 0;
    self->keepsMapFlag = 0;
}
} // namespace

Lever::Lever() {
    lock = LockKey{1, 0, 0};
    CreateMapObject();
    ResetLever(this);
}

Lever* Lever::Construct() { return new (this) Lever(); }
EI_REWRITTEN(0x0052B1F0, Lever::Construct);

Lever::~Lever() { DestroyMapObject(); }

void Lever::DestructBody() { this->Lever::~Lever(); }
EI_REWRITTEN(0x0052B320, Lever::DestructBody);

Lever* Lever::DeletingDestruct(int flags) {
    this->Lever::~Lever();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REWRITTEN(0x0052B300, Lever::DeletingDestruct);

int Lever::CreateLever() {
    int result = CreateMapObject();
    ResetLever(this);
    return result;
}
EI_REWRITTEN(0x0052B370, Lever::CreateLever);

int Lever::UpdateLever() {
    Update();
    if (static_cast<CWorld*>(ManagerContext())->IsServer()) return 0;
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
EI_REPLACE_METHOD(0x0052B490, Lever::UpdateLever);

void Lever::LoadLever(CStream* stream) {
    LoadMapObject(stream);
    stream->Read(&state, 4);
    stream->Read(&stateCount, 4);
    stream->Read(&cycling, 4);
    lock.ReadFrom(stream);
    stream->Read(&flag260, 1);
    stream->Read(&keepsMapFlag, 1);
}
EI_REPLACE_METHOD(0x0052BDD0, Lever::LoadLever);

void Lever::SaveLever(CStream* stream) {
    SaveMapObject(stream);
    // Each value goes through a stack copy, as the original writes them.
    std::uint32_t value = static_cast<std::uint32_t>(state);
    stream->Write(&value, 4);
    value = static_cast<std::uint32_t>(stateCount);
    stream->Write(&value, 4);
    value = static_cast<std::uint32_t>(cycling);
    stream->Write(&value, 4);
    lock.WriteTo(stream);
    std::uint8_t byte = flag260;
    stream->Write(&byte, 1);
    byte = keepsMapFlag;
    stream->Write(&byte, 1);
}
EI_REPLACE_METHOD(0x0052BE40, Lever::SaveLever);

void Lever::Load(CStream* stream) { LoadLever(stream); }
void Lever::Save(CStream* stream) { SaveLever(stream); }
bool Lever::ApplyScience(int first, int second) {
    return ApplyKey(reinterpret_cast<const LockKey*>(static_cast<std::intptr_t>(first)), second);
}
void Lever::UseSpots(void* out, float userRadius) { CollectUseSpots(static_cast<LeverSpots*>(out), userRadius); }

void LockKey::ReadFrom(CStream* stream) {
    stream->Read(&mask, 4);
    stream->Read(&id, 4);
    stream->Read(&level, 4);
}
EI_REPLACE_METHOD(0x005229F0, LockKey::ReadFrom);

void LockKey::WriteTo(CStream* stream) const {
    for (std::uint32_t word : {mask, id, level}) stream->Write(&word, 4);
}
EI_REPLACE_METHOD(0x00522A30, LockKey::WriteTo);

void Lever::WriteLeverNetState() {
    MapObjectWriteNetState(reinterpret_cast<std::uint8_t*>(this));
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
EI_REPLACE_METHOD(0x0052B560, Lever::WriteLeverNetState);

std::uint32_t Lever::ReadLeverNetState(int first) {
    std::uint32_t complete = MapObjectReadNetState(reinterpret_cast<std::uint8_t*>(this), nullptr, first);
    DeltaField* field = NetState()->BeginField(kLeverField);
    if (!field) return complete;
    field->ReadDelta(&lock, sizeof(LockKey));
    field->ReadDelta(&from, 4);
    field->ReadDelta(&to, 4);
    field->ReadDelta(&endTick, 4);
    field->ReadDelta(&rate, 4);
    std::uint8_t disabled;
    field->ReadDelta(&disabled, 1);
    BitStream* snapshot = delta::ReadSnapshot();
    std::uint32_t readTo = snapshot->Position();
    std::uint32_t size = delta::ReadSnapshot()->Size();
    complete &= readTo == size ? 1u : 0u;
    field->EndField();
    if (disabled) flags_ |= kDisabled;
    else flags_ &= ~kDisabled;
    TheClientWorld()->QueueSynchronised(this);
    if (first == 1) {
        const LeverRecord* record = static_cast<const LeverRecord*>(stlp::RecordNamed(static_cast<stlp::RecordsByName*>(LeverTable()), nullptr, name));
        EI_NAN_AUDIT(from, to);
        if (record && from != to)
            TheSoundManager()->Play3D(record->sound, 0, &position.x, kSoundNear, kSoundFar, 0, 0);
    }
    return complete;
}
EI_REWRITTEN(0x0052B650, Lever::ReadLeverNetState);

std::uintptr_t Lever::SetUp(const LeverSetup* setup) {
    state = setup->state;
    stateCount = setup->stateCount;
    cycling = setup->cycling != 0 ? 1 : 0;
    lock = setup->lock;
    flag260 = setup->flag260;
    keepsMapFlag = setup->keepsMapFlag;
    if (stateCount > 1) scale.y = static_cast<float>(static_cast<double>(state) / (stateCount - 1));
    else scale.y = 0.0f;
    return MarkChanged(kPartScale);
}
EI_REPLACE_METHOD(0x0052B3F0, Lever::SetUp);

std::uintptr_t Lever::Enable(bool on) {
    if (on) flags_ &= ~kDisabled;
    else flags_ |= kDisabled;
    return MarkChanged(kLeverField);
}
EI_REPLACE_METHOD(0x0052BAD0, Lever::Enable);

std::uintptr_t Lever::SetLock(const LockKey* key) {
    lock = *key;
    return MarkChanged(kLeverField);
}
EI_REPLACE_METHOD(0x0052BB20, Lever::SetLock);

bool Lever::Slot29True() { return true; }
EI_REPLACE_METHOD(0x0052B2B0, Lever::Slot29True);

int Lever::Flag260Slot() { return flag260; }
EI_REPLACE_METHOD(0x0052B2D0, Lever::Flag260Slot);

bool Lever::KeepsMapFlagSlot() { return keepsMapFlag != 0; }
EI_REPLACE_METHOD(0x0052B2C0, Lever::KeepsMapFlagSlot);

int Lever::NeedsKey() {
    if (flags_ & kDisabled) return 0;
    return lock.mask != 0 ? 1 : 0;
}
EI_REPLACE_METHOD(0x0052BB00, Lever::NeedsKey);

std::uint32_t Lever::KeyId() { return (lock.mask & kKeyById) ? lock.id : 0; }
std::uint32_t __fastcall LeverKeyIdHook(Lever* self) { return self->Lever::KeyId(); }
EI_REPLACE(0x0052BBC0, LeverKeyIdHook);

bool Lever::ApplyKey(const LockKey* key, int) {
    if (!LockOpensOriginal(key, nullptr, &lock)) return false;
    SetState(-1, kNextStateDuration);
    flags_ |= kDisabled;
    MarkChanged(kLeverField);
    return true;
}
EI_REWRITTEN(0x0052BB50, Lever::ApplyKey);

BOOL Lever::KeyOpens(const LockKey* key) { return LockOpensOriginal(key, nullptr, &lock); }
EI_REWRITTEN(0x0052BBA0, Lever::KeyOpens);

double Lever::RadiusSlot() { return radius; }
EI_REPLACE_METHOD(0x0052B3E0, Lever::RadiusSlot);

const char* Lever::GetClassName() { return kLeverClassName; }
int Lever::GetClassId() { return kClassLever; }

const char* __fastcall LeverClassName(Lever* self) { return self->Lever::GetClassName(); }
EI_REPLACE(0x0052B2E0, LeverClassName);

int __fastcall LeverClassId(Lever* self) { return self->Lever::GetClassId(); }
EI_REPLACE(0x0052B2F0, LeverClassId);
