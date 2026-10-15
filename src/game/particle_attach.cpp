// Particle sources that follow an object (A-S4): Attach (004849F0), the attachment point
// (00484C00) and its part lookup (00484B40), the attachment offset (00485280) and StopAndClear
// (00485380). Names are provisional.
//
// The object a source follows is a CObject3D (attachedTo, +0x94). With flag 0x10 a source on a
// unit sits on the part attachPoint (+0x13C) names; otherwise on top of the object's bounds.
#include "effects.h"

#include "map_object_figures.h"
#include "unit.h"

#include "ei/hook.h"
#include "lib/vecmath.h"

#include <cmath>

namespace {

constexpr std::uint32_t kFlagOnPart = 0x10;     // flags_: sit on a part of a unit
constexpr std::uint32_t kBoundsSphere = 1;      // CObject3D::boundsFlags: radius valid

// The part each attachPoint 1..12 asks for (0 is the unit's effect point). 2 is the head raised
// by 1; 7 asks for "bd" twice (the fallback part).
const char* const kPartNames[13] = {nullptr, "hd", "hd", "lh2", "rh2", "ll2", "rl2", "bd",
                                    "body00", "body01", "body02", "body03", "body04"};

CEngineObject* AsEngineObject(CParticleSource* source) { return reinterpret_cast<CEngineObject*>(source); }

void SetPosition(Vector3* out, const float* p) {
    out->x = p[0];
    out->y = p[1];
    out->z = p[2];
}

} // namespace

void CParticleSource::ReleaseAllParticles() {
    while (particles.count != 0) {
        auto* node = particles.head;
        Particle* p = node->data;
        particles.head = node->next;
        if (particles.head) particles.head->prev = nullptr;
        else particles.tail = nullptr;
        particles.FreeNode(node);
        static_cast<CParticleManager*>(owner_)->ReleaseParticle(p);
    }
}

Vector3* CParticleSource::PartPoint(Vector3* out, CObject3D* object, const char* prefix) {
    CFigureMesh* part = object->FindPartByPrefix(prefix);
    if (!part) part = object->FindPartByPrefix("bd");
    if (part) {
        Vector3 point;
        object->PartWorldPosition(&point.x, part);
        *out = point;
        return out;
    }
    out->x = object->position.x;
    out->y = object->position.y;
    out->z = object->position.z + object->above.z;
    return out;
}
EI_REPLACE_METHOD(0x00484B40, CParticleSource::PartPoint);

Vector3* CParticleSource::AttachmentPoint(Vector3* out, void* objectArg) {
    auto* object = static_cast<CObject3D*>(objectArg);
    if (!object) {
        out->x = 0.0f;
        out->y = 0.0f;
        out->z = 0.0f;
        return out;
    }
    if (!(flags_ & kFlagOnPart)) {
        SetPosition(out, &object->position.x);
        return out;
    }
    const auto point = static_cast<std::uint32_t>(attachPoint);
    if (object->GetClassId() == kClassUnit && point <= 12) {
        if (point == 0) return reinterpret_cast<CUnit*>(object)->EffectPoint(out);
        PartPoint(out, object, kPartNames[point]);
        if (point == 2) out->z = out->z + 1.0f;
        return out;
    }
    float rise = (object->boundsFlags & kBoundsSphere) ? object->radius * 1.6f
                                                         : object->above.z - object->below.z;
    out->x = object->position.x;
    out->y = object->position.y;
    out->z = rise + object->position.z;
    return out;
}
EI_REPLACE_METHOD(0x00484C00, CParticleSource::AttachmentPoint);

Vector3* CParticleSource::AttachmentOffset(Vector3* out, float* radius) {
    auto* object = static_cast<CObject3D*>(attachedTo);
    if (!object) {
        out->x = 0.0f;
        out->y = 0.0f;
        out->z = 0.0f;
        *radius = 0.1f;
        return out;
    }
    object->ComputeBounds();
    if (object->boundsFlags & kBoundsSphere) {
        *radius = object->radius;
    } else {
        double dz = object->above.z - object->below.z;
        double dx = object->above.x - object->below.x;
        double dy = object->above.y - object->below.y;
        *radius = static_cast<float>(x87::fsqrt(LengthSquaredUnrounded<Sum3::XYZ>(dx, dy, dz)) * 0.5);
    }
    out->x = 0.0f;
    out->y = 0.0f;
    out->z = object->center.z;
    return out;
}
EI_REPLACE_METHOD(0x00485280, CParticleSource::AttachmentOffset);

void CParticleSource::Attach(void* objectArg) {
    auto* object = static_cast<CObject3D*>(objectArg);
    auto* old = static_cast<CObject3D*>(attachedTo);
    if (old == object) {
        if (!old) origin = position;
        return;
    }
    attachedTo = nullptr;
    if (old) {
        if (!object) {
            // Let go where the source is: the attachment point becomes part of the position.
            Vector3 point;
            AttachmentPoint(&point, old);
            float x = point.x + position.x;
            float y = position.y + point.y;
            float z = position.z + point.z;
            position.x = x;
            position.y = y;
            position.z = z;
        }
        // Drops the source and calls Attach(null) again (nothing to do then but the origin).
        old->RemoveEffectSource(AsEngineObject(this));
    }
    attachedTo = object;
    if (!object) {
        origin = position;
        return;
    }
    object->ComputeBounds();
    // Adds the source and calls Attach(object) again (already attached: nothing).
    static_cast<CObject3D*>(attachedTo)->AddEffectSource(AsEngineObject(this));
    Vector3 point;
    AttachmentPoint(&point, attachedTo);
    origin = point;
}
EI_REPLACE_METHOD(0x004849F0, CParticleSource::Attach);

void CParticleSource::StopAndClear() {
    ReleaseAllParticles();
    flags_ &= ~5u;
    Attach(nullptr);
}
EI_REPLACE_METHOD(0x00485380, CParticleSource::StopAndClear);
