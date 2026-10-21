// The unit AI's look arrays (CArray<AILook> template code: 005573F0, 005D8EE0, 005D8B60,
// 005D9DE0), clearing a prepared move (00545B20) and adding a patrol point (005D12B0).
#include "unit_ai.h"

#include "core_maths.h"
#include "originals_ai.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/mfc_array.h"

#include <cmath>
#include <cstring>

namespace {
constexpr float kOne = 1.0f;                        // 0073FEC8 / 0073B84C
constexpr float kHalf = 0.5f;                       // 0073B86C

// CArray growth when the capacity is exceeded: the step (0: an eighth of the size, 4..1024) added
// to the capacity, or the new size when that is more.
int GrownCapacity(int size, int max, int growBy, int wanted) {
    int step = growBy;
    if (step == 0) {
        step = size / 8;
        if (step < 4) {
            step = 4;
        } else if (step > 1024) {
            step = 1024;
        }
    }
    int capacity = max + step;
    return capacity <= wanted ? wanted : capacity;
}

// The element constructor (004CDE10): zero bytes.
void ZeroLooks(AILook* first, int count) { std::memset(first, 0, sizeof(AILook) * static_cast<unsigned>(count)); }
} // namespace

AILookArray* __fastcall LooksInit(AILookArray* looks) {
    looks->vtable = mfc::impl::AiLookArrayClass::Vtable();
    looks->data = nullptr;
    looks->growBy = 0;
    looks->maxSize = 0;
    looks->size = 0;
    return looks;
}
EI_REWRITTEN(0x005D8EE0, AILookArray::SetSize);
EI_REWRITTEN(0x005D8B60, AILookArray::Copy);
EI_REWRITTEN(0x005573F0, LooksInit);

AILook* AILooks::CopyAt(AILook* out, int index) const {
    *out = data[index];
    return out;
}
EI_REWRITTEN(0x005D9DE0, AILooks::CopyAt);

void AIMoveOrder::Clear() {
    point = Vector3{0.0f, 0.0f, 0.0f};
    looks.RemoveAll();
}
EI_REWRITTEN(0x00545B20, AIMoveOrder::Clear);

AIMoveOrder* AIMoveOrder::Assign(const AIMoveOrder* other) {
    point = other->point;
    looks.Copy(other->looks);
    return this;
}
EI_REWRITTEN(0x005D7970, AIMoveOrder::Assign);

void PatrolMode::AppendPoint(const Vector3* position) {
    // Half-cell index of each coordinate (rounded to nearest after a float store), then its middle.
    float cell = static_cast<float>(kOne * kHalf);
    long ix = x87::RoundToInt(static_cast<float>(kOne / cell * position->x));
    long iy = x87::RoundToInt(static_cast<float>(kOne / cell * position->y));
    float x = static_cast<float>((static_cast<float>(ix) + kHalf) * cell);
    float y = static_cast<float>((static_cast<float>(iy) + kHalf) * cell);
    int index = points.size;
    points.SetSize(index + 1, -1);
    PatrolPoint& added = points[index];
    added.position = Vector3{x, y, 0.0f};
    // The original copies an empty temporary look array into the new point.
    added.looks.SetSize(0, -1);
}
EI_REWRITTEN(0x005D12B0, PatrolMode::AppendPoint);

// --- The patrol point array (CArray<PatrolPoint>) ---------------------------------------------------

void PatrolPointElements::Construct(PatrolPoint* points, int count) {
    std::memset(static_cast<void*>(points), 0, sizeof(PatrolPoint) * static_cast<unsigned>(count));
    for (int i = 0; i < count; ++i) points[i].looks.vtable = mfc::impl::AiLookRecordArrayClass::Vtable();
}

void PatrolPointElements::Destruct(PatrolPoint* points, int count) {
    for (int i = 0; i < count; ++i) {
        AILookArray& looks = points[i].looks;
        looks.vtable = mfc::impl::AiLookArrayClass::Vtable();
        if (looks.data) gamecrt::operator_delete(looks.data);
    }
}

PatrolPoint& PatrolPoint::operator=(const PatrolPoint& other) {
    std::memcpy(&position, &other.position, sizeof(Vector3));
    looks.Copy(other.looks);
    return *this;
}

EI_REWRITTEN(0x005D93B0, PatrolPointArray::Add);
EI_REWRITTEN(0x005D9F30, PatrolPointArray::SetAtGrow);
