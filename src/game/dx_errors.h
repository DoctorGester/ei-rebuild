#pragma once

#include <cstdarg>
#include <cstddef>
#include <string>

// The text of a DirectDraw / Direct3D / COM result, as the game's error reports print it (FatalErrorCode), and the
// thrown error object. See dx_errors.cpp.

// The result's message: every known code's message (each ending in a newline), "No error.\n" for 0,
// "Unrecognized error value.\n" for any other. Returns its own copies of the exe's strings.
const char* DirectXErrorText(long result);

// The error object the device layer throws (0xC bytes on the game's heap), by pointer: this compiler's `throw` of a
// CDXException* makes the game's throw information (the pointer to CDXException, to CGameException and to void), a
// handler matches by the decorated names. WinMain catches it; every such throw ends the game.
class CGameException {
public:
    virtual ~CGameException();


    std::string message;       // the formatted text

protected:
    CGameException() {}
    CGameException(const CGameException&) = delete;
    CGameException& operator=(const CGameException&) = delete;
};

class CDXException final : public CGameException {
public:
    // The wvsprintf text of `format` (variadic).
    CDXException(const char* format, va_list args);
    ~CDXException() override;
};

// `throw new CDXException(...)`: thrown by pointer (null when the game's heap had no room).
[[noreturn]] void ThrowGameError(const char* format, ...);
