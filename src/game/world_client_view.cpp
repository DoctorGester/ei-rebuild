// CWorldClient per-frame view code: filing visible objects into the frame's draw lists, sorting
// those lists, and picking the ground point under the cursor. See world.h and docs/notes/world.md.
#include "world.h"

#include "core_maths.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "map_object_shadow.h"
#include "object_tables.h"
#include "stl_sort.h"
#include "terrain.h"

#include "ei/hook.h"

#include <cmath>
#include <cstring>
#include "originals_render.h"
#include "originals_library.h"
#include "originals_units.h"

// Every float expression here must keep the original's association: (a + b) + c.
#pragma float_control(precise, on, push)

namespace {

constexpr int kTypeHero = 0x32;           // CEngineObject::type_ of heroes
constexpr std::uint32_t kFlagVisible = 0x20;
constexpr std::uint32_t kFlagTouchModel = 0x200;
constexpr std::uint32_t kFlagAlwaysDrawn = 0x400000;

} // namespace

// The render device (MatrixDevice, world.h).
__declspec(naked) int MatrixDevice::Classify(const void*) EI_ORIGINAL(0x004F16A0)

namespace {

MatrixDevice* Device() { return TheMatrixDevice(); }

// 004D9880. out = a * b (4x4 float matrices; RET 8).
void MatrixMultiply(float* out, const float* a, const float* b) { MatrixMultiplyOriginal(out, nullptr, a, b); }

// 0051E300. Whether the model's shadow volume is in view (shadow settings 0079BD0C..).
__declspec(naked) bool __fastcall ShadowInView(CObject3DClientSpecific*) EI_ORIGINAL(0x0051E300)

// Terrain::HeightAt (006C8680) declared double: the original adds the unrounded ST0.
__declspec(naked) double __fastcall TerrainHeightAt(Terrain*, void*, float, float, int) EI_ORIGINAL(0x006C8680)

// The frame's draw lists: STLport vector<CEngineObject*> globals 0x10 apart (0056AFE0 clears them).
using ObjectVector = stlp::vector<CEngineObject*>;
struct DrawQueue {
    ObjectVector objects;
    std::uint32_t allocator;
};
static_assert(sizeof(DrawQueue) == 0x10);
struct DrawQueues {
    DrawQueue touchedModels;   // 007AF508 models to keep loaded (by last use)
    DrawQueue shadow52;        // 007AF518
    DrawQueue shadowUnits;     // 007AF528
    DrawQueue shadow5D;        // 007AF538
    DrawQueue shadowOther;     // 007AF548
    DrawQueue shadow54;        // 007AF558
    DrawQueue visible52;       // 007AF568
    DrawQueue visibleUnits;    // 007AF578
    DrawQueue visible5D;       // 007AF588
    DrawQueue visibleOther;    // 007AF598
    DrawQueue visible54;       // 007AF5A8
};
DrawQueues& Frame() { return *reinterpret_cast<DrawQueues*>(0x007AF508); }

// 00571B00. _M_insert_overflow: grows the storage and inserts n copies of *value at pos.
__declspec(naked) void __fastcall InsertOverflow(ObjectVector*, void*, CEngineObject**, CEngineObject* const*, int)
    EI_ORIGINAL(0x00571B00)

// 0056E840 (also inlined by the original): push_back.
void PushBack(DrawQueue& queue, CEngineObject* const& value) {
    ObjectVector& v = queue.objects;
    if (v.Finish() != v.EndOfStorage()) {
        if (v.Finish()) *v.Finish() = value;
        ++v.Finish();
    } else {
        InsertOverflow(&v, nullptr, v.Finish(), &value, 1);
    }
}

// STLport list<counted pointer>.
using ObjectList = stlp::list<CEngineObject*>;
using ObjectListNode = ObjectList::Node;

// 0056E8A0. insert(pos, value): new node holding a reference; returns *result.
ObjectListNode** ObjectListInsert(ObjectList* list, ObjectListNode** result, ObjectListNode* pos, CEngineObject* const& value) {
    return reinterpret_cast<ObjectListNode**>(ListInsert56E8A0Original(list, nullptr, reinterpret_cast<void**>(result), pos, &value));
}
ObjectList* DeferredUnits() { return reinterpret_cast<ObjectList*>(0x007AF5B4); }

const std::uint32_t& g_multiplayerView = *reinterpret_cast<const std::uint32_t*>(0x0079BE6C);
const std::uint32_t& g_modelTouchDelay = *reinterpret_cast<const std::uint32_t*>(0x0079BD24);
// 007AFDB0: the point PickGround / PickSurface return without a view matrix.
const float* NoPickPoint() { return reinterpret_cast<const float*>(0x007AFDB0); }

// Pushes the object's matrix on the device's object stack, as the original inlines it.
void PushObjectMatrix(MatrixDevice* device, float* matrix) {
    if (device->objectDepth == 0) {
        device->objectDepth = 1;
        std::memcpy(device->objectStack, matrix, 0x40);
    } else {
        int depth = ++device->objectDepth;
        MatrixMultiply(device->objectStack + (depth - 1) * 16, device->objectStack + (depth - 2) * 16, matrix);
    }
}

CObject3D* Object3D(CEngineObject* object) { return static_cast<CObject3D*>(object); }
CObject3DClientSpecific* ModelOf(CEngineObject* object) {
    return static_cast<CObject3DClientSpecific*>(Object3D(object)->attached);
}

} // namespace

// ---- Visibility ----------------------------------------------------------------------------------

int CWorldClient::CollectVisible(void* list, const float* view, float animTime, std::uint32_t now, char animateUnits,
                                 char withScripted) {
    MatrixDevice* device = Device();
    int touched = 0;
    // Only a multiplayer spectator view (session kind 1) leaves units it cannot see out.
    bool drawAllUnits = true;
    if (g_multiplayerView != 0 && Session()->gameKind == 1) drawAllUnits = false;

    ObjectListNode* node = static_cast<ObjectList*>(list)->head->next;
    if (node == static_cast<ObjectList*>(list)->head) return 0;
    do {
        CEngineObject* object = node->data;
        if (object && Object3D(object)->attached) {
            bool process = true;
            if (object->GetClassId() == world::kClassUnit) {
                if (object->type_ == kTypeHero && (IsTextKey(object->id_) & 0xFF) && !withScripted) {
                    process = false;
                } else if (animateUnits) {
                    ObjectSlots(object)->AdvanceTime(animTime);
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
                CObject3D* o3d = Object3D(object);
                float* matrix = o3d->worldMatrix;
                QuaternionToMatrixOriginal(&o3d->orientation, nullptr, matrix);
                std::memcpy(&matrix[3], &o3d->position.x, 4);
                std::memcpy(&matrix[7], &o3d->position.y, 4);
                std::memcpy(&matrix[11], &o3d->position.z, 4);
                const std::uint32_t zero = 0;
                const std::uint32_t one = 0x3F800000;   // 1.0f
                std::memcpy(&matrix[14], &zero, 4);
                std::memcpy(&matrix[13], &zero, 4);
                std::memcpy(&matrix[12], &zero, 4);
                std::memcpy(&matrix[15], &one, 4);
                PushObjectMatrix(device, matrix);
                int where = Device()->Classify(&o3d->boundsFlags);
                if (device->objectDepth != 0) --device->objectDepth;

                CEngineObject* const& entry = object;
                DrawQueues& frame = Frame();
                if (where != 2) {
                    object->flags_ |= kFlagVisible;
                    // View-space position: each row summed as (a + b) + c, then rounded.
                    const float* p = &o3d->position.x;
                    float z = StoreFloat((view[8] * p[0] + view[9] * p[1]) + view[10] * p[2]);
                    float y = StoreFloat((view[4] * p[0] + view[5] * p[1]) + view[6] * p[2]);
                    float x = StoreFloat((view[1] * p[1] + view[2] * p[2]) + view[0] * p[0]);
                    o3d->viewPosition[0] = x;
                    o3d->viewPosition[1] = y;
                    o3d->viewPosition[2] = z;
                    switch (object->GetClassId()) {
                    case 0x50:
                        PushBack(frame.visibleUnits, entry);
                        PushBack(frame.shadowUnits, entry);
                        break;
                    case 0x52:
                        PushBack(frame.visible52, entry);
                        PushBack(frame.shadow52, entry);
                        break;
                    case 0x54:
                        PushBack(frame.visible54, entry);
                        PushBack(frame.shadow54, entry);
                        break;
                    case 0x5D:
                        PushBack(frame.visible5D, entry);
                        PushBack(frame.shadow5D, entry);
                        break;
                    default:
                        PushBack(frame.visibleOther, entry);
                        PushBack(frame.shadowOther, entry);
                        break;
                    }
                    if ((object->flags_ & kFlagTouchModel) && now - ModelOf(object)->touchTime > g_modelTouchDelay) {
                        PushBack(frame.touchedModels, entry);
                        ++touched;
                    }
                } else {
                    object->flags_ &= ~kFlagVisible;
                    // Out of view: still listed for shadows when its shadow volume is in view.
                    if (ShadowInView(ModelOf(object))) {
                        if (o3d->animation) FigureApplyPendingOriginal(o3d->animation);
                        switch (object->GetClassId()) {
                        case 0x50: PushBack(frame.shadowUnits, entry); break;
                        case 0x52: PushBack(frame.shadow52, entry); break;
                        case 0x54: PushBack(frame.shadow54, entry); break;
                        case 0x5D: PushBack(frame.shadow5D, entry); break;
                        default: PushBack(frame.shadowOther, entry); break;
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
        std::memcpy(out, NoPickPoint(), 12);
        return out;
    }
    // Push the current view matrix on the picking stack (multiplied onto its top).
    float* top = device->viewStack + (device->viewDepth - 1) * 16;
    if (device->pickDepth == 0) {
        device->pickDepth = 1;
        std::memcpy(device->pickStack, top, 0x40);
    } else {
        int depth = ++device->pickDepth;
        MatrixMultiply(device->pickStack + (depth - 1) * 16, device->pickStack + (depth - 2) * 16, top);
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
        float half = StoreFloat(world->terrain_->heightRange * 0.5f);
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
        return TerrainHeightAt(terrain_, nullptr, px, py, 0);
    });
}

float* CWorldClient::PickSurface(float* out, int x, int y) {
    return PickPoint(this, out, x, y, [this](float px, float py) {
        return GroundHeightAt(px, py);
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
    return ModelOf(b)->touchTime < ModelOf(a)->touchTime;
}

EI_REPLACE_METHOD(0x0056A990, CWorldClient::CollectVisible);
EI_REPLACE_METHOD(0x0056BF30, CWorldClient::PickGround);
EI_REPLACE_METHOD(0x0056C350, CWorldClient::PickSurface);
EI_REPLACE(0x005740D0, IntroSortByModel);
EI_REPLACE(0x00574390, FinalSortByModel);
EI_REPLACE(0x00573D50, IntroSortByModelUse);
EI_REPLACE(0x00573F10, FinalSortByModelUse);
EI_REPLACE_METHOD(0x00573EE0, ModelUseLess::Less);
