// The unit AI's look arrays (CArray<AILook> template code: 005573F0, 005D8EE0, 005D8B60,
// 005D9DE0), clearing a prepared move (00545B20) and adding a patrol point (005D12B0).
#include "unit_ai.h"

#include "core_maths.h"
#include "originals_ai.h"

#include "ei/hook.h"
#include "lib/crt.h"

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
    looks->vtable = reinterpret_cast<void*>(kLookArrayBaseVtable);
    looks->data = nullptr;
    looks->growBy = 0;
    looks->maxSize = 0;
    looks->size = 0;
    return looks;
}
EI_REPLACE(0x005573F0, LooksInit);

// 005D8EE0: SetSize (growBy -1 keeps the step; new elements zeroed, also when they fit the
// capacity; a shrink only lowers the size). 005D8B60: Copy (SetSize to the other's size, then its
// elements; the original copies them with memcpy, the same bytes).
EI_REPLACE_METHOD(0x005D8EE0, AILookArray::SetSize);
EI_REPLACE_METHOD(0x005D8B60, AILookArray::Copy);

AILook* __fastcall LooksElementAt(const AILookArray* looks, void*, AILook* out, int index) {
    *out = looks->data[index];
    return out;
}
EI_REPLACE(0x005D9DE0, LooksElementAt);

void AIMoveOrder::Clear() {
    point = Vector3{0.0f, 0.0f, 0.0f};
    looks.RemoveAll();
}
EI_REPLACE_METHOD(0x00545B20, AIMoveOrder::Clear);

void PatrolMode::AppendPoint(const Vector3* position) {
    // Half-cell index of each coordinate (rounded to nearest after a float store), then its middle.
    float cell = StoreFloat(kOne * kHalf);
    long ix = std::lrint(StoreFloat(kOne / cell * position->x));
    long iy = std::lrint(StoreFloat(kOne / cell * position->y));
    float x = StoreFloat((static_cast<float>(ix) + kHalf) * cell);
    float y = StoreFloat((static_cast<float>(iy) + kHalf) * cell);
    int index = pointCount;
    PatrolPointArraySetSizeOriginal(&pointsVtable, nullptr, index + 1, -1);
    PatrolPoint& added = points[index];
    added.position = Vector3{x, y, 0.0f};
    // The original copies an empty temporary look array into the new point.
    LooksSetSizeAt(&added.looks, nullptr, 0, -1);
}
EI_REPLACE_METHOD(0x005D12B0, PatrolMode::AppendPoint);
