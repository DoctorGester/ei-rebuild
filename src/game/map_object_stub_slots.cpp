// The map object's small virtual slots (CMapObject, vtable 0073DAA0; A-U12): the ones a plain
// object leaves empty (units, levers, traps and torches override them), its deleting destructor
// and the network state's write / read wrappers. Slot names: CMapObject (map_object.h).
#include "map_object.h"

#include "core_transform.h"
#include "map_object_figures.h"
#include "net_delta.h"
#include "unit.h"
#include "unit_create.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "originals_engine.h"

namespace {
constexpr int kNoKey = -123;                    // slot 40: the object asks for no key
constexpr float kNoSpeed = 0.0f;                // 0073B858
constexpr float kPlainRadius = 0.75f;           // 0073C524
constexpr std::uint16_t kVitalParts = 0x11;     // changed parts 0 and 4: the statistics are repacked
constexpr std::uint32_t kStateUnsent = 2;       // flags_: the network state has not been written
} // namespace

// --- Empty slots -------------------------------------------------------------------------------------

// Nothing (plain RET): 5 ClearState 0050E760, 28 EffectsChanged 0050E420, 37 UpdateSizeClasses
// 0050E490, 57 UpdateMoveFactors 0050E520, 58 RefreshWounds 0050E530. One byte each: too short to
// hook, registered so the addresses are known.
void __fastcall MapObjectNothing(CMapObject*) {}
EI_REPLACE(0x0050E760, MapObjectNothing);
EI_REPLACE(0x0050E420, MapObjectNothing);
EI_REPLACE(0x0050E490, MapObjectNothing);
EI_REPLACE(0x0050E520, MapObjectNothing);
EI_REPLACE(0x0050E530, MapObjectNothing);

// Nothing with one argument (RET 4): 25 AdvanceTime 0050E410, 35 SetStats 0050E480, 46 SetStunned
// 0050E4A0, 56 ForgetObject 0050E510.
void CMapObject::IgnoreArgument(int) {}
EI_REPLACE_METHOD(0x0050E410, CMapObject::IgnoreArgument);
EI_REPLACE_METHOD(0x0050E480, CMapObject::IgnoreArgument);
EI_REPLACE_METHOD(0x0050E4A0, CMapObject::IgnoreArgument);
EI_REPLACE_METHOD(0x0050E510, CMapObject::IgnoreArgument);

// 0: 38 IsUsable 0050F0C0, 39 IsDirectUseBlocked 0050F0D0, 54 IsLargeUnit 0050E4F0.
int __fastcall MapObjectZero(CMapObject*) { return 0; }
EI_REPLACE(0x0050F0C0, MapObjectZero);
EI_REPLACE(0x0050F0D0, MapObjectZero);
EI_REPLACE(0x0050E4F0, MapObjectZero);

// 0050F110 (slot 42, RET 4): no key is accepted.
int CMapObject::AcceptsNoKey(const void* /*key*/) { return 0; }
EI_REPLACE_METHOD(0x0050F110, CMapObject::AcceptsNoKey);

// 0050E400 (slot 18, RET 4; AL): any animation may be played.
bool CMapObject::AcceptsAnyAnimation(const void* /*animation*/) { return true; }
EI_REPLACE_METHOD(0x0050E400, CMapObject::AcceptsAnyAnimation);

// 0050E960 (slot 33, RET 4): a scaling effect came or went; 1.
int CMapObject::ScaleEffectChangedDefault(int /*scaleEffect*/) { return 1; }
EI_REPLACE_METHOD(0x0050E960, CMapObject::ScaleEffectChangedDefault);

// 0050F0E0 (slot 40): the key the object asks for; none.
int __fastcall MapObjectKeyId(CMapObject*) { return kNoKey; }
EI_REPLACE(0x0050F0E0, MapObjectKeyId);

// 0050E540 / 0050E550 (slots 59 / 60, ST0): a plain object does not move; its radius is 0.75.
float __fastcall MapObjectMoveSpeed(CMapObject*) { return kNoSpeed; }
EI_REPLACE(0x0050E540, MapObjectMoveSpeed);
float __fastcall MapObjectRadius(CMapObject*) { return kPlainRadius; }
EI_REPLACE(0x0050E550, MapObjectRadius);

// --- Slots that read the object ------------------------------------------------------------------------

CMapObject* CMapObject::DeleteMapObject(unsigned flags) {
    DestructBody();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x0050E560, CMapObject::DeleteMapObject);

int CMapObject::CompanionIsDead() { return static_cast<CMapObjectCompanion*>(companion)->IsDead(); }
EI_REPLACE_METHOD(0x0050E500, CMapObject::CompanionIsDead);

int CMapObject::NotifyCompanion() {
    static_cast<CMapObjectCompanion*>(companion)->Unused14();
    return 1;
}
EI_REPLACE_METHOD(0x0050E930, CMapObject::NotifyCompanion);

void CMapObject::TakePrototypeImpl(const char* prototype) {
    // Zeroed: the builder leaves the vitals alone when the prototype exists (it wants statistics
    // too), and the original then packs what its stack held; the harness zeroes them there too
    // (a determinism fix, SV7b3, harness_leftovers.h).
    UnitVitals vitals{};
    reinterpret_cast<UnitStatsBuilder*>(owner_)->BuildFromPrototype(&vitals, nullptr, prototype);
    vitals.Pack(PackedVitals());
}
EI_REPLACE_METHOD(0x0050E9B0, CMapObject::TakePrototypeImpl);

void CMapObject::WriteFullState() {
    DeltaState* state = NetState();
    state->SetWriteMode(0);
    ObjectSlots(this)->WriteNetState();
    state->Finish();
    flags_ &= ~kStateUnsent;
}
EI_REPLACE_METHOD(0x0050EA40, CMapObject::WriteFullState);

void CMapObject::WriteChangedState() {
    if (changedParts & kVitalParts) ObjectSlots(this)->StatsChanged();
    DeltaState* state = NetState();
    state->SetWriteMode(1);
    ObjectSlots(this)->WriteNetState();
    state->Finish();
    flags_ &= ~kStateUnsent;
}
EI_REPLACE_METHOD(0x0050EA70, CMapObject::WriteChangedState);

std::uint32_t CMapObject::ReadState(int first, CMemoryStream* reader) {
    DeltaState* state = NetState();
    state->BeginRead(first, reader);
    std::uint32_t result = ObjectSlots(this)->ReadNetState(first);
    state->Finish();
    return result;
}
EI_REPLACE_METHOD(0x0050EAB0, CMapObject::ReadState);

mfc::CString* CMapObject::SetSecondName(const char* text) {
    auto* string = reinterpret_cast<mfc::CString*>(&name2);
    *string = text;
    return string;
}
EI_REPLACE_METHOD(0x0050F150, CMapObject::SetSecondName);

void CMapObject::SaveTransformSlot(void* file) { Transform()->CTransformObject::SaveTransformImpl(file); }
EI_REPLACE_METHOD(0x00516350, CMapObject::SaveTransformSlot);

void CMapObject::LoadTransformSlot(void* file) { Transform()->CTransformObject::LoadTransformImpl(file); }
EI_REPLACE_METHOD(0x00516360, CMapObject::LoadTransformSlot);
