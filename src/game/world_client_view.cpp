// WorldClient per-frame view code: filing visible objects into the frame's draw lists, sorting
// those lists, and picking the ground point under the cursor. See world.h and docs/notes/world.md.
#include "model.h"
#include "options.h"
#include "world.h"

#include "core_maths.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "map_object_shadow.h"
#include "object_tables.h"
#include "lib/containers.h"
#include "terrain.h"

#include <cmath>
#include <cstring>
#include <memory>
#include <vector>
#include "lib/nan_audit.h"
#include "lib/vecmath.h"
#include "world_data.h"

namespace {


// The scene's transform pipeline: the picking stack, the clip stack an object's matrix is pushed on to classify it,
// and the view stack.
float* StackTop(TransformPipeline* device, int index) {
    MatrixStack& stack = device->stacks[index];
    return stack.matrices[stack.count - 1].m;
}


void PushBack(std::vector<CMapObject*>& v, CMapObject* const& value) {
    v.push_back(value);
}

const int& g_multiplayerView = options::g_options.drawVisibleOnly;
const int& g_modelTouchDelay = options::g_options.shadowLightingFreq;

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

CObject3D* Object3D(CMapObject* object) { return object; }
Object3DClientSpecific* ModelOf(CMapObject* object) {
    return Object3D(object)->ClientSpecific();
}

// Clamp used by the picking: negative or NaN becomes +0 (a branch-free select).
__forceinline float NonNegative(float v) { return v >= 0.0f ? v : 0.0f; }

// The two draws share everything but the height function (which returns its result unrounded).
template <typename Height>
Vector3* PickPoint(WorldClient* world, Vector3* out, int screenX, int screenY, Height height) {
    TransformPipeline* device = g_sceneRenderer.pipeline;
    if (device->stacks[TransformPipeline::kViewStack].count == 0) {
        std::memcpy(out, &worlddata::kOrigin, 12);
        return out;
    }
    PushStackMatrix(device, TransformPipeline::kPickStack, StackTop(device, TransformPipeline::kViewStack));
    const float* m = StackTop(g_sceneRenderer.pipeline, TransformPipeline::kPickStack);

    // The values kept on the FPU stack are doubles here (no rounding to float).
    double sx = screenX;
    double sy = screenY;
    float outX = 0.0f, outY = 0.0f, pz = 0.0f;
    double det = ((m[4] * m[13] - m[5] * m[12]) * sx + (m[1] * m[12] - m[0] * m[13]) * sy) + (m[0] * m[5] - m[4] * m[1]);
    // Skipped when zero.
    EI_NAN_AUDIT(det);
    if (det != 0.0) {
        // The screen ray as x = a z + b, y = c z + d.
        float a = static_cast<float>(
            (((m[13] * m[2] - m[1] * m[14]) * sy + (m[14] * m[5] - m[13] * m[6]) * sx) + (m[1] * m[6] - m[5] * m[2])) / det);
        float b = static_cast<float>(
            (((m[5] * m[15] - m[13] * m[7]) * sx + (m[3] * m[13] - m[1] * m[15]) * sy) + (m[1] * m[7] - m[3] * m[5])) / det);
        float c = static_cast<float>(
            -((((m[12] * m[2] - m[0] * m[14]) * sy + (m[4] * m[14] - m[6] * m[12]) * sx) + (m[0] * m[6] - m[4] * m[2])) / det));
        float d = static_cast<float>(
            -((((m[4] * m[15] - m[12] * m[7]) * sx + (m[3] * m[12] - m[0] * m[15]) * sy) + (m[0] * m[7] - m[3] * m[4])) / det));

        // Start halfway up the terrain's height range.
        float half = static_cast<float>(world->terrain->heightRange * 0.5f);
        float x = NonNegative(static_cast<float>(a * half + b));
        float y = NonNegative(static_cast<float>(c * half + d));
        pz = static_cast<float>((height(x, y) + half) * 0.5f);

        // Fixed-point iteration on the height (up to 100 steps)...
        float previous;
        int steps = 0;
        bool converged = false;
        for (;;) {
            previous = pz;
            outX = NonNegative(static_cast<float>(a * previous + b));
            outY = NonNegative(static_cast<float>(c * previous + d));
            pz = static_cast<float>(height(outX, outY));
            float diff = std::fabs(static_cast<float>(pz - previous));
            ++steps;
            EI_NAN_AUDIT(diff);
            if (diff <= 0.01) {
                converged = steps < 100;
                break;
            }
            if (steps >= 100) break;
        }
        // ...and when it did not settle, bisection between the last two heights.
        if (!converged) {
            steps = 0;
            do {
                previous = static_cast<float>((pz + previous) * 0.5f);
                outX = NonNegative(static_cast<float>(a * previous + b));
                outY = NonNegative(static_cast<float>(c * previous + d));
                pz = static_cast<float>(height(outX, outY));
                float diff = std::fabs(static_cast<float>(pz - previous));
                ++steps;
                EI_NAN_AUDIT(diff);
                if (diff <= 0.01) break;
            } while (steps < 100);
        }
    }
    device->PopMatrix(TransformPipeline::kPickStack);
    out->x = outX;
    out->y = outY;
    out->z = pz;
    return out;
}

} // namespace

// ---- Visibility ----------------------------------------------------------------------------------

int WorldClient::CollectVisible(CMapObject* object, const Matrix4* view, float animTime, std::uint32_t now, char animateUnits,
                                char withScripted) {
    if (!object || !Object3D(object)->attached) return 0;
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
            object->AdvanceTime(animTime);
            if (!drawAllUnits && !(object->flags & MapObjectFlag::InParty)) process = false;
        } else {
            // First pass: units wait on the deferred list (which holds a reference).
            g_deferredUnits.emplace_back(object);
            process = false;
        }
    }
    if (process) {
        // Object -> world matrix: the rotation with the position as the last column.
        CObject3D* o3d = Object3D(object);
        float* matrix = o3d->worldMatrix.m;
        static_cast<const Quaternion*>(&o3d->localRotation)->ToMatrix(matrix);
        matrix[3] = o3d->localPosition.x;
        matrix[7] = o3d->localPosition.y;
        matrix[11] = o3d->localPosition.z;
        matrix[14] = 0.0f;
        matrix[13] = 0.0f;
        matrix[12] = 0.0f;
        matrix[15] = 1.0f;
        PushStackMatrix(device, TransformPipeline::kClipStack, matrix);
        int where = g_sceneRenderer.pipeline->ClipTest(reinterpret_cast<const ClipBounds*>(&o3d->bounds.flags));
        device->PopMatrix(TransformPipeline::kClipStack);

        CMapObject* const& entry = object;
        if (where != 2) {
            object->flags |= MapObjectFlag::Shown;
            // View-space position: x summed ZYX, y and z XYZ, each rounded.
            o3d->worldPosition = TransformColumnNormal<Sum3::ZYX, Sum3::XYZ>(*view, o3d->localPosition);
            switch (object->GetClassId()) {
            case ClassId::Unit:
                PushBack(g_visibleUnits, entry);
                PushBack(g_shadowUnits, entry);
                break;
            case ClassId::Tree:
                PushBack(g_visibleTrees, entry);
                PushBack(g_shadowTrees, entry);
                break;
            case ClassId::Building:
                PushBack(g_visibleBuildings, entry);
                PushBack(g_shadowBuildings, entry);
                break;
            case ClassId::Lever:
                PushBack(g_visibleLevers, entry);
                PushBack(g_shadowLevers, entry);
                break;
            default:
                PushBack(g_visibleOther, entry);
                PushBack(g_shadowOther, entry);
                break;
            }
            if ((object->flags & MapObjectFlag::Stationary) && now - ModelOf(object)->touchTime > static_cast<std::uint32_t>(g_modelTouchDelay)) {
                PushBack(g_touchedModels, entry);
                ++touched;
            }
        } else {
            object->flags &= ~MapObjectFlag::Shown;
            // Out of view: still listed for shadows when its shadow volume is in view.
            if (ModelOf(object)->ShadowInView()) {
                if (o3d->animation) o3d->animation->UpdatePose();
                switch (object->GetClassId()) {
                case ClassId::Unit: PushBack(g_shadowUnits, entry); break;
                case ClassId::Tree: PushBack(g_shadowTrees, entry); break;
                case ClassId::Building: PushBack(g_shadowBuildings, entry); break;
                case ClassId::Lever: PushBack(g_shadowLevers, entry); break;
                default: PushBack(g_shadowOther, entry); break;
                }
            }
        }
    }
    return touched;
}

// ---- Cursor picking ------------------------------------------------------------------------------

Vector3* WorldClient::PickGround(Vector3* out, int x, int y) {
    return PickPoint(this, out, x, y, [this](float px, float py) {
        return terrain->HeightAt(px, py, terrain::Layer::Ground);
    });
}

Vector3* WorldClient::PickSurface(Vector3* out, int x, int y) {
    return PickPoint(this, out, x, y, [this](float px, float py) {
        return GroundHeightAt(px, py);
    });
}

// ---- The frame's draw lists (world.h) ------------------------------------------------------------

// Plain globals (constructed and destroyed by the C++ runtime).
std::vector<CMapObject*> g_touchedModels;
std::vector<CMapObject*> g_shadowTrees;
std::vector<CMapObject*> g_shadowUnits;
std::vector<CMapObject*> g_shadowLevers;
std::vector<CMapObject*> g_shadowOther;
std::vector<CMapObject*> g_shadowBuildings;
std::vector<CMapObject*> g_visibleTrees;
std::vector<CMapObject*> g_visibleUnits;
std::vector<CMapObject*> g_visibleLevers;
std::vector<CMapObject*> g_visibleOther;
std::vector<CMapObject*> g_visibleBuildings;
std::vector<HeldRef<CMapObject>> g_deferredUnits;
