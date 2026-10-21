// Drawing a 3D object on the client (CObject3DClientSpecific::Draw). See map_object_shadow.h and
// docs/notes/map_objects.md ("Drawing").
#include "game_arrays.h"          // aligned::AllocateDwords
#include "map_object.h"           // ObjectSlots
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

#include "ei/hook.h"
#include "originals_core.h"
#include "originals_effects.h"

#include <bit>
#include <cstring>
#include "originals_render.h"
#include "originals_units.h"
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
constexpr float kNearSquared = 1e9f;                // 0073DE58
constexpr float kLightMargin = 10.0f;               // 0073BAAC
constexpr std::uint32_t kOne = 0x3F800000;
constexpr int kStackWorld = 1;                      // the pipeline's matrix stacks
constexpr int kStackView = 2;
constexpr int kStackObject = 3;

// Not rewritten, forwarders:
// 006CB020. Light manager: is the point lit, AL.
// 004EB170. Device: sets the texture of stage 0.
// 00457720. A temporary texture reference: released.
void __fastcall ReleaseTemporary(void* object) { ReleaseCountedOriginal(object); }

// The light list being collected: aligned storage of light pointers.
using LightList = lib::AlignedArray<CEngineObject*>;

// Does `light` reach `object`: its range + 10 squared at least the squared distance.
bool InReach(const CObject3D* object, const CLight* light) {
    const Vector3& at = light->position_;
    float dx = static_cast<float>(static_cast<double>(object->position.x) - at.x);
    float dy = static_cast<float>(static_cast<double>(object->position.y) - at.y);
    float dz = static_cast<float>(static_cast<double>(object->position.z) - at.z);
    double reach = static_cast<double>(light->range_) + kLightMargin;
    double distanceSquared = LengthSquaredUnrounded<Sum3::ZXY, double>(dx, dy, dz);
    return reach * reach >= distanceSquared;   // NaN does not reach
}

bool PartTooFar(const CFigureMesh* part) {
    return LengthSquaredUnrounded<Sum3::XYZ, double>(part->localPosition_) > kNearSquared;
}

// The end of a draw: the iterator's reference to the light list and the light list itself.
void Finish(ObjectListIterator& lights, LightList& list) {
    if (lights.manager) {
        --reinterpret_cast<CEngineObject*>(lights.manager)->refCount_;
        lights.manager = nullptr;
    }
    if (list.data) gamecrt::operator_delete(reinterpret_cast<void**>(list.data)[-1]);
}
} // namespace

void ObjectWorldMatrix(const Quaternion* orientation, const Vector3* position, Matrix4* out) {
    QuaternionToMatrixOriginal(orientation, nullptr, out->m);
    std::memcpy(&out->m[11], &position->z, 4);
    std::memcpy(&out->m[3], &position->x, 4);
    std::memcpy(&out->m[7], &position->y, 4);
    std::memset(&out->m[12], 0, 3 * sizeof(float));
    std::memcpy(&out->m[15], &kOne, 4);
}

int CObject3DClientSpecific::Draw(CSceneRenderer* context, void* dirty) {
    if (object->flags_ & kObjectNotDrawn) return 1;
    std::uint32_t anyDirty = 0;
    CTransformPipeline* pipeline = context->pipeline_;
    Vector3 at;
    std::memcpy(&at, &object->position, sizeof(at));
    ObjectSlots(object)->LoadFigures();
    if (!(object->flags_ & kObjectAnimated)) object->flags_ &= ~kObjectStatic;
    if (dirty) {
        gameapi::SetRectEmpty(DrawnRect());
        gameapi::SetRectEmpty(object->DirtyRect());
    }
    if (object->animation) object->animation->UpdatePose();

    // The lights within reach (the list starts with room for 16).
    LightList list{nullptr, 0, 0, 0};
    ObjectListIterator lights{nullptr, nullptr};
    CEngineObject* light = lights.First(TheLightFactory());
    list.data = reinterpret_cast<CEngineObject**>(aligned::AllocateDwords(0x10));
    list.capacity = 0x10;
    list.size = 0;
    for (; light; light = lights.Next()) {
        if (context->sun_ == light) continue;
        if (!InReach(object, static_cast<CLight*>(light))) continue;
        if (light->flags_ & kLightRemoved) continue;
        if (light->flags_ & kLightMoving) object->flags_ &= ~kObjectStatic;
        list.SetAtGrow(list.size, light);   // 0051EE50
    }

    // The object's matrix, and its extra scale when it has one.
    ObjectWorldMatrix(&object->orientation, &object->position, &object->worldMatrix);
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
        std::memcpy(&m.m[0], &extraScale.x, 4);
        std::memcpy(&m.m[5], &extraScale.y, 4);
        std::memcpy(&m.m[10], &extraScale.z, 4);
        std::memcpy(&m.m[15], &kOne, 4);
        pipeline->PushMatrix(m.m, kStackView);
        pipeline->PushMatrix(m.m, kStackWorld);
        if (!(object->flags_ & kObjectStatic)) pipeline->PushMatrixProduct(m, kStackObject);
    }
    std::uint8_t lit = 0xFF;
    if (!(object->flags_ & kObjectUnlit)) {
        Terrain* terrain = static_cast<CWorldClient*>(ManagerContext())->terrain_;
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
        CEngineObject* temporary = nullptr;
        // The first set takes +0xDC (woundsMaterial) as stage 0's texture.
        TheRenderDevice()->SetMaterial(static_cast<CDeviceMaterial*>(k == 0 ? static_cast<void*>(woundsMaterial) : temporary));
        if (k != 0 && temporary) ReleaseTemporary(temporary);   // never set
        set = partSets[k];
        for (int g = 0; g < set->groups.size; ++g) {
            for (int p = 0; p < set->groups[g]->parts.size; ++p) {
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
                    RECT* total = DrawnRect();
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
        std::memcpy(object->dirtyRect, drawnRect, sizeof(RECT));
        std::memcpy(dirty, object->dirtyRect, sizeof(RECT));
    }
    if (anyDirty) object->flags_ |= kObjectDirty;
    else object->flags_ &= ~kObjectDirty;
    TheRenderDevice()->SetMaterial(nullptr);
    Finish(lights, list);
    return 1;
}
EI_REPLACE_METHOD(0x0051B7E0, CObject3DClientSpecific::Draw);
