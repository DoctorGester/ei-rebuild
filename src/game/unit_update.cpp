// CUnit's own virtual slots (shared by CFakeUnit): the per-tick update and the slots that pass map object
// events on to the unit's controller (CUnit + 0x240), plus standing on the ground. See unit.h and
// docs/notes/units.md.
#include "combat.h"
#include "unit.h"

#include "map_object.h"
#include "object_tables.h"
#include "unit_db.h"
#include "world.h"

#include "ei/hook.h"
#include "lib/crt.h"

#include <cstring>

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

namespace {
constexpr const char* kUnitClassName = "CUnit";
constexpr int kUnitClassId = 0x50;
constexpr int kControllerType = 0x48;         // CEngineObject::type_ of unit controllers
constexpr std::uint32_t kFlagFiguresLoaded = 0x80;
constexpr std::uint8_t kMovesOnWater = 2;     // UnitParams::movementKind

// The world's slots 29 / 30 give the ground and water surface heights at (x, y), in ST0.
CWorld* WorldOf(CUnit* unit) { return unit->World(); }
} // namespace

// The map object's (map_object.cpp) and the 3D object's (map_object_figures.cpp) bodies these slots run are
// called directly.

int CUnit::Tick() {
    controller->RunTick();
    Update();
    return 1;
}
EI_REWRITTEN(0x0052F7D0, CUnit::Tick);

std::uintptr_t CUnit::RecomputeScale() {
    CMapObject::RecomputeScale();
    Controller()->Resettle();
    return 0;
}
EI_REWRITTEN(0x00532A80, CUnit::RecomputeScale);

std::uintptr_t CUnit::EffectsChanged() {
    Controller()->EffectsChanged();
    return 0;
}
EI_REWRITTEN(0x00532AB0, CUnit::EffectsChanged);

std::uint32_t CUnit::ApplyBaseScale() { return static_cast<std::uint32_t>(SetFigureScale(&baseScale.x)); }
EI_REWRITTEN(0x00532AA0, CUnit::ApplyBaseScale);

std::uintptr_t CUnit::LoadFigures() {
    if (flags_ & kFlagFiguresLoaded) return 1;
    std::uintptr_t result = CObject3D::LoadFigures();
    Controller()->FiguresLoaded();
    return result;
}
EI_REWRITTEN(0x0052E910, CUnit::LoadFigures);

std::uintptr_t CUnit::AttachObject(CEngineObject* object) {
    if (!object || object->type_ != kControllerType) return CMapObject::AttachObject(object);
    if (controller) {
        Controller()->unit = nullptr;
        controller->Release();
    }
    controller = object;
    static_cast<CUnitController*>(object)->unit = this;
    return static_cast<std::uintptr_t>(++controller->refCount_);
}
EI_REWRITTEN(0x0052E870, CUnit::AttachObject);

int CUnit::StatsChanged() {
    PackVitals();
    if (controller) Controller()->RepackStats();
    return 1;
}
EI_REWRITTEN(0x0052EB50, CUnit::StatsChanged);

std::uintptr_t CUnit::ScaleEffectChanged(int value) {
    if (!controller) return 0;
    return static_cast<std::uintptr_t>(Controller()->RefreshVitals(static_cast<char>(value)));
}
EI_REWRITTEN(0x0052EB30, CUnit::ScaleEffectChanged);

std::uintptr_t CUnit::SetStats(const UnitStats* stats) {
    int classId = controller->GetClassId();
    if (classId != kClassUnitServer) return static_cast<std::uintptr_t>(classId);
    return ServerController()->SetStats(stats);
}
EI_REWRITTEN(0x00530A20, CUnit::SetStats);

std::uintptr_t CUnit::UpdateSizeClasses() {
    Controller()->UpdateSizeClasses();
    return 0;
}
EI_REWRITTEN(0x00530A50, CUnit::UpdateSizeClasses);

float* CUnit::StandPoint(float* out) {
    float x = localPosition_.x;
    float y = localPosition_.y;
    float lift = static_cast<float>(ModeSpeed());   // inlined
    CWorld* world = WorldOf(this);
    double height;
    if (params.movementKind == kMovesOnWater) {
        if (controller->GetClassId() == kClassUnitServer) {
            height = world->SurfaceHeightAt(x, y) + localPosition_.z;
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
EI_REWRITTEN(0x005303A0, CUnit::StandPoint);

void CUnit::PlaceOnGround(float offset) {
    float x = localPosition_.x;
    float y = localPosition_.y;
    heightOffset = offset;
    CWorld* world = WorldOf(this);
    if (params.movementKind == kMovesOnWater) {
        if (controller->GetClassId() == kClassUnitServer) {
            localPosition_.z = static_cast<float>(world->SurfaceHeightAt(x, y) + localPosition_.z);
        } else {
            localPosition_.z = static_cast<float>(world->SurfaceHeightAt(x, y) + heightOffset);
        }
    } else {
        localPosition_.z = static_cast<float>(world->GroundHeightAt(x, y));
    }
}
EI_REWRITTEN(0x0052F740, CUnit::PlaceOnGround);

void CUnit::SetFromStats(UnitStats* stats) {
    stats->Pack(this, Params());
    float curve = static_cast<float>(stats->HitPointCurve());
    float limit = ScaleHitPointsByEffects(this, curve);
    packedVitals.maxHitPoints = static_cast<std::int16_t>(gamecrt::Ftol(limit));
    packedVitals.hitPoints = static_cast<std::int16_t>(gamecrt::Ftol(stats->Reduce(limit)));
}
EI_REWRITTEN(0x00532A20, CUnit::SetFromStats);

int CUnit::Level() {
    if (IsTextKey(id_) & 0xFF) return Controller()->Level();
    const auto* prototype =
        unitdb::PrototypeRecord(prototypeId_);
    return static_cast<int>(prototype->level);
}
EI_REWRITTEN(0x00532D40, CUnit::Level);

// --- The virtuals -------------------------------------------------------------

int CUnit::Create() { return CreateUnit(); }
void CUnit::Destroy() { DestroyUnit(); }
void CUnit::ClearState() { ReleaseUnit(); }
int CUnit::RunTick() { return Tick(); }
const char* CUnit::GetName() { return scriptName; }
void CUnit::Load(CStream* stream) { LoadUnit(stream); }
void CUnit::Save(CStream* stream) { SaveUnit(stream); }
const char* CUnit::GetClassName() { return kUnitClassName; }
int CUnit::GetClassId() { return kUnitClassId; }
bool CUnit::IsSynchronised() { return true; }
Vector3* CUnit::EyePoint(Vector3* out) {
    StandPoint(&out->x);
    return out;
}
int CUnit::IsLargeUnit() { return static_cast<int>(obstacle); }
int CUnit::IsDead() { return dead; }
void CUnit::UpdateMoveFactors() { Controller()->MoveFactorsChanged(); }
void CUnit::RefreshWounds() { Controller()->RefreshWounds(); }
double CUnit::MoveSpeed() {
    float forward;
    float backward;
    GetMoveSpeeds(&forward, &backward);
    return forward;
}
float CUnit::Radius() { return unitRadius; }
