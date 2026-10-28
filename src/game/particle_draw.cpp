// The particle manager's draw pass and its draw order: every source's depth along the camera's view axis goes
// into an array, which is sorted far-to-near with the game's sort (lib/intro_sort.h: sources at the same depth
// keep its order) and drawn in that order. Names are provisional.
#include "effects.h"
#include "map_object_figures.h"

#include "engine_object.h"

#include "lib/intro_sort.h"
#include "lib/vecmath.h"
#include "camera.h"
#include "lib/static_object.h"

#include <bit>
#include <cstdlib>
#include <cstring>

namespace {

inline double D(float f) { return f; }

// The particles' draw order: built empty by a static initialiser, destroyed at exit.
constinit lib::StaticObject<std::vector<ParticleDepth>> g_drawOrder = /*007A9C50*/ {};
void DestroyDrawOrder() {
    g_drawOrder.Destroy();
}
// The camera's orientation.
const Quaternion& CameraRotation() { return g_camera->localRotation_; }

// The blend factors set: SRCBLEND, DESTBLEND (5 source alpha; 2 one: additive, 6 inverse source alpha: the
// default).
void SetRenderState(CSceneRenderer* renderer, int source, int dest) {
    renderer->SetBlend(static_cast<std::uint32_t>(source), static_cast<std::uint32_t>(dest));
}

// A source attached to a unit (class 0x50) is drawn only while the unit has flag 0x400000.
bool AttachedToHiddenUnit(const CParticleSource* s) {
    CEngineObject* object = s->attachedTo;
    if (!object) return false;
    return object->GetClassId() == 0x50 && !(object->flags_ & 0x400000);
}

} // namespace

void ConstructDrawOrder() {
    g_drawOrder.Construct();
    std::atexit(DestroyDrawOrder);
}

// The attachment point plus the local position, or the local position alone.
Vector3* CParticleSource::WorldPosition(Vector3* out) {
    if (attachedTo) {
        Vector3 point;
        const Vector3* a = AttachmentPoint(&point, attachedTo);
        double z = D(a->z) + D(localPosition_.z);
        double y = D(a->y) + D(localPosition_.y);
        double x = D(a->x) + D(localPosition_.x);
        out->x = static_cast<float>(x);
        out->y = static_cast<float>(y);
        out->z = static_cast<float>(z);
        return out;
    }
    *out = localPosition_;
    return out;
}

// Rebuilds the draw order (depth = position . camera view axis, iterating the manager's
// sources until the first empty entry), sorts it far to near and draws each source that is not
// hidden (flag 0x40) and not on a hidden unit, switching to additive blending for sources with
// +0x150 == 1. Returns 0 as soon as a source fails to draw. The manager is held (+4) meanwhile.
int CParticleManager::DrawAll(CSceneRenderer* renderer) {
    ++refCount_;
    std::vector<ParticleDepth>& order = *g_drawOrder;
    lib::FreeStorage(order);
    Vector3 axis;
    CameraRotation().AxisZ(&axis);

    // The manager's objects are its sources.
    auto sourceOf = [](CEngineObject* object) { return static_cast<CParticleSource*>(object); };
    auto node = objectList_.begin();
    CParticleSource* source = node != objectList_.end() ? sourceOf(*node) : nullptr;
    while (source) {
        ++node;
        Vector3 scratch;
        const Vector3* p = source->WorldPosition(&scratch);
        ParticleDepth entry;
        entry.source = source;
        entry.depth = static_cast<float>(DotUnrounded<Sum3::ZYX, double>(axis, *p));
        lib::SetAtGrow(order, std::ssize(order), entry);
        source = node != objectList_.end() ? sourceOf(*node) : nullptr;
    }

    // Far (greater depth) first; false for NaN.
    lib::IntroSort(order.data(), order.data() + order.size(),
                   [](const ParticleDepth& a, const ParticleDepth& b) { return a.depth > b.depth; });

    bool additive = false;
    for (int i = 0; i < std::ssize(order); ++i) {
        CParticleSource* s = order[i].source;
        if (s->flags_ & 0x40) continue;
        if (AttachedToHiddenUnit(s)) continue;
        int blend;
        blend = std::bit_cast<std::int32_t>(s->additiveBlend);
        if (blend == 1 && !additive) {
            SetRenderState(renderer, 5, 2);
            additive = true;
        }
        blend = std::bit_cast<std::int32_t>(s->additiveBlend);
        if (blend == 0 && additive) {
            SetRenderState(renderer, 5, 6);
            additive = false;
        }
        if (!s->Draw(renderer)) {
            --refCount_;
            return 0;
        }
    }
    --refCount_;
    return 1;
}
