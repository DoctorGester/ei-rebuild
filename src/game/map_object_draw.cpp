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
constexpr std::uint32_t kObjectNotDrawn = 0x40;     // object->flags_
constexpr std::uint32_t kObjectAnimated = 0x200;
constexpr std::uint32_t kObjectStatic = 0x400;
constexpr std::uint32_t kObjectUnlit = 0x800;
constexpr std::uint32_t kObjectDirty = 0x20;
constexpr std::uint32_t kLightRemoved = 0x80;       // light->flags_
constexpr std::uint32_t kLightMoving = 0x100;
constexpr std::uint32_t kPartMoving = 0x10;         // part->flags_
constexpr std::uint32_t kPartDirty = 0x40;
constexpr std::uint32_t kPartHidden = 0x80;
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
        --lights.manager->refCount_;
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
    if (object->flags_ & kObjectNotDrawn) return 1;
    std::uint32_t anyDirty = 0;
    CTransformPipeline* pipeline = context->pipeline_;
    Vector3 at;
    at = object->localPosition_;
    object->LoadFigures();
    if (!(object->flags_ & kObjectAnimated)) object->flags_ &= ~kObjectStatic;
    if (dirty) {
        gameapi::SetRectEmpty(&drawnRect);
        gameapi::SetRectEmpty(&object->dirtyRect);
    }
    if (object->animation) object->animation->UpdatePose();

    // The lights within reach (the list starts with room for 16).
    std::vector<CLight*> list;
    list.reserve(0x10);
    ObjectListIteratorOf<CLight> lights{};
    CLight* light = lights.First(g_clientWorld->lightFactory_);
    for (; light; light = lights.Next()) {
        if (context->sun_ == light) continue;
        if (!InReach(object, light)) continue;
        if (light->flags_ & kLightRemoved) continue;
        if (light->flags_ & kLightMoving) object->flags_ &= ~kObjectStatic;
        list.push_back(light);
    }

    // The object's matrix, and its extra scale when it has one.
    ObjectWorldMatrix(&object->localRotation_, &object->localPosition_, &object->worldMatrix);
    Matrix4 m = object->worldMatrix;
    pipeline->PushMatrix(m.m, kStackView);
    pipeline->PushMatrix(m.m, kStackWorld);
    if (!(object->flags_ & kObjectStatic)) pipeline->PushMatrixProduct(m, kStackObject);
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
        if (!(object->flags_ & kObjectStatic)) pipeline->PushMatrixProduct(m, kStackObject);
    }
    std::uint8_t lit = 0xFF;
    if (!(object->flags_ & kObjectUnlit)) {
        Terrain* terrain = ClientWorld()->terrain_;
        lit = terrain->EdgeFade(&at);   // AL as it is
    }

    // The parts of both sets.
    std::uint32_t allStatic = kPartMoving;
    std::uint32_t wasStatic = object->flags_ & kObjectStatic;
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
                if (!(object->flags_ & kObjectStatic)) part->flags_ &= ~kPartMoving;
                bool pushed = !(part->flags_ & kPartMoving);
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
                anyDirty |= part->flags_ & kPartDirty;
                if (dirty && !gameapi::IsRectEmpty(&partRect) && (part->flags_ & kPartDirty)) {
                    RECT* total = &drawnRect;
                    gameapi::UnionRect(total, total, &partRect);
                }
                if (pushed) pipeline->PopMatrix(kStackObject);
                pipeline->PopMatrix(kStackView);
                pipeline->PopMatrix(kStackWorld);
                allStatic &= part->flags_ & kPartMoving;
            }
        }
    }
    if (scaled) {
        if (!(object->flags_ & kObjectStatic)) pipeline->PopMatrix(kStackObject);
        pipeline->PopMatrix(kStackView);
        pipeline->PopMatrix(kStackWorld);
    }
    if (!(object->flags_ & kObjectStatic)) pipeline->PopMatrix(kStackObject);
    pipeline->PopMatrix(kStackView);
    pipeline->PopMatrix(kStackWorld);

    if (allStatic) {
        object->flags_ |= kObjectStatic;
        if (!wasStatic) {
            touchTime = gameapi::timeGetTime();
            flags_ |= kRedrawStatic;
        }
    } else {
        object->flags_ &= ~kObjectStatic;
    }
    if (dirty) {
        object->dirtyRect = drawnRect;
        *dirty = object->dirtyRect;
    }
    if (anyDirty) object->flags_ |= kObjectDirty;
    else object->flags_ &= ~kObjectDirty;
    g_sceneRenderer->device_->SetMaterial(nullptr);
    Finish(lights, list);
    return 1;
}
