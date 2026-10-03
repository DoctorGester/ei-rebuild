// Checks for the application helpers (src/game/app.cpp). They drive the window, the interface and
// the menu, so only ours runs (stolenBytes 0) and the visible effects are checked.
#include "verify.h"

#include "../../src/game/app.h"

#include <cstdint>

namespace {
void* s_unused;

std::uint8_t ActiveFlag() { return *reinterpret_cast<std::uint8_t*>(0x0079BC5C); }
std::uint8_t ActivationSeen() { return *reinterpret_cast<std::uint8_t*>(0x0079BC5D); }

// 00457D40. WM_ACTIVATE: minimised or inactive clears the active flag, states 1 and 2 set it,
// other states change nothing; every handled message returns 1 (screen saver / monitor power 0).
LRESULT __stdcall WindowProcVerify(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    std::uint8_t activeBefore = ActiveFlag(), seenBefore = ActivationSeen();
    LRESULT result = app::WindowProc(window, message, wParam, lParam);
    bool ok = true;
    switch (message) {
    case WM_ACTIVATE: {
        unsigned state = wParam & 0xFFFF;
        if ((wParam >> 16) != 0 || state == 0) ok = ActiveFlag() == 0 && ActivationSeen() == 1;
        else if (state <= 2) ok = ActiveFlag() == 1 && ActivationSeen() == 1;
        else ok = ActiveFlag() == activeBefore && ActivationSeen() == seenBefore;
        ok = ok && result == 1;
        break;
    }
    case WM_PAINT:
    case WM_DESTROY:
    case WM_SETCURSOR:
        ok = result == 1;
        break;
    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_SCREENSAVE || (wParam & 0xFFF0) == SC_MONITORPOWER) ok = result == 0;
        break;
    }
    ei::verify::Check("app::WindowProc", true, ok, "message 0x%X wParam 0x%X result %d", message, unsigned(wParam),
                      int(result));
    return result;
}

bool IsInput(UINT m) {
    return (m >= WM_KEYDOWN && m <= WM_SYSKEYUP && m != WM_DEADCHAR) || (m >= WM_MOUSEMOVE && m <= WM_MOUSEWHEEL) ||
           m == 0xCC54;
}

// 00457970. Input messages are handled (1); the rest is what the window procedure returned.
LRESULT __fastcall DispatchInputVerify(UINT message, WPARAM wParam, LPARAM lParam) {
    LRESULT result = app::DispatchInput(message, wParam, lParam);
    bool ok = !IsInput(message) || result == 1;
    ei::verify::Check("app::DispatchInput", true, ok, "message 0x%X result %d", message, int(result));
    return result;
}

// 00458220. Nothing to compare (the messages are consumed); counted only.
void __cdecl PumpMessagesVerify() {
    app::PumpMessages();
    ei::verify::Check("app::PumpMessages", true, true, "");
}

// 00456140. Without a command-line map (the harness) the menu opens and the result is 1.
int __cdecl StartFirstMapVerify() {
    const char* mode = *reinterpret_cast<const char* const*>(0x0079BA14);
    bool menu = mode[0] == '\0';
    int result = app::StartFirstMap();
    ei::verify::Check("app::StartFirstMap", true, !menu || result == 1, "result %d", result);
    return result;
}

} // namespace

EI_VERIFY(0x00457D40, WindowProcVerify, 0, s_unused);
EI_VERIFY(0x00457970, DispatchInputVerify, 0, s_unused);
EI_VERIFY(0x00458220, PumpMessagesVerify, 0, s_unused);
EI_VERIFY(0x00456140, StartFirstMapVerify, 0, s_unused);
