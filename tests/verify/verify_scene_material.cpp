// Checks for scene_material.cpp: side by side (only the two outputs are written; a fatal error for
// an unknown name would be reported twice, by each version).
#include "verify.h"

namespace scene {
int __fastcall ParseMaterial(const char* name, int* texture, int* material);
}

namespace {
// 0044A2A0: PUSH -1 (2), PUSH imm32 (5).
int(__fastcall* s_parse)(const char*, int*, int*);
int __fastcall ParseVerify(const char* name, int* texture, int* material) {
    int expectedTexture = *texture, expectedMaterial = *material;
    int expected = s_parse(name, &expectedTexture, &expectedMaterial);
    int result = scene::ParseMaterial(name, texture, material);
    ei::verify::Check("ParseMaterial", true,
                      expected == result && expectedTexture == *texture && expectedMaterial == *material, "%s", name);
    return result;
}
} // namespace

EI_VERIFY(0x0044A2A0, ParseVerify, 7, s_parse);
