// Checks for the unit shapes (src/game/ai_unit_shape.cpp). All differential: the in-place ones
// run the original on a copy of the shape, the creating ones compare the two new shapes' bytes
// (the original's is then freed: it has no references and nothing to destroy). Reached when a
// unit of a new radius first enters the AI map (every world load); Empty / Copy only when a
// shared shape is shut down or shared copy-on-share.
#include "verify.h"

#include "../../src/game/ai_unit_layer.h"
#include "../../src/lib/crt.h"

#include <cstring>

namespace {
bool SameShape(const CUnitShape* a, const CUnitShape* b) {
    return std::memcmp(static_cast<const void*>(a), static_cast<const void*>(b), sizeof(CUnitShape)) == 0;
}
int DifferentLevels(const CUnitShape* a, const CUnitShape* b) {
    int count = 0;
    for (int i = 0; i < 16; ++i)
        for (int j = 0; j < 16; ++j) count += a->levels[i][j] != b->levels[i][j];
    return count;
}
struct ShapeBytes {
    unsigned char bytes[sizeof(CUnitShape)];
    CUnitShape* Shape() { return reinterpret_cast<CUnitShape*>(bytes); }
};
} // namespace

// 005B3440: PUSH EBP; MOV EBP,ESP; SUB ESP,8 (6).
static CUnitShape*(__fastcall* s_construct)(CUnitShape*, void*, float);
static CUnitShape* __fastcall ConstructVerify(CUnitShape* self, void*, float radius) {
    ShapeBytes probe;
    std::memcpy(probe.bytes, static_cast<void*>(self), sizeof(probe.bytes));
    CUnitShape* expected = s_construct(probe.Shape(), nullptr, radius);
    CUnitShape* actual = self->Construct(radius);
    ei::verify::Check("CUnitShape::Construct", true,
                      expected == probe.Shape() && actual == self && SameShape(probe.Shape(), self),
                      "radius %g: %d levels differ", static_cast<double>(radius), DifferentLevels(probe.Shape(), self));
    return actual;
}
EI_VERIFY(0x005B3440, ConstructVerify, 6, s_construct);

// 005B3540: MOV EDX,ECX; TEST EDX,EDX; MOV [EDX],imm32 (10).
static int(__fastcall* s_empty)(CUnitShape*);
static int __fastcall EmptyVerify(CUnitShape* self) {
    ShapeBytes probe;
    std::memcpy(probe.bytes, static_cast<void*>(self), sizeof(probe.bytes));
    int expected = s_empty(probe.Shape());
    int actual = self->Empty();
    ei::verify::Check("CUnitShape::Empty", true, expected == actual && SameShape(probe.Shape(), self), "%d vs %d",
                      expected, actual);
    return actual;
}
EI_VERIFY(0x005B3540, EmptyVerify, 10, s_empty);

// 005B34F0: PUSH EBX; MOV EBX,ECX; PUSH 0x10C (8).
static CUnitShape*(__fastcall* s_copy)(CUnitShape*);
static CUnitShape* __fastcall CopyVerify(CUnitShape* self) {
    CUnitShape* expected = s_copy(self);
    CUnitShape* actual = self->Copy();
    ei::verify::Check("CUnitShape::Copy", true, expected && actual && SameShape(expected, actual), "radius %g",
                      static_cast<double>(self->radius));
    gamecrt::operator_delete(expected);
    return actual;
}
EI_VERIFY(0x005B34F0, CopyVerify, 8, s_copy);

// 005B33D0: PUSH -1; PUSH imm32 (7).
static CUnitShape*(__fastcall* s_create)(void*, void*, const UnitShapeHandle*);
static CUnitShape* __fastcall CreateVerify(void* cache, void*, const UnitShapeHandle* handle) {
    CUnitShape* expected = s_create(cache, nullptr, handle);
    CUnitShape* actual = CreateUnitShape(cache, nullptr, handle);
    ei::verify::Check("CreateUnitShape", true, expected && actual && SameShape(expected, actual),
                      "radius %g: %d levels differ", static_cast<double>(handle->height),
                      expected && actual ? DifferentLevels(expected, actual) : -1);
    gamecrt::operator_delete(expected);
    return actual;
}
EI_VERIFY(0x005B33D0, CreateVerify, 7, s_create);
