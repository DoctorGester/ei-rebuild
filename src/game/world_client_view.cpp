// CWorldClient per-frame view code: filing visible objects into the frame's draw lists, sorting
// those lists, and picking the ground point under the cursor. See world.h and docs/notes/world.md.
#include "world.h"

#include "core_maths.h"
#include "object_tables.h"
#include "stl_sort.h"

#include "ei/hook.h"
#include "ei/vcall.h"

#include <cmath>
#include <cstring>
#include "originals_render.h"
#include "originals_library.h"
#include "originals_units.h"

// Every float expression here must keep the original's association: (a + b) + c.
#pragma float_control(precise, on, push)

namespace {

constexpr int kSlotAnimate = 25;          // units: advance the animation to a game time (RET 4)
constexpr int kSlotSurfaceHeight = 29;    // CWorldClient: height with platforms (005618E0)
constexpr int kClassUnit = 0x50;
constexpr int kTypeHero = 0x32;           // CEngineObject::type_ of heroes
constexpr std::uint32_t kFlagVisible = 0x20;
constexpr std::uint32_t kFlagTouchModel = 0x200;
constexpr std::uint32_t kFlagAlwaysDrawn = 0x400000;

template <typename T>
__forceinline T& Field(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(object) + offset);
}

} // namespace

// The render device (MatrixDevice, world.h).
__declspec(naked) int MatrixDevice::Classify(const void*) EI_ORIGINAL(0x004F16A0)

namespace {

MatrixDevice* Device() { return TheMatrixDevice(); }

// The quaternion at +0x34 of a map object.
struct Orientation {
    // 004D9AA0. Writes the rotation into the 4x4 matrix `out` (RET 4).
    void ToMatrix(float* out);
};
void Orientation::ToMatrix(float* a0) { QuaternionToMatrixOriginal((void*)this, nullptr, a0); }

// The object's model (+0x94; CObject3DClientSpecific range).
struct ObjectModel {
    // 0051E300. Whether the model's shadow volume is in view (shadow settings 0079BD0C..).
    bool ShadowInView();
};
__declspec(naked) bool ObjectModel::ShadowInView() EI_ORIGINAL(0x0051E300)

// The object's +0x98 (figure animation state).
struct FigureState {
    // 0050D450. Applies a pending animation change once (clears its +0x74 flag).
    void ApplyPending();
};
void FigureState::ApplyPending() { FigureApplyPendingOriginal((void*)this); }

// Terrain::HeightAt (006C8680) declared double: the original adds the unrounded ST0.
struct TerrainHeight {
    double HeightAt(float x, float y, int layer);
};
__declspec(naked) double TerrainHeight::HeightAt(float, float, int) EI_ORIGINAL(0x006C8680)

// STLport vector<CEngineObject*> (the frame's draw lists are globals).
struct ObjectVector {
    CEngineObject** begin;
    CEngineObject** end;
    CEngineObject** capacity;

    // 00571B00. _M_insert_overflow: grows the storage and inserts n copies of *value at pos.
    void InsertOverflow(CEngineObject** pos, CEngineObject* const* value, int n);

    // 0056E840 (also inlined by the original): push_back.
    void PushBack(CEngineObject* const& value) {
        if (end != capacity) {
            if (end) *end = value;
            ++end;
        } else {
            InsertOverflow(end, &value, 1);
        }
    }
};
__declspec(naked) void ObjectVector::InsertOverflow(CEngineObject**, CEngineObject* const*, int) EI_ORIGINAL(0x00571B00)

ObjectVector& DrawList(std::uintptr_t address) { return *reinterpret_cast<ObjectVector*>(address); }

// STLport list<counted pointer>.
using ObjectList = stlp::list<CEngineObject*>;
using ObjectListNode = ObjectList::Node;

// 0056E8A0. insert(pos, value): new node holding a reference; returns *result.
ObjectListNode** ObjectListInsert(ObjectList* list, ObjectListNode** result, ObjectListNode* pos, CEngineObject* const& value) {
    return reinterpret_cast<ObjectListNode**>(ListInsert56E8A0Original(list, nullptr, reinterpret_cast<void**>(result), pos, &value));
}

// The frame's lists (0056AFE0 clears them before collecting).
constexpr std::uintptr_t kTouchedModels = 0x007AF508;   // models to keep loaded (by last use)
constexpr std::uintptr_t kShadow52 = 0x007AF518;
constexpr std::uintptr_t kShadowUnits = 0x007AF528;
constexpr std::uintptr_t kShadow5D = 0x007AF538;
constexpr std::uintptr_t kShadowOther = 0x007AF548;
constexpr std::uintptr_t kShadow54 = 0x007AF558;
constexpr std::uintptr_t kVisible52 = 0x007AF568;
constexpr std::uintptr_t kVisibleUnits = 0x007AF578;
constexpr std::uintptr_t kVisible5D = 0x007AF588;
constexpr std::uintptr_t kVisibleOther = 0x007AF598;
constexpr std::uintptr_t kVisible54 = 0x007AF5A8;
ObjectList* DeferredUnits() { return reinterpret_cast<ObjectList*>(0x007AF5B4); }

const std::uint32_t& g_multiplayerView = *reinterpret_cast<const std::uint32_t*>(0x0079BE6C);
const std::uint32_t& g_modelTouchDelay = *reinterpret_cast<const std::uint32_t*>(0x0079BD24);

// Pushes the object's matrix (at +0x54) on the device's object stack, as the original inlines it.
void PushObjectMatrix(MatrixDevice* device, float* matrix) {
    if (device->objectDepth == 0) {
        device->objectDepth = 1;
        std::memcpy(device->objectStack, matrix, 0x40);
    } else {
        int depth = ++device->objectDepth;
        // 004D9880: top = previous top * matrix.
        MatrixMultiplyOriginal(device->objectStack + (depth - 1) * 16, nullptr, device->objectStack + (depth - 2) * 16,
                               matrix);
    }
}

} // namespace

// ---- Visibility ----------------------------------------------------------------------------------

int CWorldClient::CollectVisible(void* list, const float* view, float animTime, std::uint32_t now, char animateUnits,
                                 char withScripted) {
    MatrixDevice* device = Device();
    int touched = 0;
    // Only a multiplayer spectator view (session +0x100 == 1) leaves units it cannot see out.
    bool drawAllUnits = true;
    if (g_multiplayerView != 0 && Field<int>(session_, 0x100) == 1) drawAllUnits = false;

    ObjectListNode* node = static_cast<ObjectList*>(list)->head->next;
    if (node == static_cast<ObjectList*>(list)->head) return 0;
    do {
        CEngineObject* object = node->data;
        if (object && Field<void*>(object, 0x94)) {
            bool process = true;
            if (object->GetClassId() == kClassUnit) {
                if (object->type_ == kTypeHero && (IsTextKey(object->id_) & 0xFF) && !withScripted) {
                    process = false;
                } else if (animateUnits) {
                    ei::VCall<void>(object, kSlotAnimate, animTime);
                    if (!drawAllUnits && !(object->flags_ & kFlagAlwaysDrawn)) process = false;
                } else {
                    // First pass: units wait on the deferred list (which holds a reference).
                    CEngineObject* held = object;
                    ++held->refCount_;
                    ObjectListNode* result;
                    ObjectListInsert(DeferredUnits(), &result, DeferredUnits()->head, held);
                    held->Release();
                    process = false;
                }
            }
            if (process) {
                // Object -> world matrix: the rotation with the position as the last column.
                float* matrix = &Field<float>(object, 0x54);
                reinterpret_cast<Orientation*>(&Field<float>(object, 0x34))->ToMatrix(matrix);
                Field<std::uint32_t>(object, 0x60) = Field<std::uint32_t>(object, 0x1C);
                Field<std::uint32_t>(object, 0x70) = Field<std::uint32_t>(object, 0x20);
                Field<std::uint32_t>(object, 0x80) = Field<std::uint32_t>(object, 0x24);
                Field<std::uint32_t>(object, 0x8C) = 0;
                Field<std::uint32_t>(object, 0x88) = 0;
                Field<std::uint32_t>(object, 0x84) = 0;
                Field<std::uint32_t>(object, 0x90) = 0x3F800000;   // 1.0f
                PushObjectMatrix(device, matrix);
                int where = Device()->Classify(&Field<std::uint8_t>(object, 0x124));
                if (device->objectDepth != 0) --device->objectDepth;

                CEngineObject* const& entry = object;
                if (where != 2) {
                    object->flags_ |= kFlagVisible;
                    // View-space position (+0x28): each row summed as (a + b) + c, then rounded.
                    const float* p = &Field<float>(object, 0x1C);
                    float z = StoreFloat((view[8] * p[0] + view[9] * p[1]) + view[10] * p[2]);
                    float y = StoreFloat((view[4] * p[0] + view[5] * p[1]) + view[6] * p[2]);
                    float x = StoreFloat((view[1] * p[1] + view[2] * p[2]) + view[0] * p[0]);
                    Field<float>(object, 0x28) = x;
                    Field<float>(object, 0x2C) = y;
                    Field<float>(object, 0x30) = z;
                    switch (object->GetClassId()) {
                    case 0x50:
                        DrawList(kVisibleUnits).PushBack(entry);
                        DrawList(kShadowUnits).PushBack(entry);
                        break;
                    case 0x52:
                        DrawList(kVisible52).PushBack(entry);
                        DrawList(kShadow52).PushBack(entry);
                        break;
                    case 0x54:
                        DrawList(kVisible54).PushBack(entry);
                        DrawList(kShadow54).PushBack(entry);
                        break;
                    case 0x5D:
                        DrawList(kVisible5D).PushBack(entry);
                        DrawList(kShadow5D).PushBack(entry);
                        break;
                    default:
                        DrawList(kVisibleOther).PushBack(entry);
                        DrawList(kShadowOther).PushBack(entry);
                        break;
                    }
                    if ((object->flags_ & kFlagTouchModel) &&
                        now - Field<std::uint32_t>(Field<void*>(object, 0x94), 0xEC) > g_modelTouchDelay) {
                        DrawList(kTouchedModels).PushBack(entry);
                        ++touched;
                    }
                } else {
                    object->flags_ &= ~kFlagVisible;
                    // Out of view: still listed for shadows when its shadow volume is in view.
                    if (static_cast<ObjectModel*>(Field<void*>(object, 0x94))->ShadowInView()) {
                        if (Field<void*>(object, 0x98)) static_cast<FigureState*>(Field<void*>(object, 0x98))->ApplyPending();
                        switch (object->GetClassId()) {
                        case 0x50: DrawList(kShadowUnits).PushBack(entry); break;
                        case 0x52: DrawList(kShadow52).PushBack(entry); break;
                        case 0x54: DrawList(kShadow54).PushBack(entry); break;
                        case 0x5D: DrawList(kShadow5D).PushBack(entry); break;
                        default: DrawList(kShadowOther).PushBack(entry); break;
                        }
                    }
                }
            }
        }
        node = node->next;
    } while (node != static_cast<ObjectList*>(list)->head);
    return touched;
}

// ---- Cursor picking ------------------------------------------------------------------------------

namespace {

// Clamp used by the picking: negative or NaN becomes +0 (a branch-free select on FCOM's C0).
__forceinline float NonNegative(float v) { return !(v >= 0.0f) ? 0.0f : v; }

// 0056BF30 / 0056C350 share everything but the height function (which returns ST0 unrounded).
template <typename Height>
float* PickPoint(CWorldClient* world, float* out, int screenX, int screenY, Height height) {
    MatrixDevice* device = Device();
    if (device->viewDepth == 0) {
        std::memcpy(out, reinterpret_cast<const void*>(0x007AFDB0), 12);
        return out;
    }
    // Push the current view matrix on the picking stack (multiplied onto its top).
    float* top = device->viewStack + (device->viewDepth - 1) * 16;
    if (device->pickDepth == 0) {
        device->pickDepth = 1;
        std::memcpy(device->pickStack, top, 0x40);
    } else {
        int depth = ++device->pickDepth;
        MatrixMultiplyOriginal(device->pickStack + (depth - 1) * 16, nullptr, device->pickStack + (depth - 2) * 16, top);
    }
    MatrixDevice* current = Device();
    const float* m = current->pickStack + (current->pickDepth - 1) * 16;

    // The values the original keeps on the FPU stack are doubles here (no rounding to float).
    double sx = screenX;
    double sy = screenY;
    float outX = 0.0f, outY = 0.0f, pz = 0.0f;
    double det = ((m[4] * m[13] - m[5] * m[12]) * sx + (m[1] * m[12] - m[0] * m[13]) * sy) + (m[0] * m[5] - m[4] * m[1]);
    // FCOM + TEST AH,0x40: skipped when zero or unordered.
    if (det < 0.0 || det > 0.0) {
        // The screen ray as x = a z + b, y = c z + d.
        float a = StoreFloat(static_cast<float>(
            (((m[13] * m[2] - m[1] * m[14]) * sy + (m[14] * m[5] - m[13] * m[6]) * sx) + (m[1] * m[6] - m[5] * m[2])) / det));
        float b = StoreFloat(static_cast<float>(
            (((m[5] * m[15] - m[13] * m[7]) * sx + (m[3] * m[13] - m[1] * m[15]) * sy) + (m[1] * m[7] - m[3] * m[5])) / det));
        float c = StoreFloat(static_cast<float>(
            -((((m[12] * m[2] - m[0] * m[14]) * sy + (m[4] * m[14] - m[6] * m[12]) * sx) + (m[0] * m[6] - m[4] * m[2])) / det)));
        float d = StoreFloat(static_cast<float>(
            -((((m[4] * m[15] - m[12] * m[7]) * sx + (m[3] * m[12] - m[0] * m[15]) * sy) + (m[0] * m[7] - m[3] * m[4])) / det)));

        // Start halfway up the terrain's height range.
        float half = StoreFloat(Field<float>(world->terrain_, 0x114) * 0.5f);
        float x = NonNegative(StoreFloat(a * half + b));
        float y = NonNegative(StoreFloat(c * half + d));
        pz = StoreFloat(static_cast<float>((height(x, y) + half) * 0.5f));

        // Fixed-point iteration on the height (up to 100 steps)...
        float previous;
        int steps = 0;
        bool converged = false;
        for (;;) {
            previous = pz;
            outX = NonNegative(StoreFloat(a * previous + b));
            outY = NonNegative(StoreFloat(c * previous + d));
            pz = StoreFloat(static_cast<float>(height(outX, outY)));
            float diff = std::fabs(StoreFloat(pz - previous));
            ++steps;
            if (!(diff > 0.01)) {
                converged = steps < 100;
                break;
            }
            if (steps >= 100) break;
        }
        // ...and when it did not settle, bisection between the last two heights.
        if (!converged) {
            steps = 0;
            do {
                previous = StoreFloat((pz + previous) * 0.5f);
                outX = NonNegative(StoreFloat(a * previous + b));
                outY = NonNegative(StoreFloat(c * previous + d));
                pz = StoreFloat(static_cast<float>(height(outX, outY)));
                float diff = std::fabs(StoreFloat(pz - previous));
                ++steps;
                if (!(diff > 0.01)) break;
            } while (steps < 100);
        }
    }
    if (device->pickDepth != 0) --device->pickDepth;
    out[0] = outX;
    std::memcpy(&out[1], &outY, 4);
    std::memcpy(&out[2], &pz, 4);
    return out;
}

} // namespace

float* CWorldClient::PickGround(float* out, int x, int y) {
    return PickPoint(this, out, x, y, [this](float px, float py) {
        return reinterpret_cast<TerrainHeight*>(terrain_)->HeightAt(px, py, 0);
    });
}

float* CWorldClient::PickSurface(float* out, int x, int y) {
    return PickPoint(this, out, x, y, [this](float px, float py) {
        return ei::VCall<double>(this, kSlotSurfaceHeight, px, py);
    });
}

#pragma float_control(pop)

// ---- Draw list sorting ---------------------------------------------------------------------------

namespace {

template <typename Less>
using PtrSort = StlSort<CEngineObject*, Less>;

struct ByModel {
    bool operator()(CEngineObject* a, CEngineObject* b) const {
        UnitOrderLess less;
        return less.Less(a, b);
    }
};

struct ByModelUse {
    bool operator()(CEngineObject* a, CEngineObject* b) const {
        ModelUseLess less;
        return less.Less(a, b) != 0;
    }
};

} // namespace

void __fastcall IntroSortByModel(CEngineObject** first, CEngineObject** last, void*, int depthLimit, int) {
    PtrSort<ByModel>::IntroLoop(first, last, depthLimit, ByModel());
}

void __fastcall FinalSortByModel(CEngineObject** first, CEngineObject** last, int) {
    PtrSort<ByModel>::FinalInsertionSort(first, last, ByModel());
}

void __fastcall IntroSortByModelUse(CEngineObject** first, CEngineObject** last, void*, int depthLimit, int) {
    PtrSort<ByModelUse>::IntroLoop(first, last, depthLimit, ByModelUse());
}

void __fastcall FinalSortByModelUse(CEngineObject** first, CEngineObject** last, int) {
    PtrSort<ByModelUse>::FinalInsertionSort(first, last, ByModelUse());
}

BOOL ModelUseLess::Less(CEngineObject* a, CEngineObject* b) {
    return Field<std::uint32_t>(Field<void*>(b, 0x94), 0xEC) < Field<std::uint32_t>(Field<void*>(a, 0x94), 0xEC);
}

EI_REPLACE_METHOD(0x0056A990, CWorldClient::CollectVisible);
EI_REPLACE_METHOD(0x0056BF30, CWorldClient::PickGround);
EI_REPLACE_METHOD(0x0056C350, CWorldClient::PickSurface);
EI_REPLACE(0x005740D0, IntroSortByModel);
EI_REPLACE(0x00574390, FinalSortByModel);
EI_REPLACE(0x00573D50, IntroSortByModelUse);
EI_REPLACE(0x00573F10, FinalSortByModelUse);
EI_REPLACE_METHOD(0x00573EE0, ModelUseLess::Less);
