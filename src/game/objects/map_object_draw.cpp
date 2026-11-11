// Drawing a 3D object on the client (Object3DClientSpecific::Draw). See map_object_shadow.h and
// docs/notes/map_objects.md ("Drawing").
#include "game/scene/drawn_motion.h"
#include "game/scene/device.h"
#include "game/core/engine_object.h"
#include "game/models/link_figure.h"
#include "game/objects/map_object.h"
#include "game/objects/object3d.h"
#include "game/objects/map_object_shadow.h"
#include "game/world/hoover.h"
#include "game/scene/light.h"
#include "game/terrain/terrain.h"
#include "game/scene/scene_renderer.h"
#include "game/scene/pipeline.h"
#include "game/world/world.h"                // WorldClient
#include "game/data/options.h"
#include "render/render.h"

#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/rect.h"
#include "lib/rounding.h"

#include "lib/vecmath.h"

#include <cfloat>
#include <cstring>
#include "game/app/singletons.h"

namespace {
bool PartTooFar(const FigureMesh* part) {
    return LengthSquared(part->localPosition) > kPartsNearSquared;
}

// The screen rectangle of a part's box (its cube without one) placed by `screen` (the screen stack's top times the
// part's model): the bounds of its corners in front of the eye and inside the depth range, clamped to `clamp` and
// rounded to nearest; empty when no corner counts. Past a corner of the screen this can be larger than the part
// drawn there (renderer.md section 3, "Picking").
Rect BoxRect(const Matrix4& screen, const ClipBounds& bounds, const Rect& clamp) {
    const bool box = (bounds.flags & static_cast<std::uint32_t>(BoundsFlag::Box)) != 0;
    const float r = bounds.radius;
    const Vector3 low = box ? bounds.boxMin : Vector3{-r, -r, -r}, high = box ? bounds.boxMax : Vector3{r, r, r};
    float minX = FLT_MAX, minY = FLT_MAX, maxX = -FLT_MAX, maxY = -FLT_MAX;
    for (int k = 0; k < 8; ++k) {
        const Vector3 corner =
            bounds.center + Vector3{(k & 1) ? high.x : low.x, (k & 2) ? high.y : low.y, (k & 4) ? high.z : low.z};
        const Vector4 p = ToClip(screen, corner);   // the screen's x and y times w, the clip space's z and w
        if (!(p.z >= 0.0f && p.w - p.z >= 0.0f && p.w > 0.0f)) continue;
        const float x = p.x / p.w, y = p.y / p.w;
        if (x < minX) minX = x;   // a NaN never replaces a bound
        if (maxX < x) maxX = x;
        if (y < minY) minY = y;
        if (maxY < y) maxY = y;
    }
    const float left = static_cast<float>(clamp.left), top = static_cast<float>(clamp.top);
    const float right = static_cast<float>(clamp.right), bottom = static_cast<float>(clamp.bottom);
    minX = minX < left ? left : minX;
    minY = minY < top ? top : minY;
    maxX = right < maxX ? right : maxX;
    maxY = bottom < maxY ? bottom : maxY;
    return Rect{lib::RoundEven(minX), lib::RoundEven(minY), lib::RoundEven(maxX), lib::RoundEven(maxY)};
}
} // namespace

void ObjectWorldMatrix(const Quaternion* orientation, const Vector3& position, Matrix4* out) {
    orientation->ToMatrix(out->m);
    out->m[11] = position.z;
    out->m[3] = position.x;
    out->m[7] = position.y;
    std::memset(&out->m[12], 0, 3 * sizeof(float));
    out->m[15] = 1.0f;
}

// The object is in view (the caller's one test of its bounds): every part near enough is drawn whole and the GPU clips
// it. The units' and levers' draws (`dirty`) also give each part its screen rectangle from its box.
int Object3DClientSpecific::DrawParts(SceneRenderer* context, Rect* dirty) {
    if (object->flags & MapObjectFlag::NotDrawn) return 1;
    TextureDevice* device = context->device;
    object->LoadFigures();
    if (dirty) {
        drawnRect = {};
        object->dirtyRect = {};
    }
    if (object->animation) object->animation->PlaceForDraw();

    // The scene's sun and ambient colour: the parts are lit on the GPU (the point lights there too).
    render::MeshLighting lighting{};
    if (const Light* sun = g_sceneRenderer.sun) {
        lighting.sun = true;
        lighting.sunDirection = sun->direction;
        for (int k = 0; k < 3; ++k) lighting.sunColour[k] = sun->color[k];
    }
    const float* ambient = AmbientColour();
    for (int k = 0; k < 3; ++k) lighting.ambientColour[k] = ambient[k];
    lighting.coefficient = options::kObjectsLightingCoeff;

    // The object's matrix at the pose the draw shows (drawn_motion.h), times its extra scale when it has one, under the
    // screen stack's top (the camera with the viewport).
    const drawn::Pose shown = drawn::PoseOf(object);
    ObjectWorldMatrix(&shown.rotation, shown.position, &object->worldMatrix);
    Matrix4 placement = object->worldMatrix;
    const Vector3& scale = figureScale;
    if (!(scale.x == 1.0f && scale.y == 1.0f && scale.z == 1.0f))
        for (int row = 0; row < 4; ++row) {
            placement.m[row * 4] *= scale.x;
            placement.m[row * 4 + 1] *= scale.y;
            placement.m[row * 4 + 2] *= scale.z;
        }
    const Matrix4& view = *context->pipeline->Top(TransformPipeline::kWorldStack);
    if (dirty) originDepth = TransformColumnW(view, shown.position);
    std::uint8_t fade = 0xFF;
    if (!(object->flags & MapObjectFlag::NoEdgeFade)) fade = ClientWorld()->terrain->EdgeFade(object->localPosition);

    // The parts of both sets; the object is static when every part near enough was drawn lit on the GPU (none hidden,
    // none flat).
    bool anyDrawn = false, allLit = true;
    for (int k = 0; k < 2; ++k) {
        TextureParts* set = partSets[k].get();
        if (!set) continue;
        context->SetTexture(0, set->texture);
        // The first set takes the wounds material as stage 0's texture.
        WoundsMaterial* wounds = woundsMaterial;
        device->SetMaterial(k == 0 ? wounds : nullptr);
        for (int g = 0; g < std::ssize(set->groups); ++g) {
            for (int p = 0; p < std::ssize(set->groups[g]->parts); ++p) {
                FigureMesh* part = set->groups[g]->parts[p];
                if (PartTooFar(part)) continue;
                part->flags &= ~FigureMeshFlag::Drawn;
                allLit = allLit && !(part->flags & (FigureMeshFlag::Hidden | FigureMeshFlag::Flat));
                if (part->flags & FigureMeshFlag::Hidden) continue;
                Matrix4 local, model;
                ObjectWorldMatrix(&part->localRotation, part->localPosition, &local);
                model.Multiply(placement.m, local.m);
                FigureMeshDrawer* drawer = part->drawer;
                const bool drawn = drawer->DrawLit(device, view, model, lighting, set->groups[g]->material, fade);
                if (drawn) part->flags |= FigureMeshFlag::Drawn;
                anyDrawn = anyDrawn || drawn;
                if (!dirty) continue;
                drawer->screen = {};
                if (!drawn) continue;
                drawer->screenMatrix.Multiply(view.m, model.m);
                drawer->screen = BoxRect(drawer->screenMatrix, part->box.bounds, device->screen);
                if (!drawer->screen.IsEmpty()) drawnRect = drawnRect.Union(drawer->screen);
            }
        }
    }

    if (allLit) object->flags |= MapObjectFlag::Static;
    else object->flags &= ~MapObjectFlag::Static;
    if (dirty) {
        object->dirtyRect = drawnRect;
        *dirty = object->dirtyRect;
    }
    if (anyDrawn) object->flags |= MapObjectFlag::Shown;
    else object->flags &= ~MapObjectFlag::Shown;
    device->SetMaterial(nullptr);
    return 1;
}
