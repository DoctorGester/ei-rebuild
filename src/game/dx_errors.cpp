// DirectX result texts; see dx_errors.h. The table is the game's (its comparison tree flattened; the texts copied
// from the exe's strings) without its DirectDraw and Direct3D codes (0x887xxxxx: nothing calls DirectDraw now).
#include "dx_errors.h"

#include "lib/crt.h"
#include "lib/gameapi.h"

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <iterator>

namespace {

struct ErrorText {
    std::uint32_t result;
    const char* text;
};

// Sorted by result (unsigned).
constexpr ErrorText kErrorTexts[] = {
    {0x00000000, "No error.\n"},
    {0x80004001, "Action not supported.\n"},
    {0x80004005, "Generic failure.\n"},
    {0x80070005, "Another application has a higher priority level, preventing this call from succeeding\n"},
    {0x8007000C, "The operation cannot be performed unless the device is acquired\n"},
    {0x8007000E, "DirectDraw does not have enough memory to perform the operation.\n"},
    {0x80070015, "This object has not been initialized\n"},
    {0x8007001E, "Access to the input device has been lost. It must be reacquired\n"},
    {0x80070057, "One or more of the parameters passed to the function are incorrect.\n"},
};

constexpr const char* kUnrecognized = "Unrecognized error value.\n";

// The stack buffer (wvsprintf writes at most 1024 characters).
constexpr std::size_t kBuffer = 0x400;

// The message: an operator new copy of `text`.
char* CopyMessage(const char* text) {
    std::size_t size = std::strlen(text) + 1;
    auto* message = new char[size];
    std::memcpy(message, text, size);
    return message;
}

} // namespace

const char* DirectXErrorText(long result) {
    const std::uint32_t key = static_cast<std::uint32_t>(result);
    const ErrorText* found = std::lower_bound(std::begin(kErrorTexts), std::end(kErrorTexts), key,
                                              [](const ErrorText& e, std::uint32_t k) { return e.result < k; });
    return found != std::end(kErrorTexts) && found->result == key ? found->text : kUnrecognized;
}

// ---- The thrown error object ---------------------------------------------------


CGameException::~CGameException() {
    if (message) delete[] message;
}

CDXException::CDXException(const char* format, va_list args) {
    char text[kBuffer];
    gameapi::wvsprintfA(text, format, args);
    message = CopyMessage(text);
}

CDXException::~CDXException() = default;

void ThrowGameError(const char* format, ...) {
    va_list args;
    va_start(args, format);
    CDXException* error = new CDXException(format, args);
    va_end(args);
    throw error;
}
