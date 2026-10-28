// The map object's small virtuals (CMapObject): the ones a plain object leaves empty (units, levers, traps and
// torches override them) and the network state's write / read wrappers. Virtual names: CMapObject
// (map_object.h).
#include "map_object.h"

#include "core_transform.h"
#include "map_object_figures.h"
#include "net_delta.h"
#include "unit.h"
#include "unit_create.h"

namespace {
constexpr int kNoKey = -123;                    // the object asks for no key
constexpr float kNoSpeed = 0.0f;
constexpr float kPlainRadius = 0.75f;
constexpr std::uint16_t kVitalParts = 0x11;     // changed parts 0 and 4: the statistics are repacked
constexpr std::uint32_t kStateUnsent = 2;       // flags_: the network state has not been written
} // namespace

// --- Empty slots -------------------------------------------------------------------------------------

// No key is accepted.
int CMapObject::AcceptsNoKey() { return 0; }

// A scaling effect came or went; 1.
int CMapObject::ScaleEffectChangedDefault() { return 1; }

// --- Slots that read the object ------------------------------------------------------------------------

int CMapObject::CompanionIsDead() { return companion->IsDead(); }

int CMapObject::NotifyCompanion() {
    companion->Unused14();
    return 1;
}

void CMapObject::TakePrototype(std::string_view prototype) {
    // Zeroed: the builder leaves the vitals alone when the prototype exists (it wants statistics too), and the
    // original then packs what its stack held; the harness zeroes them there too (a determinism fix,
    // harness_leftovers.h).
    UnitVitals vitals{};
    MapObjectManagerOf(this)->BuildFromPrototype(&vitals, nullptr, prototype);
    vitals.Pack(&packedVitals);
}

void CMapObject::WriteFullState() {
    DeltaState* state = &netState_;
    state->SetWriteMode(0);
    WriteNetState();
    state->Finish();
    flags_ &= ~kStateUnsent;
}

void CMapObject::WriteChangedState() {
    if (netState_.mask_ & kVitalParts) StatsChanged();
    DeltaState* state = &netState_;
    state->SetWriteMode(1);
    WriteNetState();
    state->Finish();
    flags_ &= ~kStateUnsent;
}

std::uint32_t CMapObject::ReadState(int first, CMemoryStream* reader) {
    DeltaState* state = &netState_;
    state->BeginRead(first, reader);
    std::uint32_t result = ReadNetState(first);
    state->Finish();
    return result;
}

std::string* CMapObject::SetSecondName(const char* text) {
    name2 = text;
    return &name2;
}

// --- The virtuals for the compiler's tables ----------------------------------------------------------------
// The bodies above and in map_object*.cpp / object_small_slots.cpp, called without dispatch.

void CMapObject::Load(CStream* stream) { LoadMapObject(stream); }
void CMapObject::Save(CStream* stream) { SaveMapObject(stream); }
bool CMapObject::InvisibleSlot() { return IsInvisible() != 0; }
void CMapObject::EffectsChanged() {}
bool CMapObject::IsSynchronised() { return Flag20000() != 0; }
void CMapObject::ApplyBaseScale() { ResetScale(); }
void CMapObject::ReceiveHit(DamageInfo*, CUnit*, CUnit*) {
    CompanionQuery(0, 0);
}
int CMapObject::StatsChanged() { return PackVitals(); }
void CMapObject::ScaleEffectChanged(int) {
    ScaleEffectChangedDefault();
}
void CMapObject::SetStats(const UnitStats*) {}
void CMapObject::UpdateSizeClasses() {}
int CMapObject::IsUsable() { return 0; }
int CMapObject::IsDirectUseBlocked() { return 0; }
std::uint32_t CMapObject::KeyId() { return static_cast<std::uint32_t>(kNoKey); }
int CMapObject::AcceptsKey(const LockKey*) { return AcceptsNoKey(); }
bool CMapObject::KeepsMapFlag() { return true; }
void CMapObject::UseSpots(std::vector<Vector3>* out, float) { FreeArray(out); }
std::uint32_t CMapObject::SetStunned(int) { return 0; }
Vector3* CMapObject::EyePoint(Vector3* out) {
    GroundPoint(&out->x);
    return out;
}
Vector3* CMapObject::PositionAt(Vector3* out, int) {
    GetPosition(&out->x);
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
void CMapObject::SetScriptName(std::string_view) {}
