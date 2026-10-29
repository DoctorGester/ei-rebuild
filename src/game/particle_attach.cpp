// Particle sources that follow an object (A-S4): Attach, the attachment point
// and its part lookup, the attachment offset and StopAndClear
//. Names are provisional.
// The object a source follows is a CObject3D (attachedTo, +0x94). With ParticleSourceFlag::OnPart a source on a
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

void SetPosition(Vector3* out, const float* p) {
    out->x = p[0];
    out->y = p[1];
    out->z = p[2];
}

} // namespace

void ParticleSource::ReleaseAllParticles() {
    while (!particles.empty()) {
        Particle* p = particles.front();
        particles.pop_front();
        Manager()->ReleaseParticle(p);
    }
}

Vector3* ParticleSource::PartPoint(Vector3* out, CObject3D* object, const char* prefix) {
    FigureMesh* part = object->FindPartByPrefix(prefix);
    if (!part) part = object->FindPartByPrefix("bd");
    if (part) {
        Vector3 point;
        object->PartWorldPosition(&point.x, part);
        *out = point;
        return out;
    }
    out->x = object->localPosition.x;
    out->y = object->localPosition.y;
    out->z = object->localPosition.z + object->bounds.boxMax.z;
    return out;
}

Vector3* ParticleSource::AttachmentPoint(Vector3* out, CObject3D* object) {
    if (!object) {
        out->x = 0.0f;
        out->y = 0.0f;
        out->z = 0.0f;
        return out;
    }
    if (!(flags & ParticleSourceFlag::OnPart)) {
        SetPosition(out, &object->localPosition.x);
        return out;
    }
    const auto point = static_cast<std::uint32_t>(attachPoint);
    if (object->GetClassId() == ClassId::Unit && point <= 12) {
        if (point == 0) return static_cast<CUnit*>(object)->EffectPoint(out);
        PartPoint(out, object, kPartNames[point]);
        if (point == 2) out->z = out->z + 1.0f;
        return out;
    }
    float rise = lib::Has(object->bounds.flags, BoundsFlag::Sphere) ? object->bounds.radius * 1.6f
                                                                  : object->bounds.boxMax.z - object->bounds.boxMin.z;
    out->x = object->localPosition.x;
    out->y = object->localPosition.y;
    out->z = rise + object->localPosition.z;
    return out;
}

Vector3* ParticleSource::AttachmentOffset(Vector3* out, float* radius) {
    CObject3D* object = attachedTo;
    if (!object) {
        out->x = 0.0f;
        out->y = 0.0f;
        out->z = 0.0f;
        *radius = 0.1f;
        return out;
    }
    object->ComputeBounds();
    if (lib::Has(object->bounds.flags, BoundsFlag::Sphere)) {
        *radius = object->bounds.radius;
    } else {
        double dz = object->bounds.boxMax.z - object->bounds.boxMin.z;
        double dx = object->bounds.boxMax.x - object->bounds.boxMin.x;
        double dy = object->bounds.boxMax.y - object->bounds.boxMin.y;
        *radius = static_cast<float>(std::sqrt(LengthSquaredUnrounded<Sum3::XYZ>(dx, dy, dz)) * 0.5);
    }
    out->x = 0.0f;
    out->y = 0.0f;
    out->z = object->bounds.center.z;
    return out;
}

void ParticleSource::Attach(CObject3D* object) {
    CObject3D* old = attachedTo;
    if (old == object) {
        if (!old) origin = localPosition;
        return;
    }
    attachedTo = nullptr;
    if (old) {
        if (!object) {
            // Let go where the source is: the attachment point becomes part of the position.
            Vector3 point;
            AttachmentPoint(&point, old);
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
    Vector3 point;
    AttachmentPoint(&point, attachedTo);
    origin = point;
}

void ParticleSource::StopAndClear() {
    ReleaseAllParticles();
    flags &= ~(ParticleSourceFlag::Emitting | ParticleSourceFlag::KeepAlive);
    Attach(nullptr);
}
