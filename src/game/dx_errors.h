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
// DXException* makes the game's throw information (the pointer to DXException, to GameException and to void), a
// handler matches by the decorated names. WinMain catches it; every such throw ends the game.
class GameException {
public:
    virtual ~GameException();


    std::string message;       // the formatted text

protected:
    GameException() {}
    GameException(const GameException&) = delete;
    GameException& operator=(const GameException&) = delete;
};

class DXException final : public GameException {
public:
    // The text of `format` (printf's: the callers use only %s and %d), cut to 1023 bytes.
    DXException(const char* format, va_list args);
    ~DXException() override;
};

// `throw new DXException(...)`: thrown by pointer (null when the game's heap had no room).
[[noreturn]] void ThrowGameError(const char* format, ...);
