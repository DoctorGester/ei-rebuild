// CUnit's own virtual slots (vtable 0073EA8C, shared by CFakeUnit 0073BFE8): the per-tick update
// and the slots that pass map object events on to the unit's controller (CUnit + 0x240), plus
// standing on the ground. See unit.h and docs/notes/units.md.
#include "combat.h"
#include "unit.h"

#include "map_object.h"
#include "object_tables.h"
#include "unit_db.h"
#include "world.h"

#include "ei/hook.h"
#include "originals_units.h"
#include "lib/crt.h"

#include <cstring>

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

namespace {
constexpr const char* kUnitClassName = "CUnit";
constexpr int kUnitClassId = 0x50;
constexpr int kControllerType = 0x48;         // CEngineObject::type_ of unit controllers
constexpr std::uint32_t kFlagFiguresLoaded = 0x80;
constexpr std::uint8_t kMovesOnWater = 2;     // CUnit::movementKind

// The world's slots 29 / 30 give the ground and water surface heights at (x, y), in ST0.
CWorld* WorldOf(CUnit* unit) { return static_cast<CWorld*>(unit->World()); }
} // namespace

// The map object's (map_object.cpp) and the 3D object's (map_object_figures.cpp) bodies these slots
// run are rewritten and called directly.

int CUnit::Tick() {
    controller->RunTick();
    Update();
    return 1;
}
EI_REPLACE_METHOD(0x0052F7D0, CUnit::Tick);

std::uintptr_t CUnit::RecomputeScale() {
    CMapObject::RecomputeScale();
    return Controller()->Resettle();
}
std::uintptr_t __fastcall UnitRecomputeScaleHook(CUnit* self) { return self->CUnit::RecomputeScale(); }
EI_REPLACE(0x00532A80, UnitRecomputeScaleHook);

std::uintptr_t CUnit::EffectsChanged() { return Controller()->EffectsChanged(); }
std::uintptr_t __fastcall UnitEffectsChangedHook(CUnit* self) { return self->CUnit::EffectsChanged(); }
EI_REPLACE(0x00532AB0, UnitEffectsChangedHook);

std::uint32_t CUnit::ApplyBaseScale() { return static_cast<std::uint32_t>(SetFigureScale(&baseScale.x)); }
std::uint32_t __fastcall UnitApplyBaseScaleHook(CUnit* self) { return self->CUnit::ApplyBaseScale(); }
EI_REPLACE(0x00532AA0, UnitApplyBaseScaleHook);

std::uintptr_t CUnit::LoadFigures() {
    if (flags_ & kFlagFiguresLoaded) return 1;
    std::uintptr_t result = static_cast<std::uintptr_t>(CObject3D::LoadFiguresImpl());
    Controller()->FiguresLoaded();
    return result;
}
std::uintptr_t __fastcall UnitLoadFiguresHook(CUnit* self) { return self->CUnit::LoadFigures(); }
EI_REPLACE(0x0052E910, UnitLoadFiguresHook);

std::uintptr_t CUnit::AttachObjectImpl(CEngineObject* object) {
    if (!object || object->type_ != kControllerType) return CMapObject::AttachObjectImpl(object);
    if (controller) {
        Controller()->unit_ = nullptr;
        controller->Release();
    }
    controller = object;
    static_cast<CUnitController*>(object)->unit_ = this;
    return static_cast<std::uintptr_t>(++controller->refCount_);
}
EI_REPLACE_METHOD(0x0052E870, CUnit::AttachObjectImpl);

int CUnit::StatsChanged() {
    PackVitals();
    if (controller) Controller()->RepackStats();
    return 1;
}
int __fastcall UnitStatsChangedHook(CUnit* self) { return self->CUnit::StatsChanged(); }
EI_REPLACE(0x0052EB50, UnitStatsChangedHook);

std::uintptr_t CUnit::ScaleEffectChangedImpl(int value) {
    if (!controller) return 0;
    return static_cast<std::uintptr_t>(Controller()->RefreshVitals(value));
}
EI_REPLACE_METHOD(0x0052EB30, CUnit::ScaleEffectChangedImpl);

std::uintptr_t CUnit::SetStatsImpl(const UnitStats* stats) {
    int classId = controller->GetClassId();
    if (classId != kClassUnitServer) return static_cast<std::uintptr_t>(classId);
    return static_cast<CUnitServer*>(controller)->SetStats(stats);
}
EI_REPLACE_METHOD(0x00530A20, CUnit::SetStatsImpl);

std::uintptr_t CUnit::UpdateSizeClasses() { return Controller()->UpdateSizeClasses(); }
std::uintptr_t __fastcall UnitUpdateSizeClassesHook(CUnit* self) { return self->CUnit::UpdateSizeClasses(); }
EI_REPLACE(0x00530A50, UnitUpdateSizeClassesHook);

float* CUnit::StandPoint(float* out) {
    float x = position.x;
    float y = position.y;
    float lift = static_cast<float>(ModeSpeed());   // the original inlines it
    CWorld* world = WorldOf(this);
    double height;
    if (movementKind == kMovesOnWater) {
        if (controller->GetClassId() == kClassUnitServer) {
            height = world->SurfaceHeightAt(x, y) + position.z;
        } else {
            height = world->SurfaceHeightAt(x, y) + heightOffset;
        }
    } else {
        height = world->GroundHeightAt(x, y);
    }
    out[0] = x;
    out[1] = y;
    out[2] = static_cast<float>(height + lift);
    return out;
}
EI_REPLACE_METHOD(0x005303A0, CUnit::StandPoint);

void CUnit::PlaceOnGroundImpl(float offset) {
    float x = position.x;
    float y = position.y;
    heightOffset = offset;
    CWorld* world = WorldOf(this);
    if (movementKind == kMovesOnWater) {
        if (controller->GetClassId() == kClassUnitServer) {
            position.z = static_cast<float>(world->SurfaceHeightAt(x, y) + position.z);
        } else {
            position.z = static_cast<float>(world->SurfaceHeightAt(x, y) + heightOffset);
        }
    } else {
        position.z = static_cast<float>(world->GroundHeightAt(x, y));
    }
}
EI_REPLACE_METHOD(0x0052F740, CUnit::PlaceOnGroundImpl);

void CUnit::SetFromStats(UnitStats* stats) {
    stats->Pack(this, Params());
    float curve = static_cast<float>(stats->HitPointCurve());
    float limit = ScaleHitPointsByEffects(this, curve);
    packedVitals.maxHitPoints = static_cast<std::int16_t>(gamecrt::Ftol(limit));
    packedVitals.hitPoints = static_cast<std::int16_t>(gamecrt::Ftol(stats->Reduce(limit)));
}
EI_REPLACE_METHOD(0x00532A20, CUnit::SetFromStats);

int CUnit::Level() {
    if (IsTextKey(id_) & 0xFF) return Controller()->Level();
    const auto* prototype =
        reinterpret_cast<const UnitPrototypeRecord*>(unitdb::Prototypes().RecordAt(prototypeId_, unitdb::kPrototypeSize));
    return static_cast<int>(prototype->level);
}
EI_REPLACE_METHOD(0x00532D40, CUnit::Level);

// --- The virtual slots (vtable 0073EA8C) for the compiler's table (C19) ---------------------------

int CUnit::Create() { return CreateUnit(); }
void CUnit::Destroy() { DestroyUnit(); }
void CUnit::ClearState() { ReleaseUnit(); }
int CUnit::RunTick() { return Tick(); }
const char* CUnit::GetName() { return scriptName; }
void CUnit::Load(void* stream) { LoadUnit(static_cast<CStream*>(stream)); }
void CUnit::Save(void* stream) { SaveUnit(static_cast<CStream*>(stream)); }
const char* CUnit::GetClassName() { return kUnitClassName; }
int CUnit::GetClassId() { return kUnitClassId; }
bool CUnit::AcceptsAnimation(const void* entry) { return AcceptsAnimationImpl(entry); }
std::uintptr_t CUnit::AttachObject(CEngineObject* object) { return AttachObjectImpl(object); }
void CUnit::AdvanceTime(float time) { AdvanceTimeImpl(time); }
bool CUnit::IsSynchronised() { return true; }
void CUnit::ReceiveHit(DamageInfo* hit, CEngineObject* attacker, CEngineObject* caster) { TakeHitImpl(hit, attacker, caster); }
std::uintptr_t CUnit::ScaleEffectChanged(int value) { return ScaleEffectChangedImpl(value); }
void CUnit::TakePrototype(const char* prototype) { TakePrototypeImpl(prototype); }
std::uintptr_t CUnit::SetStats(const UnitStats* stats) { return SetStatsImpl(stats); }
std::uint32_t CUnit::SetStunned(int on) { return SetStunnedImpl(on); }
Vector3* CUnit::EyePoint(Vector3* out) {
    StandPoint(&out->x);
    return out;
}
std::uint32_t CUnit::ReadNetState(int first) { return UnitReadNetState(reinterpret_cast<std::uint8_t*>(this), nullptr, first); }
void CUnit::WriteNetState() { UnitWriteNetState(reinterpret_cast<std::uint8_t*>(this)); }
void CUnit::PlaceOnGround(float offset) { PlaceOnGroundImpl(offset); }
Vector3* CUnit::PositionAt(Vector3* out, int time) {
    PositionAtImpl(&out->x, time);
    return out;
}
Vector3* CUnit::AimPoint(Vector3* out) {
    AimPointImpl(&out->x);
    return out;
}
int CUnit::IsLargeUnit() { return static_cast<int>(obstacle); }
int CUnit::IsDead() { return dead; }
int CUnit::ForgetObject(CEngineObject* object) {
    ObjectDeletedImpl(object);
    return 0;
}
void CUnit::UpdateMoveFactors() { Controller()->MoveFactorsChanged(); }
void CUnit::RefreshWounds() { Controller()->RefreshWounds(); }
double CUnit::MoveSpeed() {
    float forward;
    float backward;
    GetMoveSpeeds(&forward, &backward);
    return forward;
}
float CUnit::Radius() { return unitRadius; }
void CUnit::SetScriptName(const char* newName) { SetScriptNameImpl(newName); }
