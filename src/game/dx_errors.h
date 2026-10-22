#pragma once

#include <cstdarg>
#include <cstddef>

// 00480B80 (A-S8): the text of a DirectDraw / Direct3D / COM result, as the game's error reports
// print it (FatalErrorCode 004800E0 and 00480300), and the thrown error object (A-S10).
// dx_errors.cpp.

// ECX = the result. Every known code's message (each ending in a newline), "No error.\n" for 0,
// "Unrecognized error value.\n" for any other. Ours returns its own copies of the exe's strings.
const char* __fastcall DirectXErrorText(long result);

// The error object the device layer throws (0xC bytes on the game's heap), by pointer: this compiler's `throw` of a
// CDXException* makes the game's throw information (00759908: the pointer to CDXException, to CGameException and
// to void; C18e), a handler matches by the decorated names. WinMain catches it; every such throw ends the game.
class CGameException {
public:
    // 004815D0 (deleting: 00481580; game.exe's vtable 0073CAB0): the message freed.
    virtual ~CGameException();

    static void* operator new(std::size_t size) noexcept;
    static void operator delete(void* block);

    char* message = nullptr;   // +0x04 operator new copy of the formatted text
    long result;               // +0x08 the DirectX result (set by the result constructor only)

protected:
    CGameException() {}
    CGameException(const CGameException&) = delete;
    CGameException& operator=(const CGameException&) = delete;
};

class CDXException final : public CGameException {
public:
    // 00480250 (cdecl, variadic, in the original): the wvsprintf text of `format`.
    CDXException(const char* format, va_list args);
    // 00480300: "<text> (0x<result>) <the result's text>" (DirectXErrorText), and the result kept.
    CDXException(long result, const char* format, va_list args);
    // 004815B0 (deleting; game.exe's vtable 0073CAB4).
    ~CDXException() override;
};

// The original's `throw new CDXException(...)`: thrown by pointer (null when the game's heap had no room).
[[noreturn]] void ThrowGameError(const char* format, ...);
[[noreturn]] void ThrowGameErrorResult(long result, const char* format, ...);
