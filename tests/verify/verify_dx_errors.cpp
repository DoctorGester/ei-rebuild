// Checks for dx_errors.cpp (A-S8). 00480B80 is pure: the original's text and ours compared by
// content (ours are copies). Reached only when the game reports a DirectX failure (FatalErrorCode
// 004800E0, 00480300); no scenario is known to reach it.
#include "verify.h"

#include "../../src/game/dx_errors.h"
#include "../../src/lib/crt.h"

#include <cstdarg>
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

// The error object (A-S10), thrown by the device layer when a surface or device call fails (no
// scenario). The constructors against the original: it formats into a scratch object with the
// first eight argument dwords passed on (cdecl, so the unused ones are harmless; wvsprintf takes
// no doubles), ours into the real one; the messages compared by content and the scratch message
// freed. The destructors: ours only.
namespace {

constexpr int kPassedDwords = 8;

// 00480250 / 00480300: PUSH -1; PUSH imm32 (7).
using ConstructFn = GameError*(__cdecl*)(GameError*, const char*, ...);
using ConstructResultFn = GameError*(__cdecl*)(GameError*, long, const char*, ...);
ConstructFn s_construct;
ConstructResultFn s_constructResult;

struct Dwords {
    int v[kPassedDwords];
};
Dwords Collect(va_list args) {
    Dwords d{};
    for (int& x : d.v) x = va_arg(args, int);
    return d;
}

bool SameError(const GameError& expected, const GameError& actual, bool withResult) {
    return expected.vtable == actual.vtable && expected.message && actual.message &&
           std::strcmp(expected.message, actual.message) == 0 && (!withResult || expected.result == actual.result);
}

GameError* __cdecl ConstructVerify(GameError* self, const char* format, ...) {
    va_list args;
    va_start(args, format);
    Dwords d = Collect(args);
    va_end(args);
    GameError scratch{};
    s_construct(&scratch, format, d.v[0], d.v[1], d.v[2], d.v[3], d.v[4], d.v[5], d.v[6], d.v[7]);
    va_start(args, format);
    GameErrorFormat(self, format, args);
    va_end(args);
    ei::verify::Check("GameErrorConstruct", true, SameError(scratch, *self, false), "\"%s\"", self->message);
    gamecrt::operator_delete(scratch.message);
    return self;
}

GameError* __cdecl ConstructResultVerify(GameError* self, long result, const char* format, ...) {
    va_list args;
    va_start(args, format);
    Dwords d = Collect(args);
    va_end(args);
    GameError scratch{};
    s_constructResult(&scratch, result, format, d.v[0], d.v[1], d.v[2], d.v[3], d.v[4], d.v[5], d.v[6], d.v[7]);
    va_start(args, format);
    GameErrorFormatResult(self, result, format, args);
    va_end(args);
    ei::verify::Check("GameErrorConstructResult", true, SameError(scratch, *self, true), "\"%s\"", self->message);
    gamecrt::operator_delete(scratch.message);
    return self;
}

void* s_unusedDestruct;
void __fastcall DestructVerify(GameError* self) {
    GameErrorDestruct(self);
    ei::verify::Check("GameErrorDestruct", true, self->vtable == reinterpret_cast<void*>(0x0073CAB0), "error %p",
                      static_cast<void*>(self));
}
void* s_unusedDeleting;
GameError* __fastcall DeletingVerify(GameError* self, void*, int flags) {
    GameError* result = self->DeleteScalar(flags);
    ei::verify::Check("GameErrorDeletingDestruct", true, result == self, "flags %d", flags);
    return result;
}
void* s_unusedBaseDeleting;
GameError* __fastcall BaseDeletingVerify(GameError* self, void*, int flags) {
    GameError* result = self->DeleteScalarBase(flags);
    ei::verify::Check("GameErrorBaseDeletingDestruct", true, result == self, "flags %d", flags);
    return result;
}

} // namespace

EI_VERIFY(0x00480250, ConstructVerify, 7, s_construct);
EI_VERIFY(0x00480300, ConstructResultVerify, 7, s_constructResult);
EI_VERIFY(0x004815D0, DestructVerify, 0, s_unusedDestruct);
EI_VERIFY(0x004815B0, DeletingVerify, 0, s_unusedDeleting);
EI_VERIFY(0x00481580, BaseDeletingVerify, 0, s_unusedBaseDeleting);
