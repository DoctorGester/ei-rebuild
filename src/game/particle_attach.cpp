// Particle sources that follow an object (A-S4): Attach, the attachment point
// and its part lookup, the attachment offset and StopAndClear
//. Names are provisional.
// The object a source follows is a Object3D (attachedTo, +0x94). With ParticleSourceFlag::OnPart a source on a
// unit sits on the part attachPoint names; otherwise on top of the object's bounds.
#include "effects.h"

#include "map_object_figures.h"
#include "unit.h"

#include "lib/vecmath.h"

#include <cmath>

namespace {


// The part each attachPoint 1..12 asks for (0 is the unit's effect point). 2 is the head raised
// by 1; 7 asks for "bd" twice (the fallback part).
const char* const kPartNames[13] = {nullptr, "hd", "hd", "lh2", "rh2", "ll2", "rl2", "bd",
                                    "body00", "body01", "body02", "body03", "body04"};

} // namespace

void ParticleSource::ReleaseAllParticles() {
    for (Particle* p : particles) Manager()->ReleaseParticle(p);
    particles.clear();
}

Vector3 ParticleSource::PartPoint(Object3D* object, const char* prefix) {
    FigureMesh* part = object->FindPartByPrefix(prefix);
    if (!part) part = object->FindPartByPrefix("bd");
    if (part) {
        Vector3 point;
        object->PartWorldPosition(&point.x, part);
        return point;
    }
    return {object->localPosition.x, object->localPosition.y, object->localPosition.z + object->bounds.boxMax.z};
}

Vector3 ParticleSource::AttachmentPoint(Object3D* object) {
    if (!object) return {0.0f, 0.0f, 0.0f};
    if (!(flags & ParticleSourceFlag::OnPart)) return object->localPosition;
    const auto point = static_cast<std::uint32_t>(attachPoint);
    if (object->GetClassId() == ClassId::Unit && point <= 12) {
        if (point == 0) return static_cast<Unit*>(object)->EffectPoint();
        Vector3 at = PartPoint(object, kPartNames[point]);
        if (point == 2) at.z = at.z + 1.0f;
        return at;
    }
    float rise = lib::Has(object->bounds.flags, BoundsFlag::Sphere) ? object->bounds.radius * 1.6f
                                                                  : object->bounds.boxMax.z - object->bounds.boxMin.z;
    return {object->localPosition.x, object->localPosition.y, rise + object->localPosition.z};
}

ParticleSource::Attachment ParticleSource::AttachmentOffset() {
    Object3D* object = attachedTo;
    if (!object) return {{0.0f, 0.0f, 0.0f}, 0.1f};
    object->ComputeBounds();
    float radius;
    if (lib::Has(object->bounds.flags, BoundsFlag::Sphere)) {
        radius = object->bounds.radius;
    } else {
        float dz = object->bounds.boxMax.z - object->bounds.boxMin.z;
        float dx = object->bounds.boxMax.x - object->bounds.boxMin.x;
        float dy = object->bounds.boxMax.y - object->bounds.boxMin.y;
        radius = std::sqrt(LengthSquaredUnrounded<Sum3::XYZ>(dx, dy, dz)) * 0.5f;
    }
    return {{0.0f, 0.0f, object->bounds.center.z}, radius};
}

void ParticleSource::Attach(Object3D* object) {
    Object3D* old = attachedTo;
    if (old == object) {
        if (!old) origin = localPosition;
        return;
    }
    attachedTo = nullptr;
    if (old) {
        if (!object) {
            // Let go where the source is: the attachment point becomes part of the position.
            Vector3 point = AttachmentPoint(old);
            float x = point.x + localPosition.x;
            float y = localPosition.y + point.y;
            float z = localPosition.z + point.z;
            localPosition.x = x;
            localPosition.y = y;
            localPosition.z = z;
        }
        // Drops the source and calls Attach(null) again (nothing to do then but the origin).
        old->RemoveEffectSource(this);
    }
    attachedTo = object;
    if (!object) {
        origin = localPosition;
        return;
    }
    object->ComputeBounds();
    // Adds the source and calls Attach(object) again (already attached: nothing).
    attachedTo->AddEffectSource(this);
    origin = AttachmentPoint(attachedTo);
}

void ParticleSource::StopAndClear() {
    ReleaseAllParticles();
    flags &= ~(ParticleSourceFlag::Emitting | ParticleSourceFlag::KeepAlive);
    Attach(nullptr);
}
