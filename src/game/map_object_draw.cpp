// Drawing a 3D object on the client (Object3DClientSpecific::Draw). See map_object_shadow.h and
// docs/notes/map_objects.md ("Drawing").
#include "drawn_motion.h"
#include "engine_object.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "map_object_shadow.h"
#include "object.h"
#include "spell.h"                // Light, TheLightFactory
#include "terrain.h"
#include "texture.h"              // RenderDeviceObject
#include "world.h"                // WorldClient

#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/rect.h"
#include "platform/platform.h"

#include "lib/vecmath.h"

#include <bit>
#include <cstring>
#include "game_singletons.h"

namespace {
constexpr float kLightMargin = 10.0f;

// Does `light` reach `object`: its range + 10 squared at least the squared distance.
bool InReach(const Object3D* object, const Light* light) {
    const Vector3& at = light->localPosition;
    float reach = light->range + kLightMargin;
    float distanceSquared = LengthSquared(object->localPosition - at);
    return reach * reach >= distanceSquared;   // NaN does not reach
}

bool PartTooFar(const FigureMesh* part) {
    return LengthSquared(part->localPosition) > kPartsNearSquared;
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

int Object3DClientSpecific::Draw(SceneRenderer* context, Rect* dirty) {
    if (object->flags & MapObjectFlag::NotDrawn) return 1;
    bool anyDrawn = false;
    TransformPipeline* pipeline = context->pipeline;
    Vector3 at;
    at = object->localPosition;
    object->LoadFigures();
    if (!(object->flags & MapObjectFlag::Stationary)) object->flags &= ~MapObjectFlag::Static;
    if (dirty) {
        drawnRect = {};
        object->dirtyRect = {};
    }
    if (object->animation) object->animation->UpdatePose();

    // The lights within reach (the list starts with room for 16).
    std::vector<Light*> list;
    list.reserve(0x10);
    ObjectListIteratorOf<Light> lights;
    Light* light = lights.First(g_clientWorld.lightFactory);
    for (; light; light = lights.Next()) {
        if (context->sun == light) continue;
        if (!InReach(object, light)) continue;
        if (light->flags & LightFlag::Hidden) continue;
        if (light->flags & LightFlag::Moving) object->flags &= ~MapObjectFlag::Static;
        list.push_back(light);
    }

    // The object's matrix at the pose the draw shows (drawn_motion.h), and its extra scale when it has one.
    const drawn::Pose shown = drawn::PoseOf(object);
    ObjectWorldMatrix(&shown.rotation, shown.position, &object->worldMatrix);
    Matrix4 m = object->worldMatrix;
    pipeline->PushMatrix(m.m, TransformPipeline::kClipStack);
    pipeline->PushMatrix(m.m, TransformPipeline::kWorldStack);
    if (!(object->flags & MapObjectFlag::Static)) pipeline->PushMatrixProduct(m, TransformPipeline::kObjectStack);
    const Vector3& extraScale = figureScale;
    bool scaled = !(extraScale.x == 1.0f && extraScale.y == 1.0f && extraScale.z == 1.0f);
    if (scaled) {
        std::memset(m.m, 0, sizeof(m));
        m.m[0] = extraScale.x;
        m.m[5] = extraScale.y;
        m.m[10] = extraScale.z;
        m.m[15] = 1.0f;
        pipeline->PushMatrix(m.m, TransformPipeline::kClipStack);
        pipeline->PushMatrix(m.m, TransformPipeline::kWorldStack);
        if (!(object->flags & MapObjectFlag::Static)) pipeline->PushMatrixProduct(m, TransformPipeline::kObjectStack);
    }
    std::uint8_t lit = 0xFF;
    if (!(object->flags & MapObjectFlag::NoEdgeFade)) {
        terrain::Terrain* terrain = ClientWorld()->terrain;
        lit = terrain->EdgeFade(at);   // AL as it is
    }

    // The parts of both sets.
    bool allLit = true;
    const bool wasStatic = lib::Has(object->flags, MapObjectFlag::Static);
    Rect partRect;
    for (int k = 0; k < 2; ++k) {
        TextureParts* set = partSets[k];
        if (!set) continue;
        if (!context->SetTexture(0, set->texture)) {
            return game::FatalError("CObject3DClientSpecific::Draw.\n SetTexture failed.\n");
        }
        // The first set takes the wounds material as stage 0's texture.
        WoundsMaterial* wounds = woundsMaterial;
        g_sceneRenderer.device->SetMaterial(k == 0 ? wounds : nullptr);
        set = partSets[k];
        for (int g = 0; g < std::ssize(set->groups); ++g) {
            for (int p = 0; p < std::ssize(set->groups[g]->parts); ++p) {
                FigureMesh* part = set->groups[g]->parts[p];
                if (PartTooFar(part)) continue;
                if (!(object->flags & MapObjectFlag::Static)) part->flags &= ~FigureMeshFlag::Lit;
                bool pushed = !(part->flags & FigureMeshFlag::Lit);
                ObjectWorldMatrix(&part->localRotation, part->localPosition, &part->worldMatrix);
                m = part->worldMatrix;
                pipeline->PushMatrix(m.m, TransformPipeline::kClipStack);
                pipeline->PushMatrix(m.m, TransformPipeline::kWorldStack);
                if (pushed) pipeline->PushMatrixProduct(m, TransformPipeline::kObjectStack);
                TextureParts* current = partSets[k];
                int drawn = context->DrawFigureLit(part, &list, current->groups[g]->material, static_cast<int>(lit),
                                                   dirty ? &partRect : nullptr, nullptr, nullptr);
                if (!drawn) {
                    return game::FatalError("CObject3DClientSpecific::Draw.\n CFigure::Draw failed.\n");
                }
                anyDrawn = anyDrawn || lib::Has(part->flags, FigureMeshFlag::Drawn);
                if (dirty && !partRect.IsEmpty() && (part->flags & FigureMeshFlag::Drawn)) {
                    drawnRect = drawnRect.Union(partRect);
                }
                if (pushed) pipeline->PopMatrix(TransformPipeline::kObjectStack);
                pipeline->PopMatrix(TransformPipeline::kClipStack);
                pipeline->PopMatrix(TransformPipeline::kWorldStack);
                allLit = allLit && lib::Has(part->flags, FigureMeshFlag::Lit);
            }
        }
    }
    if (scaled) {
        if (!(object->flags & MapObjectFlag::Static)) pipeline->PopMatrix(TransformPipeline::kObjectStack);
        pipeline->PopMatrix(TransformPipeline::kClipStack);
        pipeline->PopMatrix(TransformPipeline::kWorldStack);
    }
    if (!(object->flags & MapObjectFlag::Static)) pipeline->PopMatrix(TransformPipeline::kObjectStack);
    pipeline->PopMatrix(TransformPipeline::kClipStack);
    pipeline->PopMatrix(TransformPipeline::kWorldStack);

    if (allLit) {
        object->flags |= MapObjectFlag::Static;
        if (!wasStatic) {
            touchTime = platform::Milliseconds();
            flags |= kHalfRedraw;
        }
    } else {
        object->flags &= ~MapObjectFlag::Static;
    }
    if (dirty) {
        object->dirtyRect = drawnRect;
        *dirty = object->dirtyRect;
    }
    if (anyDrawn) object->flags |= MapObjectFlag::Shown;
    else object->flags &= ~MapObjectFlag::Shown;
    g_sceneRenderer.device->SetMaterial(nullptr);
    return 1;
}
