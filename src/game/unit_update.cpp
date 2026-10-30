// CUnit's own virtuals: the per-tick update and the virtuals that pass map object
// events on to the unit's controller, plus standing on the ground. See unit.h and docs/notes/units.md.
#include "combat.h"
#include "map_object.h"
#include "object_tables.h"
#include "unit.h"
#include "unit_db.h"
#include "world.h"

#include "lib/pc24.h"

#include <cstring>

namespace {
constexpr const char* kUnitClassName = "CUnit";
constexpr std::uint8_t kMovesOnWater = 2;     // UnitParams::movementKind

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
    if (flags & MapObjectFlag::FiguresLoaded) return;
    CObject3D::LoadFigures();
    Controller()->FiguresLoaded();
}

void CUnit::AttachObject(EngineObject* object) {
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

void CUnit::ScaleEffectChanged(bool scaleEffect) {
    if (controller) Controller()->RefreshVitals(scaleEffect);
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
    float x = localPosition.x;
    float y = localPosition.y;
    float lift = ModeSpeed();   // inlined
    CWorld* world = OwningWorld();
    double height;   // the world's double, or the surface plus z (one rounding each)
    if (params.movementKind == kMovesOnWater) {
        if (controller->GetClassId() == ClassId::UnitServer) {
            height = pc24::Add(world->SurfaceHeightAt(x, y), localPosition.z);
        } else {
            height = pc24::Add(world->SurfaceHeightAt(x, y), heightOffset);
        }
    } else {
        height = world->GroundHeightAt(x, y);
    }
    out[0] = x;
    out[1] = y;
    out[2] = pc24::Add(height, lift);
    return out;
}

void CUnit::PlaceOnGround(float offset) {
    float x = localPosition.x;
    float y = localPosition.y;
    heightOffset = offset;
    CWorld* world = OwningWorld();
    if (params.movementKind == kMovesOnWater) {
        if (controller->GetClassId() == ClassId::UnitServer) {
            localPosition.z = pc24::Add(world->SurfaceHeightAt(x, y), localPosition.z);
        } else {
            localPosition.z = pc24::Add(world->SurfaceHeightAt(x, y), heightOffset);
        }
    } else {
        localPosition.z = static_cast<float>(world->GroundHeightAt(x, y));
    }
}

void CUnit::SetFromStats(UnitStats* stats) {
    stats->Pack(&params);
    float curve = stats->HitPointCurve();
    float limit = ScaleHitPointsByEffects(this, curve);
    // Truncated as _ftol does (the low half of the 64-bit integer; 0 for a NaN).
    packedVitals.maxHitPoints = static_cast<std::int16_t>(static_cast<std::int64_t>(limit));
    packedVitals.hitPoints = static_cast<std::int16_t>(static_cast<std::int64_t>(stats->Reduce(limit)));

}

int CUnit::Level() {
    if (IsTextKey(id)) return Controller()->Level();
    const auto* prototype =
        unitdb::PrototypeRecord(prototypeId);
    return static_cast<int>(prototype->level);
}

// --- The virtuals -------------------------------------------------------------

void CUnit::Destroy() { DestroyUnit(); }
void CUnit::ClearState() { ReleaseUnit(); }
bool CUnit::RunTick() {
    Tick();
    return true;
}
const char* CUnit::GetName() { return scriptName.c_str(); }
void CUnit::Load(ByteStream* stream) { LoadUnit(stream); }
void CUnit::Save(ByteStream* stream) { SaveUnit(stream); }
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
