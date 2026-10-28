// CWorldClient per-frame view code: filing visible objects into the frame's draw lists, sorting
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

constexpr int kTypeHero = 0x32;           // CEngineObject::type_ of heroes
constexpr std::uint32_t kFlagVisible = 0x20;
constexpr std::uint32_t kFlagTouchModel = 0x200;
constexpr std::uint32_t kFlagAlwaysDrawn = 0x400000;

// The scene's transform pipeline: the picking stack, the clip stack an object's matrix is pushed on to classify it,
// and the view stack.
float* StackTop(CTransformPipeline* device, int index) {
    MatrixStack& stack = device->stacks_[index];
    return stack.matrices[stack.count - 1].m;
}


void PushBack(std::vector<CMapObject*>& v, CMapObject* const& value) {
    v.push_back(value);
}

const int& g_multiplayerView = options::g_options->drawVisibleOnly_;
const int& g_modelTouchDelay = options::g_options->shadowLightingFreq_;

// Pushes `matrix` on the pipeline's stack `index` (multiplied onto its top).
void PushStackMatrix(CTransformPipeline* device, int index, const float* matrix) {
    MatrixStack& stack = device->stacks_[index];
    if (stack.count == 0) {
        stack.count = 1;
        std::memcpy(stack.matrices[0].m, matrix, 0x40);
    } else {
        int depth = ++stack.count;
        stack.matrices[depth - 1].Multiply(stack.matrices[depth - 2].m, matrix);
    }
}

CObject3D* Object3D(CMapObject* object) { return object; }
CObject3DClientSpecific* ModelOf(CMapObject* object) {
    return Object3D(object)->ClientSpecific();
}

// Clamp used by the picking: negative or NaN becomes +0 (a branch-free select).
__forceinline float NonNegative(float v) { return v >= 0.0f ? v : 0.0f; }

// The two draws share everything but the height function (which returns its result unrounded).
template <typename Height>
Vector3* PickPoint(CWorldClient* world, Vector3* out, int screenX, int screenY, Height height) {
    CTransformPipeline* device = g_sceneRenderer->pipeline_;
    if (device->stacks_[CTransformPipeline::kViewStack].count == 0) {
        std::memcpy(out, &worlddata::kOrigin, 12);
        return out;
    }
    PushStackMatrix(device, CTransformPipeline::kPickStack, StackTop(device, CTransformPipeline::kViewStack));
    const float* m = StackTop(g_sceneRenderer->pipeline_, CTransformPipeline::kPickStack);

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
        float half = static_cast<float>(world->terrain_->heightRange * 0.5f);
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
    device->PopMatrix(CTransformPipeline::kPickStack);
    out->x = outX;
    std::memcpy(&out->y, &outY, 4);
    std::memcpy(&out->z, &pz, 4);
    return out;
}

} // namespace

// ---- Visibility ----------------------------------------------------------------------------------

int CWorldClient::CollectVisible(std::list<HeldPtr<CMapObject>>* list, const Matrix4* view, float animTime, std::uint32_t now,
                                 char animateUnits, char withScripted) {
    CTransformPipeline* device = g_sceneRenderer->pipeline_;
    int touched = 0;
    // Only a multiplayer spectator view (session kind 1) leaves units it cannot see out.
    bool drawAllUnits = true;
    if (g_multiplayerView != 0 && session_->sessionKind_ == 1) drawAllUnits = false;

    for (CMapObject* object : *list) {
        if (object && Object3D(object)->attached) {
            bool process = true;
            if (object->GetClassId() == world::kClassUnit) {
                if (object->type_ == kTypeHero && (IsTextKey(object->id_) & 0xFF) && !withScripted) {
                    process = false;
                } else if (animateUnits) {
                    object->AdvanceTime(animTime);
                    if (!drawAllUnits && !(object->flags_ & kFlagAlwaysDrawn)) process = false;
                } else {
                    // First pass: units wait on the deferred list (which holds a reference).
                    g_deferredUnits->emplace_back(object);
                    process = false;
                }
            }
            if (process) {
                // Object -> world matrix: the rotation with the position as the last column.
                CObject3D* o3d = Object3D(object);
                float* matrix = o3d->worldMatrix.m;
                static_cast<const Quaternion*>(&o3d->localRotation_)->ToMatrix(matrix);
                std::memcpy(&matrix[3], &o3d->localPosition_.x, 4);
                std::memcpy(&matrix[7], &o3d->localPosition_.y, 4);
                std::memcpy(&matrix[11], &o3d->localPosition_.z, 4);
                const std::uint32_t zero = 0;
                const std::uint32_t one = 0x3F800000;   // 1.0f
                std::memcpy(&matrix[14], &zero, 4);
                std::memcpy(&matrix[13], &zero, 4);
                std::memcpy(&matrix[12], &zero, 4);
                std::memcpy(&matrix[15], &one, 4);
                PushStackMatrix(device, CTransformPipeline::kClipStack, matrix);
                int where = g_sceneRenderer->pipeline_->ClipTest(reinterpret_cast<const ClipBounds*>(&o3d->bounds.flags));
                device->PopMatrix(CTransformPipeline::kClipStack);

                CMapObject* const& entry = object;
                if (where != 2) {
                    object->flags_ |= kFlagVisible;
                    // View-space position: x summed ZYX, y and z XYZ, each rounded.
                    o3d->worldPosition_ = TransformColumnNormal<Sum3::ZYX, Sum3::XYZ>(*view, o3d->localPosition_);
                    switch (object->GetClassId()) {
                    case 0x50:
                        PushBack(g_visibleUnits, entry);
                        PushBack(g_shadowUnits, entry);
                        break;
                    case 0x52:
                        PushBack(g_visible52, entry);
                        PushBack(g_shadow52, entry);
                        break;
                    case 0x54:
                        PushBack(g_visible54, entry);
                        PushBack(g_shadow54, entry);
                        break;
                    case 0x5D:
                        PushBack(g_visible5D, entry);
                        PushBack(g_shadow5D, entry);
                        break;
                    default:
                        PushBack(g_visibleOther, entry);
                        PushBack(g_shadowOther, entry);
                        break;
                    }
                    if ((object->flags_ & kFlagTouchModel) && now - ModelOf(object)->touchTime > static_cast<std::uint32_t>(g_modelTouchDelay)) {
                        PushBack(g_touchedModels, entry);
                        ++touched;
                    }
                } else {
                    object->flags_ &= ~kFlagVisible;
                    // Out of view: still listed for shadows when its shadow volume is in view.
                    if (ModelOf(object)->ShadowInView()) {
                        if (o3d->animation) o3d->animation->UpdatePose();
                        switch (object->GetClassId()) {
                        case 0x50: PushBack(g_shadowUnits, entry); break;
                        case 0x52: PushBack(g_shadow52, entry); break;
                        case 0x54: PushBack(g_shadow54, entry); break;
                        case 0x5D: PushBack(g_shadow5D, entry); break;
                        default: PushBack(g_shadowOther, entry); break;
                        }
                    }
                }
            }
        }
    }
    return touched;
}

// ---- Cursor picking ------------------------------------------------------------------------------

Vector3* CWorldClient::PickGround(Vector3* out, int x, int y) {
    return PickPoint(this, out, x, y, [this](float px, float py) {
        return terrain_->HeightAt(px, py, 0);
    });
}

Vector3* CWorldClient::PickSurface(Vector3* out, int x, int y) {
    return PickPoint(this, out, x, y, [this](float px, float py) {
        return GroundHeightAt(px, py);
    });
}

// ---- The frame's draw lists (world.h) ------------------------------------------------------------

// Empty at the start, as the static initialisers leave them (in the list's order: the last list
// first); the exit functions they register free their storage (lib::FreeStorage; docs/notes/standalone.md,
// "The static initialisers' objects").
constinit std::vector<CMapObject*> g_touchedModels = /*007AF508*/ {};
constinit std::vector<CMapObject*> g_shadow52 = /*007AF518*/ {};
constinit std::vector<CMapObject*> g_shadowUnits = /*007AF528*/ {};
constinit std::vector<CMapObject*> g_shadow5D = /*007AF538*/ {};
constinit std::vector<CMapObject*> g_shadowOther = /*007AF548*/ {};
constinit std::vector<CMapObject*> g_shadow54 = /*007AF558*/ {};
constinit std::vector<CMapObject*> g_visible52 = /*007AF568*/ {};
constinit std::vector<CMapObject*> g_visibleUnits = /*007AF578*/ {};
constinit std::vector<CMapObject*> g_visible5D = /*007AF588*/ {};
constinit std::vector<CMapObject*> g_visibleOther = /*007AF598*/ {};
constinit std::vector<CMapObject*> g_visible54 = /*007AF5A8*/ {};

void FreeTouchedModels() { lib::FreeStorage(g_touchedModels); }
void FreeShadow52() { lib::FreeStorage(g_shadow52); }
void FreeShadowUnits() { lib::FreeStorage(g_shadowUnits); }
void FreeShadow5D() { lib::FreeStorage(g_shadow5D); }
void FreeShadowOther() { lib::FreeStorage(g_shadowOther); }
void FreeShadow54() { lib::FreeStorage(g_shadow54); }
void FreeVisible52() { lib::FreeStorage(g_visible52); }
void FreeVisibleUnits() { lib::FreeStorage(g_visibleUnits); }
void FreeVisible5D() { lib::FreeStorage(g_visible5D); }
void FreeVisibleOther() { lib::FreeStorage(g_visibleOther); }
void FreeVisible54() { lib::FreeStorage(g_visible54); }
