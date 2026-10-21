#pragma once

#include <cstdarg>

// 00480B80 (A-S8): the text of a DirectDraw / Direct3D / COM result, as the game's error reports
// print it (FatalErrorCode 004800E0 and 00480300), and the thrown error object (A-S10).
// dx_errors.cpp.

// ECX = the result. Every known code's message (each ending in a newline), "No error.\n" for 0,
// "Unrecognized error value.\n" for any other. Ours returns its own copies of the exe's strings.
const char* __fastcall DirectXErrorText(long result);

// The error object the device layer throws (a C++ throw, throw info 00759908; vtable 0073CAB4, its
// base's 0073CAB0, one slot each: the deleting destructor). 0xC bytes from operator new.
struct GameError {
    GameError* DeleteScalarBase(int flags);
    // 004815B0 / 00481580 (slot 0 of 0073CAB4 / 0073CAB0, RET 4): the destructor (the base's inlined),
    // then operator delete when `flags` bit 0 is set; returns the object.
    GameError* DeleteScalar(int flags);
    void* vtable;
    char* message;      // +0x04 operator new copy of the formatted text
    long result;        // +0x08 the DirectX result (set by 00480300 only)
};
// 00480250 (cdecl: the object, a wvsprintf format, its arguments). Returns the object.
GameError* __cdecl GameErrorConstruct(GameError* error, const char* format, ...);
// The two constructors' bodies with the arguments as a va_list (the verify wrappers call them).
GameError* GameErrorFormat(GameError* error, const char* format, va_list args);
GameError* GameErrorFormatResult(GameError* error, long result, const char* format, va_list args);
// The original's `throw new CError(...)`: the object from operator new, constructed by 00480250 /
// 00480300, thrown by pointer (_CxxThrowException, throw info 00759908). WinMain catches it. The
// frames of ours it passes are not unwound member by member as the original's are (what a failed
// constructor had built stays); every such throw ends the game.
[[noreturn]] void ThrowGameError(const char* format, ...);
[[noreturn]] void ThrowGameErrorResult(long result, const char* format, ...);
// 004815D0. The base vtable, the message freed.
void __fastcall GameErrorDestruct(GameError* error);
