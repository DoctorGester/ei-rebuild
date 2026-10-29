// CUnit's own virtuals: the per-tick update and the virtuals that pass map object
// events on to the unit's controller, plus standing on the ground. See unit.h and docs/notes/units.md.
#include "combat.h"
#include "map_object.h"
#include "object_tables.h"
#include "unit.h"
#include "unit_db.h"
#include "world.h"

#include "lib/crt.h"

#include <cstring>

namespace {
constexpr const char* kUnitClassName = "CUnit";
constexpr std::uint8_t kMovesOnWater = 2;     // UnitParams::movementKind

// The world gives the ground and water surface heights at (x, y), unrounded.
CWorld* WorldOf(CUnit* unit) { return unit->World(); }
} // namespace

// The map object's (map_object.cpp) and the 3D object's (map_object_figures.cpp) bodies these slots run are
// called directly.

void CUnit::Tick() {
    controller->RunTick();
    Update();
}

void CUnit::RecomputeScale() {
    CMapObject::RecomputeScale();
    Controller()->Resettle();
}

void CUnit::EffectsChanged() {
    Controller()->EffectsChanged();
}

void CUnit::ApplyBaseScale() { SetFigureScale(&baseScale.x); }

void CUnit::LoadFigures() {
    if (flags_ & MapObjectFlag::FiguresLoaded) return;
    CObject3D::LoadFigures();
    Controller()->FiguresLoaded();
}

void CUnit::AttachObject(CEngineObject* object) {
    if (!object || SideTypeOf(object) != SideObjectType::Unit) {
        CMapObject::AttachObject(object);
        return;
    }
    if (controller) {
        Controller()->unit = nullptr;
        controller->Release();
    }
    controller = object;
    Controller()->unit = this;
    controller->AddRef();
}

int CUnit::StatsChanged() {
    PackVitals();
    if (controller) Controller()->RepackStats();
    return 1;
}

void CUnit::ScaleEffectChanged(int value) {
    if (controller) Controller()->RefreshVitals(static_cast<char>(value));
}

void CUnit::SetStats(const UnitStats* stats) {
    ClassId classId = controller->GetClassId();
    if (classId != ClassId::UnitServer) return;
    ServerController()->SetStats(stats);
}

void CUnit::UpdateSizeClasses() {
    Controller()->UpdateSizeClasses();
}

float* CUnit::StandPoint(float* out) {
    float x = localPosition_.x;
    float y = localPosition_.y;
    float lift = static_cast<float>(ModeSpeed());   // inlined
    CWorld* world = WorldOf(this);
    double height;
    if (params.movementKind == kMovesOnWater) {
        if (controller->GetClassId() == ClassId::UnitServer) {
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

void CUnit::PlaceOnGround(float offset) {
    float x = localPosition_.x;
    float y = localPosition_.y;
    heightOffset = offset;
    CWorld* world = WorldOf(this);
    if (params.movementKind == kMovesOnWater) {
        if (controller->GetClassId() == ClassId::UnitServer) {
            localPosition_.z = static_cast<float>(world->SurfaceHeightAt(x, y) + localPosition_.z);
        } else {
            localPosition_.z = static_cast<float>(world->SurfaceHeightAt(x, y) + heightOffset);
        }
    } else {
        localPosition_.z = static_cast<float>(world->GroundHeightAt(x, y));
    }
}

void CUnit::SetFromStats(UnitStats* stats) {
    stats->Pack(&params);
    float curve = static_cast<float>(stats->HitPointCurve());
    float limit = ScaleHitPointsByEffects(this, curve);
    packedVitals.maxHitPoints = static_cast<std::int16_t>(libcrt::Ftol(limit));
    packedVitals.hitPoints = static_cast<std::int16_t>(libcrt::Ftol(stats->Reduce(limit)));
}

int CUnit::Level() {
    if (IsTextKey(id_)) return Controller()->Level();
    const auto* prototype =
        unitdb::PrototypeRecord(prototypeId_);
    return static_cast<int>(prototype->level);
}

// --- The virtuals -------------------------------------------------------------

void CUnit::Create() { CreateUnit(); }
void CUnit::Destroy() { DestroyUnit(); }
void CUnit::ClearState() { ReleaseUnit(); }
bool CUnit::RunTick() {
    Tick();
    return true;
}
const char* CUnit::GetName() { return scriptName.c_str(); }
void CUnit::Load(CStream* stream) { LoadUnit(stream); }
void CUnit::Save(CStream* stream) { SaveUnit(stream); }
const char* CUnit::ClassName() { return kUnitClassName; }
ClassId CUnit::GetClassId() { return ClassId::Unit; }
bool CUnit::IsSynchronised() { return true; }
Vector3* CUnit::EyePoint(Vector3* out) {
    StandPoint(&out->x);
    return out;
}
bool CUnit::IsLargeUnit() { return obstacle != 0; }
bool CUnit::IsDead() { return dead; }
void CUnit::UpdateMoveFactors() { Controller()->MoveFactorsChanged(); }
void CUnit::RefreshWounds() { Controller()->RefreshWounds(); }
double CUnit::MoveSpeed() {
    float forward;
    float backward;
    GetMoveSpeeds(&forward, &backward);
    return forward;
}
float CUnit::Radius() { return unitRadius; }
