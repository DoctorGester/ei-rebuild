// The game's answers to the particle core (particle_host.h): the client world's ground, platforms, wind, tick fraction
// and scorch marks, and the map objects and units the sources follow.
#include "engine/particles/particle_host.h"

#include "game/app/singletons.h"
#include "engine/particles/particle_record.h"
#include "engine/particles/particle_source.h"
#include "game/objects/map_object.h"
#include "game/units/unit.h"
#include "game/world/world.h"

#include "lib/bit_flags.h"

#include <cmath>
#include <vector>

namespace {

// The part each AttachPoint asks for (EffectPoint none); Body asks for "bd" twice (the fallback part).
const char* const kPartNames[13] = {nullptr, "hd", "hd", "lh2", "rh2", "ll2", "rl2", "bd",
                                    "body00", "body01", "body02", "body03", "body04"};

// The world position of `object`'s first part named `prefix...`, else of its "bd" part, else its position raised by
// the bounds' upper z extent.
Vector3 PartPoint(Object3D* object, const char* prefix) {
    FigureMesh* part = object->FindPartByPrefix(prefix);
    if (!part) part = object->FindPartByPrefix("bd");
    if (part) {
        Vector3 point = object->PartWorldPosition(part);
        return point;
    }
    return {object->localPosition.x, object->localPosition.y, object->localPosition.z + object->bounds.boxMax.z};
}

float& Value(std::vector<float>& values, effectdata::SourceValue which) { return values[static_cast<int>(which)]; }

} // namespace

float particlehost::GroundHeight(float x, float y) { return g_clientWorld.RawSurfaceHeightAt(x, y); }

float particlehost::PlatformPlane(float x, float y, int which) {
    if (which == 4) return g_clientWorld.platforms.SurfaceHeight({x, y, 0.0f}, g_clientWorld.RawSurfaceHeightAt(x, y));
    const PlatformSet::SurfacePlaneResult plane = g_clientWorld.platforms.SurfacePlane(x, y);
    switch (which) {
    case 0: return plane.height;
    case 1: return plane.normal.x;
    case 2: return plane.normal.y;
    default: return plane.normal.z;
    }
}

particlehost::Wind particlehost::CurrentWind() { return {g_clientWorld.windDirection, g_clientWorld.windStrength}; }

float particlehost::TickFraction() { return g_clientWorld.tickFraction; }

void particlehost::AddScorchMark(float x, float y, float radiusX, float radiusY, float angle, float alpha) {
    g_clientWorld.AddScorchMark(x, y, radiusX, radiusY, angle, alpha);
}

void particlehost::Follow(MapObject* carrier, ParticleSource* source) {
    carrier->ComputeBounds();
    // Adds the source and calls Attach(carrier) again (already attached: nothing).
    carrier->AddEffectSource(source);
}

void particlehost::LetGo(MapObject* carrier, ParticleSource* source) { carrier->RemoveEffectSource(source); }

bool particlehost::Removed(const MapObject* carrier) { return lib::Has(carrier->flags, MapObjectFlag::Removed); }

bool particlehost::Hidden(MapObject* carrier) {
    EngineObject* object = carrier;
    if (!object) return false;
    return object->GetClassId() == ClassId::Unit && !(object->flags & MapObjectFlag::InParty);
}

Vector3 particlehost::CarrierPoint(const ParticleSource& source, MapObject* carrier) {
    Object3D* object = carrier;
    if (!object) return {0.0f, 0.0f, 0.0f};
    if (!(source.flags & ParticleSourceFlag::OnPart)) return object->localPosition;
    if (Unit* unit = UnitOrNull(object)) {
        if (source.attachPoint == AttachPoint::EffectPoint) return unit->EffectPoint();
        Vector3 at = PartPoint(object, kPartNames[static_cast<int>(source.attachPoint)]);
        if (source.attachPoint == AttachPoint::AboveHead) at.z = at.z + 1.0f;
        return at;
    }
    float rise = lib::Has(object->bounds.flags, BoundsFlag::Sphere) ? object->bounds.radius * 1.6f
                                                                  : object->bounds.boxMax.z - object->bounds.boxMin.z;
    return {object->localPosition.x, object->localPosition.y, rise + object->localPosition.z};
}

particlehost::Measure particlehost::MeasureCarrier(MapObject* carrier) {
    Object3D* object = carrier;
    if (!object) return {{0.0f, 0.0f, 0.0f}, 0.1f};
    object->ComputeBounds();
    float radius;
    if (lib::Has(object->bounds.flags, BoundsFlag::Sphere)) {
        radius = object->bounds.radius;
    } else {
        radius = std::sqrt(LengthSquared(object->bounds.boxMax - object->bounds.boxMin)) * 0.5f;
    }
    return {{0.0f, 0.0f, object->bounds.center.z}, radius};
}

// Its feet as PartPoint finds a part.
void particlehost::WriteCarrier(const ParticleSource& source, std::vector<float>& v) {
    using effectdata::SourceValue;
    MapObject* carrier = source.attachedTo;
    Unit* unit = UnitOrNull(carrier);
    Value(v, SourceValue::CarrierUnit) = unit ? 1.0f : 0.0f;
    Value(v, SourceValue::CarrierRunning) = unit && unit->moveMode == MoveMode::Run ? 1.0f : 0.0f;
    Value(v, SourceValue::CarrierDead) = unit && unit->IsDead() ? 1.0f : 0.0f;
    const Vector3 axis = carrier ? carrier->localRotation.AxisX() : Vector3{};
    Value(v, SourceValue::CarrierAxisX) = axis.x;
    Value(v, SourceValue::CarrierAxisY) = axis.y;
    Value(v, SourceValue::CarrierAxisZ) = axis.z;
    Value(v, SourceValue::CarrierBoundsRadius) = carrier ? carrier->bounds.radius : 0.0f;
    Value(v, SourceValue::CarrierUnitRadius) = unit ? unit->Radius() : 0.0f;
    const Vector3 axisY = carrier ? carrier->localRotation.AxisY() : Vector3{};
    Value(v, SourceValue::CarrierAxisYX) = axisY.x;
    Value(v, SourceValue::CarrierAxisYY) = axisY.y;
    Value(v, SourceValue::CarrierAxisYZ) = axisY.z;
    const Vector3 left = unit ? PartPoint(unit, "ll3") : source.origin;
    const Vector3 right = unit ? PartPoint(unit, "rl3") : source.origin;
    Value(v, SourceValue::CarrierLeftFootX) = left.x;
    Value(v, SourceValue::CarrierLeftFootY) = left.y;
    Value(v, SourceValue::CarrierLeftFootZ) = left.z;
    Value(v, SourceValue::CarrierRightFootX) = right.x;
    Value(v, SourceValue::CarrierRightFootY) = right.y;
    Value(v, SourceValue::CarrierRightFootZ) = right.z;
}
