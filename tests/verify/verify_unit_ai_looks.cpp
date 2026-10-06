// Checks for the look arrays, clearing a prepared move and adding a patrol point
// (src/game/unit_ai_looks.cpp). Scenario: tests/scenarios/unit_ai.txt (UMPatrolAddPoint; the
// Guard / Patrol / Sentry / Suspection modes clear their move, construct, resize, copy and read
// look arrays through the template's addresses every time they start a move).
#include "verify.h"

#include "../../src/game/unit_ai.h"

#include <cmath>
#include <cstring>
#include <vector>

namespace {
void* s_unused;

bool LooksZero(const AILook* first, int count) {
    for (int i = 0; i < count; ++i) {
        static const AILook kZero{};
        if (std::memcmp(&first[i], &kZero, sizeof(AILook)) != 0) return false;
    }
    return true;
}
} // namespace

// 005573F0 Init: MOV EAX,ECX; XOR ECX,ECX; MOV [EAX],imm32 (10 bytes). Output: the original
// constructs a scratch array, ours the real one; all 0x14 bytes and the returned pointers compared.
static AILookArray*(__fastcall* s_init)(AILookArray*);
static AILookArray* __fastcall InitVerify(AILookArray* self) {
    AILookArray scratch;
    std::memset(&scratch, 0xCD, sizeof(scratch));
    AILookArray* theirs = s_init(&scratch);
    AILookArray* ours = self->Init();
    ei::verify::Check("AILookArray::Init", true, theirs == &scratch && ours == self && std::memcmp(&scratch, self, sizeof(scratch)) == 0,
                      "array %p", self);
    return ours;
}
EI_VERIFY(0x005573F0, InitVerify, 10, s_init);

// 005D8EE0 Resize: allocates and frees, so ours only against a model taken before: the kept
// elements unchanged, the new ones zero, size, step and a capacity that holds the size.
static void __fastcall ResizeVerify(AILookArray* self, void*, int size, int growBy) {
    int kept = size < self->size ? size : self->size;
    std::vector<AILook> before(self->data ? self->data : nullptr, self->data ? self->data + kept : nullptr);
    int step = growBy == -1 ? self->growBy : growBy;
    self->Resize(size, growBy);
    bool ok = self->size == size && self->growBy == step && self->max >= size && (size == 0) == (self->data == nullptr) &&
              (size == 0 || self->max >= 1);
    if (ok && kept > 0) ok = std::memcmp(self->data, before.data(), sizeof(AILook) * kept) == 0;
    if (ok && size > kept) ok = LooksZero(self->data + kept, size - kept);
    ei::verify::Check("AILookArray::Resize", true, ok, "array %p: size %d (max %d, step %d), asked %d", self, self->size, self->max,
                      self->growBy, size);
}
EI_VERIFY(0x005D8EE0, ResizeVerify, 0, s_unused);

// 005D8B60 CopyFrom: ours only; the same size and elements as the source afterwards.
static void __fastcall CopyFromVerify(AILookArray* self, void*, const AILookArray* other) {
    self->CopyFrom(other);
    bool ok = self->size == other->size && self->max >= self->size &&
              (other->size <= 0 || std::memcmp(self->data, other->data, sizeof(AILook) * other->size) == 0);
    ei::verify::Check("AILookArray::CopyFrom", true, ok, "array %p from %p: size %d vs %d", self, other, self->size, other->size);
}
EI_VERIFY(0x005D8B60, CopyFromVerify, 0, s_unused);

// 005D9DE0 ElementAt: MOV EAX,[ESP+8]; MOV EDX,[ECX+4] (7 bytes). Pure: the original into a
// scratch element, ours into `out`; bytes and the returned pointers compared.
static AILook*(__fastcall* s_elementAt)(const AILookArray*, void*, AILook*, int);
static AILook* __fastcall ElementAtVerify(const AILookArray* self, void*, AILook* out, int index) {
    AILook scratch;
    AILook* theirs = s_elementAt(self, nullptr, &scratch, index);
    AILook* ours = self->ElementAt(out, index);
    ei::verify::Check("AILookArray::ElementAt", true, theirs == &scratch && ours == out && std::memcmp(&scratch, out, sizeof(AILook)) == 0,
                      "array %p index %d", self, index);
    return ours;
}
EI_VERIFY(0x005D9DE0, ElementAtVerify, 7, s_elementAt);

// 00545B20 AIMoveOrder::Clear: frees the looks, so ours only: point zero, looks empty, the step
// and vtable unchanged.
static void __fastcall ClearMoveVerify(AIMoveOrder* self) {
    void* vtable = self->looks.vtable;
    int step = self->looks.growBy;
    self->Clear();
    bool ok = self->point.x == 0.0f && self->point.y == 0.0f && self->point.z == 0.0f && !self->looks.data && self->looks.size == 0 &&
              self->looks.max == 0 && self->looks.vtable == vtable && self->looks.growBy == step;
    ei::verify::Check("AIMoveOrder::Clear", true, ok, "order %p", self);
}
EI_VERIFY(0x00545B20, ClearMoveVerify, 0, s_unused);

// 005D12B0 PatrolMode::AppendPoint: grows the point array, so ours only: one point more, the
// earlier ones unchanged, the new one at (n + 0.5) / 2 (2x - 0.5 whole) 0..0.5 above
// the asked point in x and y (n is 2x rounded to nearest), z 0, no looks.
static void __fastcall AppendPointVerify(PatrolMode* self, void*, const Vector3* position) {
    int count = self->pointCount;
    std::vector<Vector3> before;
    for (int i = 0; i < count; ++i) before.push_back(self->points[i].position);
    Vector3 asked = *position;
    self->AppendPoint(position);
    bool ok = self->pointCount == count + 1;
    for (int i = 0; ok && i < count; ++i) ok = std::memcmp(&before[i], &self->points[i].position, sizeof(Vector3)) == 0;
    if (ok) {
        const PatrolPoint& added = self->points[count];
        auto onGrid = [](float v, float to) {
            float twice = v * 2.0f - 0.5f;
            return twice == std::floor(twice) && v - to >= -1e-3f && v - to <= 0.501f;
        };
        ok = onGrid(added.position.x, asked.x) && onGrid(added.position.y, asked.y) && added.position.z == 0.0f && added.looks.size == 0;
    }
    ei::verify::Check("PatrolMode::AppendPoint", true, ok, "mode %p: %d points (was %d), asked (%f, %f)", self, self->pointCount, count,
                      asked.x, asked.y);
}
EI_VERIFY(0x005D12B0, AppendPointVerify, 0, s_unused);
