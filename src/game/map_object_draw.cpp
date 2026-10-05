// Drawing a 3D object on the client (CObject3DClientSpecific::Draw). See map_object_shadow.h and
// docs/notes/map_objects.md ("Drawing").
#include "map_object_figures.h"
#include "map_object_shadow.h"
#include "object.h"

#include "lib/crt.h"
#include "lib/gameapi.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "originals_effects.h"

#include <bit>
#include <cstring>
#include "originals_render.h"

// The light range test keeps its sums on the FPU stack like the original.
#pragma float_control(precise, on)

namespace {
template <typename T>
T& At(void* base, int offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(base) + offset);
}

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
constexpr int kDrawSlot = 0x34 / 4;                 // the context's part draw (13)
constexpr int kTextureSlot = 0x2C / 4;              // the context's texture check (11)
constexpr int kStackWorld = 1;                      // the scene's matrix stacks
constexpr int kStackView = 2;
constexpr int kStackObject = 3;

// Not rewritten, forwarders:
// 0050D450. Animation state: brings the tracks to the current time.
__declspec(naked) void __fastcall UpdateAnimation(void*) EI_ORIGINAL(0x0050D450)
// 0051EF90 / 0051EE50. The light list: aligned storage for `count` pointers; SetAtGrow.
__declspec(naked) void* __fastcall AllocateLights(int) EI_ORIGINAL(0x0051EF90)
__declspec(naked) void __fastcall AddLight(void*, void*, int, CEngineObject*) EI_ORIGINAL(0x0051EE50)
// 004D9AA0. A quaternion into a 4x4 matrix.
__declspec(naked) void __fastcall OrientationToMatrix(void*, void*, float*) EI_ORIGINAL(0x004D9AA0)
// 004D9B80 / 004D9880 / 0051C110. The scene's matrix stacks: push (multiplied with the top),
// multiply, pop.
__declspec(naked) void __fastcall PushSceneMatrix(void*, void*, const float*, int) EI_ORIGINAL(0x004D9B80)
inline void __fastcall MultiplyMatrix(float* out, void*, const float* a, const float* b) { MatrixMultiplyOriginal(out, nullptr, a, b); }
__declspec(naked) void __fastcall PopSceneMatrix(void*, void*, int) EI_ORIGINAL(0x0051C110)
// 006CB020. Light manager: is the point lit, AL.
// 004EB170. Device: sets the texture of stage 0.
__declspec(naked) void __fastcall SetDeviceTexture(void*, void*, CEngineObject*) EI_ORIGINAL(0x004EB170)
// 00457720. A temporary texture reference: released.
__declspec(naked) void __fastcall ReleaseTemporary(void*) EI_ORIGINAL(0x00457720)

// The light list being collected: aligned storage of light pointers.
struct LightList {
    CEngineObject** data;
    int size;
    int capacity;
    int growBy;
};

// A set of parts the object is drawn in (this + 0x20 / + 0x24): a material, then groups of parts.
struct PartGroup {
    void* material;                 // passed to the context's draw
    FigurePart** parts;
    int count;
};
struct PartSet {
    void* material;                 // the context's texture check
    PartGroup** groups;
    int count;
};

// The scene's stack `index`: data at +0x14 + 8 * index, depth at +0x18 + 8 * index.
int& StackDepth(void* scene, int index) { return At<int>(scene, 0x18 + 8 * index); }
float* StackData(void* scene, int index) { return At<float*>(scene, 0x14 + 8 * index); }

// Pushes `m` onto the object stack (3) the way the original inlines it.
void PushObjectMatrix(void* scene, const float* m) {
    int& depth = StackDepth(scene, kStackObject);
    float* data = StackData(scene, kStackObject);
    if (depth == 0) {
        depth = 1;
        std::memcpy(data, m, 64);
        return;
    }
    int top = ++depth;
    MultiplyMatrix(data + 16 * (top - 1), nullptr, data + 16 * (top - 2), m);
}

void DropDepth(void* scene, int index) {
    int& depth = StackDepth(scene, index);
    if (depth != 0) --depth;
}

// The object's or part's matrix (+0x54) from its orientation (+0x34) and position.
void BuildMatrix(CEngineObject* object) {
    auto* raw = reinterpret_cast<char*>(object);
    OrientationToMatrix(raw + 0x34, nullptr, reinterpret_cast<float*>(raw + 0x54));
    std::memcpy(raw + 0x80, raw + 0x24, 4);
    std::memcpy(raw + 0x60, raw + 0x1C, 4);
    std::memcpy(raw + 0x70, raw + 0x20, 4);
    At<std::uint32_t>(raw, 0x8C) = 0;
    At<std::uint32_t>(raw, 0x88) = 0;
    At<std::uint32_t>(raw, 0x84) = 0;
    At<std::uint32_t>(raw, 0x90) = kOne;
}

// Does `light` reach `object`: its range + 10 squared at least the squared distance.
bool InReach(const CObject3D* object, CEngineObject* light) {
    const float* at = &At<float>(light, 0x1C);
    float dx = static_cast<float>(static_cast<double>(object->position[0]) - at[0]);
    float dy = static_cast<float>(static_cast<double>(object->position[1]) - at[1]);
    float dz = static_cast<float>(static_cast<double>(object->position[2]) - at[2]);
    double reach = static_cast<double>(At<float>(light, 0xE0)) + kLightMargin;
    double distanceSquared = (static_cast<double>(dx) * dx + static_cast<double>(dz) * dz) + static_cast<double>(dy) * dy;
    return reach * reach >= distanceSquared;   // NaN does not reach
}

bool PartTooFar(const FigurePart* part) {
    double x = part->position[0];
    double y = part->position[1];
    double z = part->position[2];
    return (x * x + y * y) + z * z > kNearSquared;
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

int CObject3DClientSpecific::Draw(ShadowDrawContext* context, void* dirty) {
    if (object->flags_ & kObjectNotDrawn) return 1;
    std::uint32_t anyDirty = 0;
    void* scene = context->scene;
    float at[3];
    std::memcpy(at, object->position, sizeof(at));
    ei::VCall<void>(object, 0x68 / 4);
    if (!(object->flags_ & kObjectAnimated)) object->flags_ &= ~kObjectStatic;
    if (dirty) {
        gameapi::SetRectEmpty(&At<RECT>(this, 0xC8));
        gameapi::SetRectEmpty(&At<RECT>(object, 0x100));
    }
    if (object->animation) UpdateAnimation(object->animation);

    // The lights within reach (the list starts with room for 16).
    LightList list{nullptr, 0, 0, 0};
    ObjectListIterator lights{nullptr, nullptr};
    CEngineObject* light = lights.First(*reinterpret_cast<CBaseManager**>(0x007AF748));
    list.data = static_cast<CEngineObject**>(AllocateLights(0x10));
    list.capacity = 0x10;
    list.size = 0;
    for (; light; light = lights.Next()) {
        if (At<CEngineObject*>(context, 0x1C) == light) continue;
        if (!InReach(object, light)) continue;
        if (light->flags_ & kLightRemoved) continue;
        if (light->flags_ & kLightMoving) object->flags_ &= ~kObjectStatic;
        AddLight(&list, nullptr, list.size, light);
    }

    // The object's matrix, and its extra scale when it has one.
    BuildMatrix(object);
    float m[16];
    std::memcpy(m, &At<float>(object, 0x54), sizeof(m));
    PushSceneMatrix(scene, nullptr, m, kStackView);
    PushSceneMatrix(scene, nullptr, m, kStackWorld);
    if (!(object->flags_ & kObjectStatic)) PushObjectMatrix(scene, m);
    const float* extraScale = &At<float>(this, 0xF0);
    bool scaled = !(std::bit_cast<std::uint32_t>(extraScale[0]) == kOne &&
                    std::bit_cast<std::uint32_t>(extraScale[1]) == kOne &&
                    std::bit_cast<std::uint32_t>(extraScale[2]) == kOne);
    if (scaled) {
        std::memset(m, 0, sizeof(m));
        std::memcpy(&m[0], &extraScale[0], 4);
        std::memcpy(&m[5], &extraScale[1], 4);
        std::memcpy(&m[10], &extraScale[2], 4);
        std::memcpy(&m[15], &kOne, 4);
        PushSceneMatrix(scene, nullptr, m, kStackView);
        PushSceneMatrix(scene, nullptr, m, kStackWorld);
        if (!(object->flags_ & kObjectStatic)) PushObjectMatrix(scene, m);
    }
    std::uint8_t lit = 0xFF;
    if (!(object->flags_ & kObjectUnlit)) {
        void* lightManager = At<void*>(ManagerContext(), 0x180);
        lit = IsLit(lightManager, nullptr, at);   // AL as it is
    }

    // The parts of both sets.
    std::uint32_t allStatic = kPartMoving;
    std::uint32_t wasStatic = object->flags_ & kObjectStatic;
    RECT partRect;
    for (int k = 0; k < 2; ++k) {
        auto* set = At<PartSet*>(this, 0x20 + 4 * k);
        if (!set) continue;
        if (!ei::VCall<int>(context, kTextureSlot, 0, set->material)) {
            int result = game::FatalError(reinterpret_cast<const char*>(0x0078F518));
            Finish(lights, list);
            return result;
        }
        CEngineObject* temporary = nullptr;
        SetDeviceTexture(*reinterpret_cast<void**>(0x007AF080), nullptr,
                         k == 0 ? At<CEngineObject*>(this, 0xDC) : temporary);
        if (k != 0 && temporary) ReleaseTemporary(temporary);   // never set
        set = At<PartSet*>(this, 0x20 + 4 * k);
        for (int g = 0; g < set->count; ++g) {
            for (int p = 0; p < set->groups[g]->count; ++p) {
                FigurePart* part = set->groups[g]->parts[p];
                if (PartTooFar(part)) continue;
                if (!(object->flags_ & kObjectStatic)) part->flags_ &= ~kPartMoving;
                bool pushed = !(part->flags_ & kPartMoving);
                BuildMatrix(part);
                std::memcpy(m, &At<float>(part, 0x54), sizeof(m));
                PushSceneMatrix(scene, nullptr, m, kStackView);
                PushSceneMatrix(scene, nullptr, m, kStackWorld);
                if (pushed) PushObjectMatrix(scene, m);
                PartSet* current = At<PartSet*>(this, 0x20 + 4 * k);
                int drawn = ei::VCall<int>(context, kDrawSlot, part, &list, current->groups[g]->material,
                                           static_cast<std::uint32_t>(lit), dirty ? &partRect : nullptr, 0, 0);
                if (!drawn) {
                    int result = game::FatalError(reinterpret_cast<const char*>(0x0078F4E0));
                    Finish(lights, list);
                    return result;
                }
                anyDirty |= part->flags_ & kPartDirty;
                if (dirty && !gameapi::IsRectEmpty(&partRect) && (part->flags_ & kPartDirty)) {
                    RECT* total = &At<RECT>(this, 0xC8);
                    gameapi::UnionRect(total, total, &partRect);
                }
                if (pushed) DropDepth(scene, kStackObject);
                DropDepth(scene, kStackView);
                PopSceneMatrix(scene, nullptr, kStackWorld);
                allStatic &= part->flags_ & kPartMoving;
            }
        }
    }
    if (scaled) {
        if (!(object->flags_ & kObjectStatic)) DropDepth(scene, kStackObject);
        DropDepth(scene, kStackView);
        PopSceneMatrix(scene, nullptr, kStackWorld);
    }
    if (!(object->flags_ & kObjectStatic)) DropDepth(scene, kStackObject);
    DropDepth(scene, kStackView);
    PopSceneMatrix(scene, nullptr, kStackWorld);

    if (allStatic) {
        object->flags_ |= kObjectStatic;
        if (!wasStatic) {
            At<std::uint32_t>(this, 0xEC) = gameapi::timeGetTime();
            flags_ |= kRedrawStatic;
        }
    } else {
        object->flags_ &= ~kObjectStatic;
    }
    if (dirty) {
        std::memcpy(&At<RECT>(object, 0x100), &At<RECT>(this, 0xC8), sizeof(RECT));
        std::memcpy(dirty, &At<RECT>(object, 0x100), sizeof(RECT));
    }
    if (anyDirty) object->flags_ |= kObjectDirty;
    else object->flags_ &= ~kObjectDirty;
    SetDeviceTexture(*reinterpret_cast<void**>(0x007AF080), nullptr, nullptr);
    Finish(lights, list);
    return 1;
}
EI_REPLACE_METHOD(0x0051B7E0, CObject3DClientSpecific::Draw);
