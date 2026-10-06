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

AILookArray* AILookArray::Init() {
    vtable = reinterpret_cast<void*>(kLookArrayBaseVtable);
    data = nullptr;
    growBy = 0;
    max = 0;
    size = 0;
    return this;
}
EI_REPLACE_METHOD(0x005573F0, AILookArray::Init);

void AILookArray::Resize(int newSize, int newGrowBy) {
    if (newGrowBy != -1) growBy = newGrowBy;
    if (newSize == 0) {
        if (data) {
            gamecrt::operator_delete(data);
            data = nullptr;
        }
        max = 0;
        size = 0;
        return;
    }
    if (!data) {
        data = static_cast<AILook*>(gamecrt::operator_new(static_cast<unsigned>(newSize) << 4));
        ZeroLooks(data, newSize);
        max = newSize;
        size = newSize;
        return;
    }
    if (newSize <= max) {
        if (size < newSize) ZeroLooks(data + size, newSize - size);
        size = newSize;
        return;
    }
    int capacity = GrownCapacity(size, max, growBy, newSize);
    auto* grown = static_cast<AILook*>(gamecrt::operator_new(static_cast<unsigned>(capacity) << 4));
    std::memcpy(grown, data, sizeof(AILook) * static_cast<unsigned>(size));
    ZeroLooks(grown + size, newSize - size);
    gamecrt::operator_delete(data);
    size = newSize;
    data = grown;
    max = capacity;
}
EI_REPLACE_METHOD(0x005D8EE0, AILookArray::Resize);

void AILookArray::CopyFrom(const AILookArray* other) {
    // The inlined SetSize(other->size, -1).
    Resize(other->size, -1);
    if (other->size > 0) std::memcpy(data, other->data, sizeof(AILook) * static_cast<unsigned>(other->size));
}
EI_REPLACE_METHOD(0x005D8B60, AILookArray::CopyFrom);

AILook* AILookArray::ElementAt(AILook* out, int index) const {
    *out = data[index];
    return out;
}
EI_REPLACE_METHOD(0x005D9DE0, AILookArray::ElementAt);

void AIMoveOrder::Clear() {
    point = Vector3{0.0f, 0.0f, 0.0f};
    if (looks.data) {
        gamecrt::operator_delete(looks.data);
        looks.data = nullptr;
    }
    looks.max = 0;
    looks.size = 0;
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
    added.looks.SetSize(0, -1);
}
EI_REPLACE_METHOD(0x005D12B0, PatrolMode::AppendPoint);
