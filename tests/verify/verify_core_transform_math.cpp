// Checks for core_transform_math.cpp. Each function runs side by side: the original on a scratch
// copy of the object (they only read and write the object and call pure maths helpers), ours on
// the real one; the local rotation (or the matrix) must match bit for bit.
#include "net_verify.h"

#include "../../src/game/core_transform.h"

#include <cstring>

namespace {

using Scratch = unsigned char[sizeof(CTransformObject)];

CTransformObject* CopyOf(const CTransformObject* self, Scratch& scratch) {
    std::memcpy(scratch, self, sizeof(Scratch));
    return reinterpret_cast<CTransformObject*>(scratch);
}

bool SameRotation(const CTransformObject* a, const CTransformObject* b) {
    return std::memcmp(&a->localRotation_, &b->localRotation_, sizeof(Quaternion)) == 0;
}

// 00401570: PUSH EBP (1), MOV EBP,ESP (2), SUB ESP,18 (3).
void(__fastcall* s_matrix)(const CTransformObject*, void*, float*);
void __fastcall MatrixVerify(const CTransformObject* self, void*, float* m) {
    float expected[16];
    s_matrix(self, nullptr, expected);
    self->WorldMatrix(m);
    ei::verify::Check("CTransformObject::WorldMatrix", true, std::memcmp(expected, m, sizeof(expected)) == 0, "");
}

// 00401680: SUB ESP,28 (3), FLD [ECX+3C] (3).
void(__fastcall* s_tilt)(CTransformObject*, void*, const Vector3*);
void __fastcall TiltVerify(CTransformObject* self, void*, const Vector3* target) {
    Scratch scratch;
    CTransformObject* copy = CopyOf(self, scratch);
    s_tilt(copy, nullptr, target);
    self->TiltTowards(*target);
    ei::verify::Check("CTransformObject::TiltTowards", true, SameRotation(copy, self), "");
}

// 00401940: SUB ESP,34 (3), FLD [ECX+34] (3).
void(__fastcall* s_turn)(CTransformObject*, void*, const Vector3*);
void __fastcall TurnVerify(CTransformObject* self, void*, const Vector3* target) {
    Scratch scratch;
    CTransformObject* copy = CopyOf(self, scratch);
    s_turn(copy, nullptr, target);
    self->TurnTowards(*target);
    ei::verify::Check("CTransformObject::TurnTowards", true, SameRotation(copy, self), "");
}

// 00401B50: SUB ESP,3C (3), PUSH ESI (1), MOV ESI,ECX (2).
void(__fastcall* s_roll)(CTransformObject*, void*, const Vector3*);
void __fastcall RollVerify(CTransformObject* self, void*, const Vector3* dir) {
    Scratch scratch;
    CTransformObject* copy = CopyOf(self, scratch);
    s_roll(copy, nullptr, dir);
    self->RollTowards(*dir);
    ei::verify::Check("CTransformObject::RollTowards", true, SameRotation(copy, self), "");
}

// 00401E50: SUB ESP,10 (3), MOV EAX,[ESP+14] (4).
void(__fastcall* s_rotate)(CTransformObject*, void*, const Vector3*, float);
void __fastcall RotateVerify(CTransformObject* self, void*, const Vector3* axis, float angle) {
    Scratch scratch;
    CTransformObject* copy = CopyOf(self, scratch);
    s_rotate(copy, nullptr, axis, angle);
    self->Rotate(*axis, angle);
    ei::verify::Check("CTransformObject::Rotate", true, SameRotation(copy, self), "");
}

// 00401F00: SUB ESP,3C (3), PUSH ESI (1), MOV ESI,ECX (2).
void(__fastcall* s_up)(CTransformObject*, void*, const Vector3*, int);
void __fastcall UpVerify(CTransformObject* self, void*, const Vector3* dir, int reduced) {
    Scratch scratch;
    CTransformObject* copy = CopyOf(self, scratch);
    s_up(copy, nullptr, dir, reduced);
    self->TurnUpTowards(*dir, reduced);
    ei::verify::Check("CTransformObject::TurnUpTowards", true, SameRotation(copy, self), "");
}

// 00402220: SUB ESP,1C (3), MOV EAX,[ESP+20] (4).
void(__fastcall* s_heading)(CTransformObject*, void*, const float*);
void __fastcall HeadingVerify(CTransformObject* self, void*, const float* dir) {
    Scratch scratch;
    CTransformObject* copy = CopyOf(self, scratch);
    s_heading(copy, nullptr, dir);
    self->SetHeading(dir);
    ei::verify::Check("CTransformObject::SetHeading", true, SameRotation(copy, self), "");
}

} // namespace

EI_VERIFY(0x00401570, MatrixVerify, 6, s_matrix);
EI_VERIFY(0x00401680, TiltVerify, 6, s_tilt);
EI_VERIFY(0x00401940, TurnVerify, 6, s_turn);
EI_VERIFY(0x00401B50, RollVerify, 6, s_roll);
EI_VERIFY(0x00401E50, RotateVerify, 7, s_rotate);
EI_VERIFY(0x00401F00, UpVerify, 6, s_up);
EI_VERIFY(0x00402220, HeadingVerify, 7, s_heading);
