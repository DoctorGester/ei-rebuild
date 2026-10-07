// Checks for dx_errors.cpp (A-S8). 00480B80 is pure: the original's text and ours compared by
// content (ours are copies). Reached only when the game reports a DirectX failure (FatalErrorCode
// 004800E0, 00480300); no scenario is known to reach it.
#include "verify.h"

#include "../../src/game/dx_errors.h"

#include <cstring>

// 00480B80: CMP ECX,imm32 (6 bytes).
static const char* (__fastcall* s_dxErrorText)(long);
static const char* __fastcall DirectXErrorTextVerify(long result) {
    const char* expected = s_dxErrorText(result);
    const char* actual = DirectXErrorText(result);
    ei::verify::Check("DirectXErrorText", true, std::strcmp(expected, actual) == 0, "%08lX", result);
    return actual;
}
EI_VERIFY(0x00480B80, DirectXErrorTextVerify, 6, s_dxErrorText);
