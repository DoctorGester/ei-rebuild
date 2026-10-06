// Checks for view_matrices.cpp: the original into scratch outputs, ours into the real ones, floats
// with NearlyEqual (rule 7). CameraViewMatrix runs every drawn frame (00569E40 through 00566360's
// camera; `newgame`, `world_load`); LightRotation / LightShearMatrix with every shadow (DrawShadow
// 0051CE60; `newgame`); the cubic copy from the unit motion code (ai_motion.cpp; any scenario with
// moving units, `newgame`); AllocateBlocks32 from the weather and terrain draws (ours only: alignment
// and the stored block pointer).
#include "verify.h"

#include "../../src/game/core_maths.h"
#include "../../src/game/core_transform.h"
#include "../../src/game/view_matrices.h"

#include <cstring>

namespace {

bool SameMatrix(const float* expected, const float* actual) {
    for (int i = 0; i < 16; ++i) {
        if (!ei::verify::NearlyEqual<float>(expected[i], actual[i], 4, 1e-6f)) return false;
    }
    return true;
}

// 004F0EF0: SUB ESP,0x30 (3), PUSH ESI, PUSH EDI.
void(__stdcall* s_cameraView)(float*, const CTransformObject*);
void __stdcall CameraViewMatrixVerify(float* out, const CTransformObject* camera) {
    float expected[16];
    std::memcpy(expected, out, sizeof(expected));
    s_cameraView(expected, camera);
    view::CameraViewMatrix(out, camera);
    const Quaternion& q = camera->localRotation_;
    ei::verify::Check("view::CameraViewMatrix", true, SameMatrix(expected, out), "q %g %g %g %g", q.w, q.x, q.y, q.z);
}

// 004F15C0: SUB ESP,0x18 (3), MOV EAX,[ESP+0x1C] (4).
void(__stdcall* s_lightRotation)(const Vector3*, Quaternion*);
void __stdcall LightRotationVerify(const Vector3* direction, Quaternion* out) {
    Quaternion expected = *out;
    s_lightRotation(direction, &expected);
    view::LightRotation(direction, out);
    bool ok = ei::verify::NearlyEqual<float>(expected.w, out->w, 4, 1e-6f) &&
              ei::verify::NearlyEqual<float>(expected.x, out->x, 4, 1e-6f) &&
              ei::verify::NearlyEqual<float>(expected.y, out->y, 4, 1e-6f) &&
              ei::verify::NearlyEqual<float>(expected.z, out->z, 4, 1e-6f);
    ei::verify::Check("view::LightRotation", true, ok, "dir %g %g", direction->x, direction->y);
}

// 004F1440: PUSH EBP, MOV EBP,ESP (2), SUB ESP,0x64 (3).
void(__stdcall* s_lightShear)(float*, const Vector3*);
void __stdcall LightShearMatrixVerify(float* out, const Vector3* direction) {
    float expected[16];
    std::memcpy(expected, out, sizeof(expected));
    s_lightShear(expected, direction);
    view::LightShearMatrix(out, direction);
    ei::verify::Check("view::LightShearMatrix", true, SameMatrix(expected, out), "dir %g %g %g", direction->x,
                      direction->y, direction->z);
}

// 004E5710: SHL ECX,5 (3), ADD ECX,0x24 (3). Ours only.
void* s_unused;
std::uint8_t* __fastcall AllocateBlocks32Verify(int count) {
    std::uint8_t* result = view::AllocateBlocks32(count);
    auto aligned = reinterpret_cast<std::uintptr_t>(result);
    auto block = reinterpret_cast<std::uintptr_t>(reinterpret_cast<void**>(result)[-1]);
    bool ok = (aligned & 0x1F) == 0 && aligned - block >= 4 && aligned - block <= 0x23;
    ei::verify::Check("view::AllocateBlocks32", true, ok, "count %d", count);
    return result;
}

// 004E5660: FLD [ESP+4] (4), FMUL [ECX+0xC] (3).
float(__fastcall* s_cubic)(const CubicSegment*, void*, float);
float __fastcall CubicVerify(const CubicSegment* self, void*, float t) {
    float expected = s_cubic(self, nullptr, t);
    float actual = self->Evaluate(t);
    ei::verify::CheckNear("CubicSegment::Evaluate@004E5660", expected, actual, "t %g", t);
    return actual;
}

} // namespace

EI_VERIFY(0x004F0EF0, CameraViewMatrixVerify, 5, s_cameraView);
EI_VERIFY(0x004F15C0, LightRotationVerify, 7, s_lightRotation);
EI_VERIFY(0x004F1440, LightShearMatrixVerify, 6, s_lightShear);
EI_VERIFY(0x004E5710, AllocateBlocks32Verify, 0, s_unused);
EI_VERIFY(0x004E5660, CubicVerify, 7, s_cubic);
