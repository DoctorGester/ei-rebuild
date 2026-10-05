// Differential checks for matrix_math.cpp: the original writes into scratch matrices (with the same
// aliasing between output and inputs), ours into the real ones; elements are compared with
// NearlyEqual (rewrite-guide rule 7), the elements a function leaves alone bit for bit. Reached by
// every drawn frame (`world_load`, `newgame`, `render_scene`).
#include "verify.h"

#include "../../src/game/core_streams.h"
#include "../../src/game/model.h"

#include <cstring>

namespace {

void* s_unused;   // registrations without a trampoline

bool SameMatrix(const float* expected, const float* actual) {
    for (int i = 0; i < 16; ++i) {
        if (!ei::verify::NearlyEqual<float>(expected[i], actual[i], 4, 1e-6f)) return false;
    }
    return true;
}

// 004D9880: MOV EDX,[ESP+4] (4), MOV EAX,[ESP+8] (4). An input that is the output is given the
// scratch output in the original's run.
void(__fastcall* s_multiply)(float*, void*, const float*, const float*);
void __fastcall MultiplyVerify(float* out, void*, const float* a, const float* b) {
    float scratchOut[16], scratchA[16], scratchB[16];
    std::memcpy(scratchOut, out, sizeof(scratchOut));
    std::memcpy(scratchA, a, sizeof(scratchA));
    std::memcpy(scratchB, b, sizeof(scratchB));
    s_multiply(scratchOut, nullptr, a == out ? scratchOut : scratchA, b == out ? scratchOut : scratchB);
    MatrixMultiply(out, nullptr, a, b);
    ei::verify::Check("MatrixMultiply", true, SameMatrix(scratchOut, out), "alias a %d b %d", a == out, b == out);
}

// 004D9AA0: SUB ESP,0x20 (3), FLD [ECX+4] (3).
void(__fastcall* s_toMatrix)(const Quaternion*, void*, float*);
void __fastcall ToMatrixVerify(const Quaternion* self, void*, float* out) {
    float expected[16];
    std::memcpy(expected, out, sizeof(expected));
    s_toMatrix(self, nullptr, expected);
    self->ToMatrix(out);
    ei::verify::Check("Quaternion::ToMatrix", true, SameMatrix(expected, out), "q %g %g %g %g", self->w, self->x,
                      self->y, self->z);
}

// 004D9B80: MOV EDX,[ESP+8] (4), MOV EAX,ECX (2). The original runs first; the slot it writes and
// the depth are recorded and put back, then ours runs.
void(__fastcall* s_push)(CTransformPipeline*, void*, const float*, int);
void __fastcall PushVerify(CTransformPipeline* self, void*, const float* m, int index) {
    MatrixStack& stack = self->stacks_[index];
    int count = stack.count;
    float* slot = stack.matrices[count].m;
    float before[16], expected[16];
    std::memcpy(before, slot, sizeof(before));
    s_push(self, nullptr, m, index);
    int expectedCount = stack.count;
    std::memcpy(expected, slot, sizeof(expected));
    std::memcpy(slot, before, sizeof(before));
    stack.count = count;
    self->PushMatrix(m, index);
    ei::verify::Check("CTransformPipeline::PushMatrix", true, expectedCount == stack.count && SameMatrix(expected, slot),
                      "stack %d depth %d", index, count);
}

// 004D9ED0 (a copy of 0047B940): ours only.
void** __fastcall AlignedAllocVerify(int count) {
    void** result = AlignedPointerArrayAlloc(count);
    auto aligned = reinterpret_cast<std::uintptr_t>(result);
    auto block = reinterpret_cast<std::uintptr_t>(reinterpret_cast<void**>(result)[-1]);
    bool ok = (aligned & 0x1F) == 0 && aligned - block >= 4 && aligned - block <= 0x23;
    ei::verify::Check("AlignedPointerArrayAlloc@004D9ED0", true, ok, "count %d", count);
    return result;
}

} // namespace

EI_VERIFY(0x004D9880, MultiplyVerify, 8, s_multiply);
EI_VERIFY(0x004D9AA0, ToMatrixVerify, 6, s_toMatrix);
EI_VERIFY(0x004D9B80, PushVerify, 6, s_push);
EI_VERIFY(0x004D9ED0, AlignedAllocVerify, 0, s_unused);
