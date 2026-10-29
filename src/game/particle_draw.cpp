// The particle manager's draw pass and its draw order: every source's depth along the camera's view axis goes
// into an array, which is sorted far-to-near with the game's sort (lib/intro_sort.h: sources at the same depth
// keep its order) and drawn in that order. Names are provisional.
#include "effects.h"
#include "map_object.h"
#include "map_object_figures.h"

#include "engine_object.h"

#include "lib/intro_sort.h"
#include "lib/vecmath.h"
#include "camera.h"

#include <bit>
#include <cstdlib>
#include <cstring>

namespace {

inline double D(float f) { return f; }

// The particles' draw order, reused by every frame.
std::vector<ParticleDepth> g_drawOrder;
// The orientation of the camera the frame shows (camera::FrameView).
const Quaternion& CameraRotation() { return camera::FrameView().rotation; }

// A source attached to a unit (class 0x50) is drawn only while the unit has MapObjectFlag::InParty.
bool AttachedToHiddenUnit(const CParticleSource* s) {
    CEngineObject* object = s->attachedTo;
    if (!object) return false;
    return object->GetClassId() == ClassId::Unit && !(object->flags & MapObjectFlag::InParty);
}

} // namespace

// The attachment point plus the local position, or the local position alone.
Vector3* CParticleSource::WorldPosition(Vector3* out) {
    if (attachedTo) {
        Vector3 point;
        const Vector3* a = AttachmentPoint(&point, attachedTo);
        double z = D(a->z) + D(localPosition.z);
        double y = D(a->y) + D(localPosition.y);
        double x = D(a->x) + D(localPosition.x);
        out->x = static_cast<float>(x);
        out->y = static_cast<float>(y);
        out->z = static_cast<float>(z);
        return out;
    }
    *out = localPosition;
    return out;
}

// Rebuilds the draw order (depth = position . camera view axis, iterating the manager's
// sources until the first empty entry), sorts it far to near and draws each source that is not
// hidden (ParticleSourceFlag::NotDrawn) and not on a hidden unit, switching to additive blending for sources with
// +0x150 == 1. Returns 0 as soon as a source fails to draw. The manager is held (+4) meanwhile.
int CParticleManager::DrawAll(CSceneRenderer* renderer) {
    ++refCount;
    std::vector<ParticleDepth>& order = g_drawOrder;
    lib::FreeStorage(order);
    Vector3 axis;
    CameraRotation().AxisZ(&axis);

    // The manager's objects are its sources.
    auto sourceOf = [](CEngineObject* object) { return static_cast<CParticleSource*>(object); };
    auto node = objectList.begin();
    CParticleSource* source = node != objectList.end() ? sourceOf(*node) : nullptr;
    while (source) {
        ++node;
        Vector3 scratch;
        const Vector3* p = source->WorldPosition(&scratch);
        ParticleDepth entry;
        entry.source = source;
        entry.depth = static_cast<float>(DotUnrounded<Sum3::ZYX, double>(axis, *p));
        lib::SetAtGrow(order, std::ssize(order), entry);
        source = node != objectList.end() ? sourceOf(*node) : nullptr;
    }

    // Far (greater depth) first; false for NaN.
    lib::IntroSort(order.data(), order.data() + order.size(),
                   [](const ParticleDepth& a, const ParticleDepth& b) { return a.depth > b.depth; });

    bool additive = false;
    for (int i = 0; i < std::ssize(order); ++i) {
        CParticleSource* s = order[i].source;
        if (s->flags & ParticleSourceFlag::NotDrawn) continue;
        if (AttachedToHiddenUnit(s)) continue;
        int blend;
        blend = std::bit_cast<std::int32_t>(s->additiveBlend);
        if (blend == 1 && !additive) {
            renderer->SetBlend(render::Blend::SourceAlpha, render::Blend::One);
            additive = true;
        }
        blend = std::bit_cast<std::int32_t>(s->additiveBlend);
        if (blend == 0 && additive) {
            renderer->SetBlend(render::Blend::SourceAlpha, render::Blend::InvSourceAlpha);
            additive = false;
        }
        if (!s->Draw(renderer)) {
            --refCount;
            return 0;
        }
    }
    --refCount;
    return 1;
}
