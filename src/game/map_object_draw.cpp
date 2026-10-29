// Drawing a 3D object on the client (CObject3DClientSpecific::Draw). See map_object_shadow.h and
// docs/notes/map_objects.md ("Drawing").
#include "engine_object.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "map_object_shadow.h"
#include "object.h"
#include "spell.h"                // CLight, TheLightFactory
#include "terrain.h"
#include "texture.h"              // RenderDeviceObject
#include "world.h"                // CWorldClient

#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/gameapi.h"

#include "lib/vecmath.h"

#include <bit>
#include <cstring>
#include "game_singletons.h"

// The light range test keeps its sums on the FPU stack like the original.

namespace {
constexpr std::uint32_t kRedrawStatic = 8;          // this->flags_
constexpr float kNearSquared = 1e9f;
constexpr float kLightMargin = 10.0f;
constexpr std::uint32_t kOne = 0x3F800000;
constexpr int kStackWorld = 1;                      // the pipeline's matrix stacks
constexpr int kStackView = 2;
constexpr int kStackObject = 3;

// Does `light` reach `object`: its range + 10 squared at least the squared distance.
bool InReach(const CObject3D* object, const CLight* light) {
    const Vector3& at = light->localPosition_;
    float dx = static_cast<float>(static_cast<double>(object->localPosition_.x) - at.x);
    float dy = static_cast<float>(static_cast<double>(object->localPosition_.y) - at.y);
    float dz = static_cast<float>(static_cast<double>(object->localPosition_.z) - at.z);
    double reach = static_cast<double>(light->range_) + kLightMargin;
    double distanceSquared = LengthSquaredUnrounded<Sum3::ZXY, double>(dx, dy, dz);
    return reach * reach >= distanceSquared;   // NaN does not reach
}

bool PartTooFar(const CFigureMesh* part) {
    return LengthSquaredUnrounded<Sum3::XYZ, double>(part->localPosition_) > kNearSquared;
}

// The end of a draw: the iterator's reference to the light list and the light list itself.
void Finish(ObjectListIterator& lights, std::vector<CLight*>& list) {
    if (lights.manager) {
        lights.manager->ForgetReference();
        lights.manager = nullptr;
    }
    lib::FreeStorage(list);
}
} // namespace

void ObjectWorldMatrix(const Quaternion* orientation, const Vector3* position, Matrix4* out) {
    orientation->ToMatrix(out->m);
    out->m[11] = position->z;
    out->m[3] = position->x;
    out->m[7] = position->y;
    std::memset(&out->m[12], 0, 3 * sizeof(float));
    out->m[15] = std::bit_cast<float>(kOne);
}

int CObject3DClientSpecific::Draw(CSceneRenderer* context, tagRECT* dirty) {
    if (object->flags_ & MapObjectFlag::NotDrawn) return 1;
    bool anyDrawn = false;
    CTransformPipeline* pipeline = context->pipeline_;
    Vector3 at;
    at = object->localPosition_;
    object->LoadFigures();
    if (!(object->flags_ & MapObjectFlag::Stationary)) object->flags_ &= ~MapObjectFlag::Static;
    if (dirty) {
        gameapi::SetRectEmpty(&drawnRect);
        gameapi::SetRectEmpty(&object->dirtyRect);
    }
    if (object->animation) object->animation->UpdatePose();

    // The lights within reach (the list starts with room for 16).
    std::vector<CLight*> list;
    list.reserve(0x10);
    ObjectListIteratorOf<CLight> lights{};
    CLight* light = lights.First(g_clientWorld.lightFactory_);
    for (; light; light = lights.Next()) {
        if (context->sun_ == light) continue;
        if (!InReach(object, light)) continue;
        if (light->flags_ & LightFlag::Hidden) continue;
        if (light->flags_ & LightFlag::Moving) object->flags_ &= ~MapObjectFlag::Static;
        list.push_back(light);
    }

    // The object's matrix, and its extra scale when it has one.
    ObjectWorldMatrix(&object->localRotation_, &object->localPosition_, &object->worldMatrix);
    Matrix4 m = object->worldMatrix;
    pipeline->PushMatrix(m.m, kStackView);
    pipeline->PushMatrix(m.m, kStackWorld);
    if (!(object->flags_ & MapObjectFlag::Static)) pipeline->PushMatrixProduct(m, kStackObject);
    const Vector3& extraScale = figureScale;
    bool scaled = !(std::bit_cast<std::uint32_t>(extraScale.x) == kOne &&
                    std::bit_cast<std::uint32_t>(extraScale.y) == kOne &&
                    std::bit_cast<std::uint32_t>(extraScale.z) == kOne);
    if (scaled) {
        std::memset(m.m, 0, sizeof(m));
        m.m[0] = extraScale.x;
        m.m[5] = extraScale.y;
        m.m[10] = extraScale.z;
        m.m[15] = std::bit_cast<float>(kOne);
        pipeline->PushMatrix(m.m, kStackView);
        pipeline->PushMatrix(m.m, kStackWorld);
        if (!(object->flags_ & MapObjectFlag::Static)) pipeline->PushMatrixProduct(m, kStackObject);
    }
    std::uint8_t lit = 0xFF;
    if (!(object->flags_ & MapObjectFlag::NoEdgeFade)) {
        Terrain* terrain = ClientWorld()->terrain_;
        lit = terrain->EdgeFade(&at);   // AL as it is
    }

    // The parts of both sets.
    bool allLit = true;
    const bool wasStatic = lib::Has(object->flags_, MapObjectFlag::Static);
    RECT partRect;
    for (int k = 0; k < 2; ++k) {
        TextureParts* set = partSets[k];
        if (!set) continue;
        if (!context->SetTexture(0, set->texture)) {
            int result = game::FatalError("CObject3DClientSpecific::Draw.\n SetTexture failed.\n");
            Finish(lights, list);
            return result;
        }
        // The first set takes the wounds material as stage 0's texture.
        WoundsMaterial* wounds = woundsMaterial;
        g_sceneRenderer->device_->SetMaterial(k == 0 ? wounds : nullptr);
        set = partSets[k];
        for (int g = 0; g < std::ssize(set->groups); ++g) {
            for (int p = 0; p < std::ssize(set->groups[g]->parts); ++p) {
                CFigureMesh* part = set->groups[g]->parts[p];
                if (PartTooFar(part)) continue;
                if (!(object->flags_ & MapObjectFlag::Static)) part->flags_ &= ~FigureMeshFlag::Lit;
                bool pushed = !(part->flags_ & FigureMeshFlag::Lit);
                ObjectWorldMatrix(&part->localRotation_, &part->localPosition_, &part->worldMatrix_);
                m = part->worldMatrix_;
                pipeline->PushMatrix(m.m, kStackView);
                pipeline->PushMatrix(m.m, kStackWorld);
                if (pushed) pipeline->PushMatrixProduct(m, kStackObject);
                TextureParts* current = partSets[k];
                int drawn = context->DrawFigureLit(part, &list, current->groups[g]->material, static_cast<int>(lit),
                                                   dirty ? &partRect : nullptr, nullptr, nullptr);
                if (!drawn) {
                    int result = game::FatalError("CObject3DClientSpecific::Draw.\n CFigure::Draw failed.\n");
                    Finish(lights, list);
                    return result;
                }
                anyDrawn = anyDrawn || lib::Has(part->flags_, FigureMeshFlag::Drawn);
                if (dirty && !gameapi::IsRectEmpty(&partRect) && (part->flags_ & FigureMeshFlag::Drawn)) {
                    RECT* total = &drawnRect;
                    gameapi::UnionRect(total, total, &partRect);
                }
                if (pushed) pipeline->PopMatrix(kStackObject);
                pipeline->PopMatrix(kStackView);
                pipeline->PopMatrix(kStackWorld);
                allLit = allLit && lib::Has(part->flags_, FigureMeshFlag::Lit);
            }
        }
    }
    if (scaled) {
        if (!(object->flags_ & MapObjectFlag::Static)) pipeline->PopMatrix(kStackObject);
        pipeline->PopMatrix(kStackView);
        pipeline->PopMatrix(kStackWorld);
    }
    if (!(object->flags_ & MapObjectFlag::Static)) pipeline->PopMatrix(kStackObject);
    pipeline->PopMatrix(kStackView);
    pipeline->PopMatrix(kStackWorld);

    if (allLit) {
        object->flags_ |= MapObjectFlag::Static;
        if (!wasStatic) {
            touchTime = gameapi::timeGetTime();
            flags_ |= kRedrawStatic;
        }
    } else {
        object->flags_ &= ~MapObjectFlag::Static;
    }
    if (dirty) {
        object->dirtyRect = drawnRect;
        *dirty = object->dirtyRect;
    }
    if (anyDrawn) object->flags_ |= MapObjectFlag::Shown;
    else object->flags_ &= ~MapObjectFlag::Shown;
    g_sceneRenderer->device_->SetMaterial(nullptr);
    Finish(lights, list);
    return 1;
}
