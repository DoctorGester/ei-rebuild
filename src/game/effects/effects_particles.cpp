// The game's answers to the particle core (effects_particles.h): the client world's ground, platforms, wind, tick
// fraction and scorch marks, and the map objects and units the sources follow (every carrier in the game is a map
// object).
#include "game/effects/effects_particles.h"

#include "game/app/singletons.h"
#include "engine/particles/particle_source.h"
#include "game/objects/map_object.h"
#include "game/world/camera.h"
#include "game/units/unit.h"
#include "game/world/world.h"

#include "lib/bit_flags.h"

#include <cmath>

namespace {

// The world position of `object`'s first part named `prefix...`, else of its "bd" part, else its position raised by
// the bounds' upper z extent.
Vector3 PartPointOf(Object3D* object, const char* prefix) {
    FigureMesh* part = object->FindPartByPrefix(prefix);
    if (!part) part = object->FindPartByPrefix("bd");
    if (part) {
        Vector3 point = object->PartWorldPosition(part);
        return point;
    }
    return {object->localPosition.x, object->localPosition.y, object->localPosition.z + object->bounds.boxMax.z};
}

Object3D& ObjectOf(FigureInstance& carrier) { return static_cast<Object3D&>(carrier); }

class WorldHost final : public particlehost::Host {
public:
    float GroundHeight(float x, float y) override { return g_clientWorld.RawSurfaceHeightAt(x, y); }

    float PlatformPlane(float x, float y, int which) override {
        if (which == 4)
            return g_clientWorld.platforms.SurfaceHeight({x, y, 0.0f}, g_clientWorld.RawSurfaceHeightAt(x, y));
        const PlatformSet::SurfacePlaneResult plane = g_clientWorld.platforms.SurfacePlane(x, y);
        switch (which) {
        case 0: return plane.height;
        case 1: return plane.normal.x;
        case 2: return plane.normal.y;
        default: return plane.normal.z;
        }
    }

    particlehost::Wind CurrentWind() override { return {g_clientWorld.windDirection, g_clientWorld.windStrength}; }

    float TickFraction() override { return g_clientWorld.tickFraction; }

    void AddScorchMark(float x, float y, float radiusX, float radiusY, float angle, float alpha) override {
        g_clientWorld.AddScorchMark(x, y, radiusX, radiusY, angle, alpha);
    }

    void ShakeCamera(const Vector3& source, float duration, float amplitude, float decay) override {
        g_camera.Shake(source, duration, amplitude, decay);
    }

    void Follow(FigureInstance& carrier, ParticleSource* source) override {
        Object3D& object = ObjectOf(carrier);
        object.ComputeBounds();
        // Adds the source and calls Attach(carrier) again (already attached: nothing).
        object.AddEffectSource(source);
    }

    void LetGo(FigureInstance& carrier, ParticleSource* source) override { ObjectOf(carrier).RemoveEffectSource(source); }

    bool Removed(const FigureInstance& carrier) override { return lib::Has(carrier.flags, MapObjectFlag::Removed); }

    // A unit out of the party.
    bool Hidden(FigureInstance& carrier) override {
        return carrier.GetClassId() == ClassId::Unit && !(carrier.flags & MapObjectFlag::InParty);
    }

    Vector3 PartPoint(FigureInstance& carrier, AttachPoint part) override {
        Object3D* object = &ObjectOf(carrier);
        if (Unit* unit = UnitOrNull(object)) {
            if (part == AttachPoint::EffectPoint) return unit->EffectPoint();
            Vector3 at = PartPointOf(object, AttachPointPart(part));
            if (part == AttachPoint::AboveHead) at.z = at.z + 1.0f;
            return at;
        }
        float rise = lib::Has(object->bounds.flags, BoundsFlag::Sphere) ? object->bounds.radius * 1.6f
                                                                      : object->bounds.boxMax.z - object->bounds.boxMin.z;
        return {object->localPosition.x, object->localPosition.y, rise + object->localPosition.z};
    }

    particlehost::Measure MeasureCarrier(FigureInstance& carrier) override {
        Object3D& object = ObjectOf(carrier);
        object.ComputeBounds();
        float radius;
        if (lib::Has(object.bounds.flags, BoundsFlag::Sphere)) {
            radius = object.bounds.radius;
        } else {
            radius = std::sqrt(LengthSquared(object.bounds.boxMax - object.bounds.boxMin)) * 0.5f;
        }
        return {{0.0f, 0.0f, object.bounds.center.z}, radius};
    }

    // Its feet as PartPointOf finds a part.
    particlehost::CarrierState DescribeCarrier(FigureInstance& carrier) override {
        Object3D& object = ObjectOf(carrier);
        particlehost::CarrierState state;
        state.boundsRadius = object.bounds.radius;
        Unit* unit = UnitOrNull(&object);
        if (!unit) return state;
        state.unit = true;
        state.running = unit->moveMode == MoveMode::Run;
        state.dead = unit->IsDead();
        state.unitRadius = unit->Radius();
        state.leftFoot = PartPointOf(unit, "ll3");
        state.rightFoot = PartPointOf(unit, "rl3");
        return state;
    }
};

} // namespace

particlehost::Host& WorldParticleHost() {
    static WorldHost host;
    return host;
}
