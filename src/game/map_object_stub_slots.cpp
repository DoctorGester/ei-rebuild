// The map object's small virtuals (CMapObject): the ones a plain object leaves empty (units, levers, traps and
// torches override them) and the network state's write / read wrappers. Virtual names: CMapObject
// (map_object.h).
#include "map_object.h"

#include "core_transform.h"
#include "map_object_figures.h"
#include "net_delta.h"
#include "unit.h"
#include "unit_create.h"

#include "ei/hook.h"
#include "lib/crt.h"

namespace {
constexpr int kNoKey = -123;                    // the object asks for no key
constexpr float kNoSpeed = 0.0f;
constexpr float kPlainRadius = 0.75f;
constexpr std::uint16_t kVitalParts = 0x11;     // changed parts 0 and 4: the statistics are repacked
constexpr std::uint32_t kStateUnsent = 2;       // flags_: the network state has not been written
} // namespace

// --- Empty slots -------------------------------------------------------------------------------------

// Nothing: ClearState, EffectsChanged, UpdateSizeClasses, UpdateMoveFactors, RefreshWounds.
void __fastcall MapObjectNothing(CMapObject*) {}
EI_REPLACE(0x0050E760, MapObjectNothing);
EI_REPLACE(0x0050E420, MapObjectNothing);
EI_REPLACE(0x0050E490, MapObjectNothing);
EI_REPLACE(0x0050E520, MapObjectNothing);
EI_REPLACE(0x0050E530, MapObjectNothing);

// Nothing with one argument (RET 4): AdvanceTime, SetStats, SetStunned, ForgetObject.
void CMapObject::IgnoreArgument(int) {}
EI_REPLACE_METHOD(0x0050E410, CMapObject::IgnoreArgument);
EI_REPLACE_METHOD(0x0050E480, CMapObject::IgnoreArgument);
EI_REPLACE_METHOD(0x0050E4A0, CMapObject::IgnoreArgument);
EI_REPLACE_METHOD(0x0050E510, CMapObject::IgnoreArgument);

// 0: IsUsable, IsDirectUseBlocked, IsLargeUnit.
int __fastcall MapObjectZero(CMapObject*) { return 0; }
EI_REPLACE(0x0050F0C0, MapObjectZero);
EI_REPLACE(0x0050F0D0, MapObjectZero);
EI_REPLACE(0x0050E4F0, MapObjectZero);

// No key is accepted.
int CMapObject::AcceptsNoKey(const void* /*key*/) { return 0; }
EI_REWRITTEN(0x0050F110, CMapObject::AcceptsNoKey);

// Any animation may be played (AL).
bool CMapObject::AcceptsAnyAnimation(const void* /*animation*/) { return true; }
EI_REWRITTEN(0x0050E400, CMapObject::AcceptsAnyAnimation);

// A scaling effect came or went; 1.
int CMapObject::ScaleEffectChangedDefault(int /*scaleEffect*/) { return 1; }
EI_REWRITTEN(0x0050E960, CMapObject::ScaleEffectChangedDefault);

// The key the object asks for; none.
int __fastcall MapObjectKeyId(CMapObject*) { return kNoKey; }
EI_REWRITTEN(0x0050F0E0, MapObjectKeyId);

// A plain object does not move; its radius is 0.75 (ST0).
float __fastcall MapObjectMoveSpeed(CMapObject*) { return kNoSpeed; }
EI_REWRITTEN(0x0050E540, MapObjectMoveSpeed);
float __fastcall MapObjectRadius(CMapObject*) { return kPlainRadius; }
EI_REWRITTEN(0x0050E550, MapObjectRadius);

// --- Slots that read the object ------------------------------------------------------------------------

int CMapObject::CompanionIsDead() { return companion->IsDead(); }
EI_REWRITTEN(0x0050E500, CMapObject::CompanionIsDead);

int CMapObject::NotifyCompanion() {
    companion->Unused14();
    return 1;
}
EI_REWRITTEN(0x0050E930, CMapObject::NotifyCompanion);

void CMapObject::TakePrototype(const char* prototype) {
    // Zeroed: the builder leaves the vitals alone when the prototype exists (it wants statistics too), and the
    // original then packs what its stack held; the harness zeroes them there too (a determinism fix,
    // harness_leftovers.h).
    UnitVitals vitals{};
    MapObjectManagerOf(this)->BuildFromPrototype(&vitals, nullptr, prototype);
    vitals.Pack(PackedVitals());
}
EI_REWRITTEN(0x0050E9B0, CMapObject::TakePrototype);

void CMapObject::WriteFullState() {
    DeltaState* state = NetState();
    state->SetWriteMode(0);
    WriteNetState();
    state->Finish();
    flags_ &= ~kStateUnsent;
}
EI_REWRITTEN(0x0050EA40, CMapObject::WriteFullState);

void CMapObject::WriteChangedState() {
    if (ChangedParts() & kVitalParts) StatsChanged();
    DeltaState* state = NetState();
    state->SetWriteMode(1);
    WriteNetState();
    state->Finish();
    flags_ &= ~kStateUnsent;
}
EI_REWRITTEN(0x0050EA70, CMapObject::WriteChangedState);

std::uint32_t CMapObject::ReadState(int first, CMemoryStream* reader) {
    DeltaState* state = NetState();
    state->BeginRead(first, reader);
    std::uint32_t result = ReadNetState(first);
    state->Finish();
    return result;
}
EI_REWRITTEN(0x0050EAB0, CMapObject::ReadState);

std::string* CMapObject::SetSecondName(const char* text) {
    name2 = text;
    return &name2;
}
EI_REWRITTEN(0x0050F150, CMapObject::SetSecondName);

// --- The virtuals for the compiler's tables ----------------------------------------------------------------
// The bodies above and in map_object*.cpp / object_small_slots.cpp, called without dispatch.

void CMapObject::Load(CStream* stream) { LoadMapObject(stream); }
void CMapObject::Save(CStream* stream) { SaveMapObject(stream); }
bool CMapObject::InvisibleSlot() { return IsInvisible() != 0; }
std::uintptr_t CMapObject::EffectsChanged() { return 0; }
bool CMapObject::IsSynchronised() { return Flag20000() != 0; }
std::uint32_t CMapObject::ApplyBaseScale() { return ResetScale(); }
void CMapObject::ReceiveHit(DamageInfo*, CUnit*, CUnit*) {
    CompanionQuery(0, 0, 0);
}
int CMapObject::StatsChanged() { return PackVitals(); }
std::uintptr_t CMapObject::ScaleEffectChanged(int scaleEffect) {
    return static_cast<std::uintptr_t>(ScaleEffectChangedDefault(scaleEffect));
}
std::uintptr_t CMapObject::SetStats(const UnitStats*) { return 0; }
std::uintptr_t CMapObject::UpdateSizeClasses() { return 0; }
int CMapObject::IsUsable() { return 0; }
int CMapObject::IsDirectUseBlocked() { return 0; }
std::uint32_t CMapObject::KeyId() { return static_cast<std::uint32_t>(kNoKey); }
int CMapObject::AcceptsKey(const LockKey* key) { return AcceptsNoKey(key); }
bool CMapObject::KeepsMapFlag() { return true; }
void CMapObject::UseSpots(UseSpotArray* out, float) { FreeArray(out, 0); }
std::uint32_t CMapObject::SetStunned(int) { return 0; }
Vector3* CMapObject::EyePoint(Vector3* out) {
    GroundPoint(&out->x);
    return out;
}
Vector3* CMapObject::PositionAt(Vector3* out, int time) {
    GetPosition(&out->x, time);
    return out;
}
Vector3* CMapObject::AimPoint(Vector3* out) {
    GetPosition2(&out->x);
    return out;
}
int CMapObject::IsLargeUnit() { return 0; }
int CMapObject::IsDead() { return CompanionIsDead(); }
int CMapObject::ForgetObject(CEngineObject*) { return 0; }
void CMapObject::UpdateMoveFactors() {}
void CMapObject::RefreshWounds() {}
double CMapObject::MoveSpeed() { return kNoSpeed; }
float CMapObject::Radius() { return kPlainRadius; }
void CMapObject::SetScriptName(const char*) {}
