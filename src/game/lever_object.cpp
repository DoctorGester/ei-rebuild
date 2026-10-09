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
#include "originals_library.h"
#include "originals_units.h"
#include "originals_world.h"

#include <initializer_list>

namespace {
void* const kLeverVtable = reinterpret_cast<void*>(0x0073E740);
constexpr int kLeverNetFields = 6;
constexpr int kLeverField = 5;            // the network field and the MarkChanged part
constexpr int kPartScale = 1;
constexpr std::uint32_t kDisabled = 0x80000;   // flags_
constexpr std::uint32_t kKeyById = 8;           // lock mask bit: the id names the key
constexpr int kClassLever = 0x5D;
constexpr float kNextStateDuration = -10.0f;   // SetState(-1, -10): the model's default
constexpr float kSoundNear = 8.0f;
constexpr float kSoundFar = 40.0f;

const char* const kLeverClassName = reinterpret_cast<const char*>(0x0078F788);   // "CLeverObject"
int GameTick() { return *reinterpret_cast<const int*>(0x007AF61C); }

void SetVtable(Lever* lever) { *reinterpret_cast<void**>(lever) = kLeverVtable; }

// The tail the constructor and Create share (after the map object's Create).
void ResetLever(Lever* self) {
    self->rate = 0.0f;
    self->endTick = 0.0f;
    self->to = 0.0f;
    self->from = 0.0f;
    NetFieldsSetSizeOriginal(self->NetState(), nullptr, kLeverNetFields, -1);
    self->lock = LockKey{1, 0, 0};
    self->state = 0;
    self->stateCount = 1;
    self->cycling = 0;
    self->flag260 = 0;
    self->keepsMapFlag = 0;
}

// x87 FCOMP + TEST AH,0x40 not taken: the values differ and neither is NaN.
bool Differ(float a, float b) { return a < b || a > b; }
} // namespace

Lever* Lever::Construct() {
    CMapObject::Construct();
    lock = LockKey{1, 0, 0};
    SetVtable(this);
    CreateMapObject();
    ResetLever(this);
    return this;
}
EI_REPLACE_METHOD(0x0052B1F0, Lever::Construct);

void Lever::DestructBody() {
    SetVtable(this);
    DestroyMapObject();
    CMapObject::DestructBody();
}
EI_REPLACE_METHOD(0x0052B320, Lever::DestructBody);

Lever* Lever::DeletingDestruct(int flags) {
    DestructBody();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x0052B300, Lever::DeletingDestruct);

int Lever::CreateLever() {
    int result = CreateMapObject();
    ResetLever(this);
    return result;
}
EI_REPLACE_METHOD(0x0052B370, Lever::CreateLever);

int Lever::UpdateLever() {
    Update();
    if (static_cast<CWorld*>(ManagerContext())->IsServer()) return 0;
    float shown[3] = {scale.x, scale.y, scale.z};
    double t = (endTick - static_cast<double>(GameTick())) * rate;
    if (t > 0.0) {
        shown[1] = StoreFloat(static_cast<float>(to - (to - from) * t));
        Object3D()->SetFigureScale(shown);
        ClientRefreshObjectOriginal(ClientWorldObject(), nullptr, this);
        return 1;
    }
    if (Differ(shown[1], to)) {
        shown[1] = to;
        Object3D()->SetFigureScale(shown);
        ClientRefreshObjectOriginal(ClientWorldObject(), nullptr, this);
    }
    return 0;
}
EI_REPLACE_METHOD(0x0052B490, Lever::UpdateLever);

void Lever::LoadLever(CStream* stream) {
    LoadMapObject(stream);
    stream->Read(&state, 4);
    stream->Read(&stateCount, 4);
    stream->Read(&cycling, 4);
    stream->Read(&lock.mask, 4);   // 005229F0: the lock's three dwords
    stream->Read(&lock.id, 4);
    stream->Read(&lock.level, 4);
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
    for (std::uint32_t word : {lock.mask, lock.id, lock.level}) stream->Write(&word, 4);   // 00522A30
    std::uint8_t byte = flag260;
    stream->Write(&byte, 1);
    byte = keepsMapFlag;
    stream->Write(&byte, 1);
}
EI_REPLACE_METHOD(0x0052BE40, Lever::SaveLever);

void Lever::WriteLeverNetState() {
    MapObjectWriteNetState(reinterpret_cast<std::uint8_t*>(this));
    DeltaField* field = NetState()->BeginField(kLeverField);
    if (!field) return;
    if (Differ(to, scale.y)) {
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
    std::uint32_t position = snapshot->Position();
    std::uint32_t size = delta::ReadSnapshot()->Size();
    complete &= position == size ? 1u : 0u;
    field->EndField();
    if (disabled) flags_ |= kDisabled;
    else flags_ &= ~kDisabled;
    ClientQueueObjectOriginal(ClientWorldObject(), nullptr, this);
    if (first == 1) {
        const LeverRecord* record = LeverRecordOriginal(LeverTable(), nullptr, name);
        if (record && Differ(from, to))
            sound::Manager()->Play3D(record->sound, 0, &positionX, kSoundNear, kSoundFar, 0, 0);
    }
    return complete;
}
EI_REPLACE_METHOD(0x0052B650, Lever::ReadLeverNetState);

std::uintptr_t Lever::SetUp(const LeverSetup* setup) {
    state = setup->state;
    stateCount = setup->stateCount;
    cycling = setup->cycling != 0 ? 1 : 0;
    lock = setup->lock;
    flag260 = setup->flag260;
    keepsMapFlag = setup->keepsMapFlag;
    if (stateCount > 1) scale.y = StoreFloat(static_cast<float>(static_cast<double>(state) / (stateCount - 1)));
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
EI_REPLACE_METHOD(0x0052BBC0, Lever::KeyId);

bool Lever::ApplyKey(const LockKey* key, int) {
    if (!LockOpensOriginal(key, nullptr, &lock)) return false;
    SetState(-1, kNextStateDuration);
    flags_ |= kDisabled;
    MarkChanged(kLeverField);
    return true;
}
EI_REPLACE_METHOD(0x0052BB50, Lever::ApplyKey);

BOOL Lever::KeyOpens(const LockKey* key) { return LockOpensOriginal(key, nullptr, &lock); }
EI_REPLACE_METHOD(0x0052BBA0, Lever::KeyOpens);

double Lever::RadiusSlot() { return radius; }
EI_REPLACE_METHOD(0x0052B3E0, Lever::RadiusSlot);

const char* __fastcall LeverClassName(Lever*) { return kLeverClassName; }
EI_REPLACE(0x0052B2E0, LeverClassName);

int __fastcall LeverClassId(Lever*) { return kClassLever; }
EI_REPLACE(0x0052B2F0, LeverClassId);
