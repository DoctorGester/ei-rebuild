// WorldClient per-frame view code: filing visible objects into the frame's draw lists, sorting
// those lists, and picking the ground point under the cursor. See world.h and docs/notes/world.md.
#include "game/frame_lists.h"
#include "game/game_singletons.h"
#include "game/light.h"
#include "game/model.h"
#include "game/pipeline.h"
#include "game/options.h"
#include "game/world.h"

#include "game/core_maths.h"
#include "game/drawn_motion.h"
#include "game/map_object.h"
#include "game/map_object_figures.h"
#include "game/map_object_shadow.h"
#include "game/text_manager.h"
#include "lib/containers.h"
#include "game/terrain.h"

#include <cmath>
#include <cstring>
#include <memory>
#include <vector>
#include "lib/float_select.h"
#include "lib/vecmath.h"

namespace {


// The scene's transform pipeline: the picking stack, the clip stack an object's matrix is pushed on to classify it,
// and the view stack.
float* StackTop(TransformPipeline* device, int index) {
    MatrixStack& stack = device->stacks[index];
    return stack.matrices[stack.count - 1].m;
}


void PushBack(std::vector<MapObject*>& v, MapObject* const& value) {
    v.push_back(value);
}

const int& g_multiplayerView = options::g_options.drawVisibleOnly;

// Pushes `matrix` on the pipeline's stack `index` (multiplied onto its top).
void PushStackMatrix(TransformPipeline* device, int index, const float* matrix) {
    MatrixStack& stack = device->stacks[index];
    if (stack.count == 0) {
        stack.count = 1;
        std::memcpy(stack.matrices[0].m, matrix, 0x40);
    } else {
        int depth = ++stack.count;
        stack.matrices[depth - 1].Multiply(stack.matrices[depth - 2].m, matrix);
    }
}

Object3D* AsObject3D(MapObject* object) { return object; }
Object3DClientSpecific* ModelOf(MapObject* object) {
    return AsObject3D(object)->ClientSpecific();
}

// Clamp used by the picking: negative or NaN becomes +0 (a branch-free select).
inline float NonNegative(float v) { return MaxKeepB(v, 0.0f); }

// The two draws share everything but the height function.
template <typename Height>
Vector3 PickPoint(WorldClient* world, int screenX, int screenY, Height height) {
    TransformPipeline* device = g_sceneRenderer.pipeline;
    if (device->stacks[TransformPipeline::kViewStack].count == 0) {
        return Vector3{0.0f, 0.0f, 0.0f};
    }
    PushStackMatrix(device, TransformPipeline::kPickStack, StackTop(device, TransformPipeline::kViewStack));
    const float* m = StackTop(g_sceneRenderer.pipeline, TransformPipeline::kPickStack);

    // The screen point: ints, exact as floats.
    float sx = static_cast<float>(screenX);
    float sy = static_cast<float>(screenY);
    float outX = 0.0f, outY = 0.0f, pz = 0.0f;
    float det = ((m[4] * m[13] - m[5] * m[12]) * sx + (m[1] * m[12] - m[0] * m[13]) * sy) + (m[0] * m[5] - m[4] * m[1]);
    // Skipped when zero.
    if (det != 0.0f) {
        // The screen ray as x = a z + b, y = c z + d.
        float a =
            (((m[13] * m[2] - m[1] * m[14]) * sy + (m[14] * m[5] - m[13] * m[6]) * sx) + (m[1] * m[6] - m[5] * m[2])) / det;
        float b =
            (((m[5] * m[15] - m[13] * m[7]) * sx + (m[3] * m[13] - m[1] * m[15]) * sy) + (m[1] * m[7] - m[3] * m[5])) / det;
        float c =
            -((((m[12] * m[2] - m[0] * m[14]) * sy + (m[4] * m[14] - m[6] * m[12]) * sx) + (m[0] * m[6] - m[4] * m[2])) / det);
        float d =
            -((((m[4] * m[15] - m[12] * m[7]) * sx + (m[3] * m[12] - m[0] * m[15]) * sy) + (m[0] * m[7] - m[3] * m[4])) / det);

        // Start halfway up the terrain's height range.
        float half = world->terrain->heightRange * 0.5f;
        float x = NonNegative(a * half + b);
        float y = NonNegative(c * half + d);
        pz = (height(x, y) + half) * 0.5f;

        // Fixed-point iteration on the height (up to 100 steps)...
        float previous;
        int steps = 0;
        bool converged = false;
        for (;;) {
            previous = pz;
            outX = NonNegative(a * previous + b);
            outY = NonNegative(c * previous + d);
            pz = static_cast<float>(height(outX, outY));
            float diff = std::fabs(pz - previous);
            ++steps;
            if (diff <= 0.01f) {
                converged = steps < 100;
                break;
            }
            if (steps >= 100) break;
        }
        // ...and when it did not settle, bisection between the last two heights.
        if (!converged) {
            steps = 0;
            do {
                previous = (pz + previous) * 0.5f;
                outX = NonNegative(a * previous + b);
                outY = NonNegative(c * previous + d);
                pz = static_cast<float>(height(outX, outY));
                float diff = std::fabs(pz - previous);
                ++steps;
                if (diff <= 0.01f) break;
            } while (steps < 100);
        }
    }
    device->PopMatrix(TransformPipeline::kPickStack);
    return {outX, outY, pz};
}

} // namespace

// ---- Visibility ----------------------------------------------------------------------------------

int WorldClient::CollectVisible(MapObject* object, const Matrix4* view, float animTime, std::uint32_t now, char animateUnits,
                                char withScripted) {
    if (!object || !AsObject3D(object)->attached) return 0;
    TransformPipeline* device = g_sceneRenderer.pipeline;
    int touched = 0;
    // Only a multiplayer spectator view (session kind 1) leaves units it cannot see out.
    bool drawAllUnits = true;
    if (g_multiplayerView != 0 && session->sessionKind == 1) drawAllUnits = false;

    bool process = true;
    if (object->GetClassId() == ClassId::Unit) {
        if (object->MapType() == MapObjectType::Hero && IsTextKey(object->id) && !withScripted) {
            process = false;
        } else if (animateUnits) {
            drawn::Observe(object);
            const drawn::Pose before = drawn::OwnPose(object);
            object->AdvanceTime(animTime);
            drawn::NotePlaced(object, before);
            if (!drawAllUnits && !(object->flags & MapObjectFlag::InParty)) process = false;
        } else {
            // First pass: units wait on the deferred list (which holds a reference).
            g_deferredUnits.emplace_back(object);
            process = false;
        }
    }
    if (process) {
        // Object -> world matrix: the rotation with the position as the last column, at the pose the draw shows.
        Object3D* o3d = AsObject3D(object);
        drawn::Observe(o3d);
        const drawn::Pose shown = drawn::Show(o3d);
        // An object's own light follows its drawn place (drawn::Carry; MapObjectClientSpecific::RunTick puts the
        // game's there at each tick).
        if (EngineObject* side = object->companion; side && side->GetClassId() == ClassId::MapObjectClientSpecific)
            if (Light* light = object->ClientCompanion()->light)
                drawn::Carry(light, shown.position + Vector3{0.0f, 0.0f, MapObjectClientSpecific::kLightHeight});
        float* matrix = o3d->worldMatrix.m;
        shown.rotation.ToMatrix(matrix);
        matrix[3] = shown.position.x;
        matrix[7] = shown.position.y;
        matrix[11] = shown.position.z;
        matrix[14] = 0.0f;
        matrix[13] = 0.0f;
        matrix[12] = 0.0f;
        matrix[15] = 1.0f;
        PushStackMatrix(device, TransformPipeline::kClipStack, matrix);
        // A figure (a unit or a lever, drawn with its own pose and weather) beyond the game's reach is out of view as
        // when the projection had its far plane there; the map's scenery is drawn to its edge
        // (TransformPipeline::BeyondReach).
        const ClassId kind = object->GetClassId();
        const bool figure = kind == ClassId::Unit || kind == ClassId::Lever;
        const bool inView = g_sceneRenderer.pipeline->ClipTest(&o3d->bounds) != TransformPipeline::ClipResult::Outside &&
                            !(figure && g_sceneRenderer.pipeline->BeyondReach(&o3d->bounds));
        device->PopMatrix(TransformPipeline::kClipStack);

        MapObject* const& entry = object;
        if (inView) {
            object->flags |= MapObjectFlag::Shown;
            // View-space position.
            o3d->worldPosition = TransformColumnNormal(*view, o3d->localPosition);
            switch (object->GetClassId()) {
            case ClassId::Unit:
                PushBack(FrameObjects(FrameList::VisibleUnits), entry);
                PushBack(FrameObjects(FrameList::ShadowUnits), entry);
                break;
            case ClassId::Tree:
                PushBack(FrameObjects(FrameList::VisibleTrees), entry);
                PushBack(FrameObjects(FrameList::ShadowTrees), entry);
                break;
            case ClassId::Building:
                PushBack(FrameObjects(FrameList::VisibleBuildings), entry);
                PushBack(FrameObjects(FrameList::ShadowBuildings), entry);
                break;
            case ClassId::Lever:
                PushBack(FrameObjects(FrameList::VisibleLevers), entry);
                PushBack(FrameObjects(FrameList::ShadowLevers), entry);
                break;
            default:
                PushBack(FrameObjects(FrameList::VisibleOther), entry);
                PushBack(FrameObjects(FrameList::ShadowOther), entry);
                break;
            }
            if ((object->flags & MapObjectFlag::Stationary) && now - ModelOf(object)->touchTime > static_cast<std::uint32_t>(options::kLightingPeriod)) {
                PushBack(FrameObjects(FrameList::TouchedModels), entry);
                ++touched;
            }
        } else {
            object->flags &= ~MapObjectFlag::Shown;
            // Out of view: still listed for shadows when its shadow volume is in view.
            if (ModelOf(object)->ShadowInView()) {
                if (o3d->animation) o3d->animation->UpdatePose();
                switch (object->GetClassId()) {
                case ClassId::Unit: PushBack(FrameObjects(FrameList::ShadowUnits), entry); break;
                case ClassId::Tree: PushBack(FrameObjects(FrameList::ShadowTrees), entry); break;
                case ClassId::Building: PushBack(FrameObjects(FrameList::ShadowBuildings), entry); break;
                case ClassId::Lever: PushBack(FrameObjects(FrameList::ShadowLevers), entry); break;
                default: PushBack(FrameObjects(FrameList::ShadowOther), entry); break;
                }
            }
        }
    }
    return touched;
}

// ---- Cursor picking ------------------------------------------------------------------------------

Vector3 WorldClient::PickGround(int x, int y) {
    return PickPoint(this, x, y, [this](float px, float py) {
        return terrain->HeightAt(px, py, terrain::Layer::Ground);
    });
}

Vector3 WorldClient::PickSurface(int x, int y) {
    return PickPoint(this, x, y, [this](float px, float py) {
        return GroundHeightAt(px, py);
    });
}

// ---- The frame's draw lists (frame_lists.h) ------------------------------------------------------

// Plain globals (constructed and destroyed by the C++ runtime).
std::vector<MapObject*> g_frameLists[static_cast<int>(FrameList::Count)];
std::vector<HeldRef<MapObject>> g_deferredUnits;
