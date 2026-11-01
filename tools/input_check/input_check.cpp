// Offline check of the SDL backend's keys (src/platform/sdl/sdl_keys.cpp) against what Windows' window procedure got:
// for every keyboard layout this machine has (none loaded or unloaded), every key's virtual-key code with Num
// Lock off and on against the layout's own (MapVirtualKeyExW, as the key messages carried it), and the control
// characters TranslateMessage made (Ctrl+letter, Backspace, Tab, Enter, Escape, Ctrl+Backspace, Ctrl+Enter) against
// ToUnicodeEx. The mouse's translation is SDL's coordinates and buttons as Windows' MK_ bits (no layout): read, not
// checked here. Exit code 1 on a difference. build_checks.py runs it.
#include "platform/sdl/sdl_keys.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using platform::sdl_keys::KeyCode;

HKL g_layout = nullptr;

int LayoutKeyOf(int scan) {
    const UINT code = static_cast<UINT>(scan & 0xFF) | ((scan & 0x100) ? 0xE000u : 0u);
    return static_cast<int>(MapVirtualKeyExW(code, MAPVK_VSC_TO_VK_EX, g_layout));
}

// The window procedure's code for a key: the layout's, with the left / right modifiers as one (WM_KEYDOWN's), and the
// number pad's digits with Num Lock on.
int WindowsKey(int scan, bool numLock) {
    int key = LayoutKeyOf(scan);
    switch (key) {
    case VK_LSHIFT: case VK_RSHIFT: return VK_SHIFT;
    case VK_LCONTROL: case VK_RCONTROL: return VK_CONTROL;
    case VK_LMENU: case VK_RMENU: return VK_MENU;
    default: break;
    }
    if (numLock && !(scan & 0x100)) {
        switch (key) {
        case VK_INSERT: return VK_NUMPAD0;
        case VK_END: return VK_NUMPAD1;
        case VK_DOWN: return VK_NUMPAD2;
        case VK_NEXT: return VK_NUMPAD3;
        case VK_LEFT: return VK_NUMPAD4;
        case VK_CLEAR: return VK_NUMPAD5;
        case VK_RIGHT: return VK_NUMPAD6;
        case VK_HOME: return VK_NUMPAD7;
        case VK_UP: return VK_NUMPAD8;
        case VK_PRIOR: return VK_NUMPAD9;
        case VK_DELETE: return VK_DECIMAL;
        default: break;
        }
    }
    return key;
}

// The layout's control character with Ctrl held (platform::LayoutControlCharacter's, on the layout checked).
int LayoutControlOf(int key, int scan) {
    BYTE state[256] = {};
    state[VK_CONTROL] = 0x80;
    wchar_t out[8] = {};
    const int n = ToUnicodeEx(static_cast<UINT>(key), static_cast<UINT>(scan & 0xFF), state, out, 8, 4, g_layout);
    return n == 1 && (out[0] < 0x20 || out[0] == 0x7F) ? out[0] : 0;
}

// Keys whose scan code alone does not name them to MapVirtualKeyExW: Pause (E1 1D 45, a sequence) against Num Lock
// (45), Print Screen (E0 37, also Alt+SysRq's 54).
bool Unmappable(int scan) { return scan == 0x45 || scan == 0x145 || scan == 0x137; }

// The character the layout types for `key` with Ctrl held or not, or 0 (none, or more than one).
int Typed(int key, int scan, bool control) {
    BYTE state[256] = {};
    if (control) state[VK_CONTROL] = 0x80;
    wchar_t out[8] = {};
    // Flag 4: the keyboard state (a dead key) left as it was.
    const int n = ToUnicodeEx(static_cast<UINT>(key), static_cast<UINT>(scan & 0xFF), state, out, 8, 4, g_layout);
    return n == 1 ? out[0] : 0;
}

} // namespace

int main() {
    const int count = GetKeyboardLayoutList(0, nullptr);
    std::vector<HKL> layouts(count > 0 ? count : 0);
    if (count > 0) GetKeyboardLayoutList(count, layouts.data());
    int checks = 0, problems = 0;
    for (HKL layout : layouts) {
        g_layout = layout;
        const unsigned language = static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(layout) & 0xFFFF);
        int keys = 0, differing = 0;
        for (int sc = SDL_SCANCODE_A; sc <= SDL_SCANCODE_RGUI; ++sc) {
            for (const bool numLock : {false, true}) {
                const SDL_Keymod mod = numLock ? SDL_KMOD_NUM : SDL_KMOD_NONE;
                const KeyCode code = platform::sdl_keys::KeyOf(static_cast<SDL_Scancode>(sc), SDLK_UNKNOWN, mod, LayoutKeyOf);
                if (code.key == 0 || Unmappable(code.scan)) continue;
                ++keys;
                ++checks;
                const int want = WindowsKey(code.scan, numLock);
                if (want != code.key) {
                    ++differing;
                    if (++problems <= 40)
                        std::printf("input_check: layout %04X, SDL scancode %d (scan %03X, Num Lock %s): key %02X, Windows %02X\n",
                                    language, sc, code.scan, numLock ? "on" : "off", code.key, want);
                }
                if (numLock) continue;
                for (const bool control : {false, true}) {
                    const int ours = platform::sdl_keys::ControlCharacter(code.key, code.scan, control ? SDL_KMOD_CTRL : SDL_KMOD_NONE,
                                                                          LayoutControlOf);
                    const int typed = Typed(code.key, code.scan, control);
                    // Only the control characters: SDL's text events carry every other character.
                    const int windows = typed > 0 && (typed < 0x20 || typed == 0x7F) ? typed : 0;
                    ++checks;
                    if (ours != windows) {
                        ++differing;
                        if (++problems <= 40)
                            std::printf("input_check: layout %04X, scan %03X key %02X%s: control character %02X, Windows %02X\n",
                                        language, code.scan, code.key, control ? " with Ctrl" : "", ours, windows);
                    }
                }
            }
        }
        std::printf("input_check: layout %04X (%p): %d keys, %d differ\n", language, static_cast<void*>(layout), keys, differing);
    }
    std::printf("input_check: %d layouts, %d checks, %d problem(s)\n", static_cast<int>(layouts.size()), checks, problems);
    return problems ? 1 : 0;
}
