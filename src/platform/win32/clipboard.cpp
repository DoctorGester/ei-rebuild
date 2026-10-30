// The clipboard on Windows (platform/clipboard.h): Unicode text. Direct calls (never through game.exe's slots: the
// clipboard came with Rebirth's console).
#include "platform/clipboard.h"

#include "platform/win32/utf16.h"

#include <cstring>
#include <windows.h>

namespace platform {

void CopyText(std::string_view text) {
    const std::wstring wide = win32::Utf16(text);
    const std::size_t bytes = (wide.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, bytes);
    if (!memory) return;
    void* locked = GlobalLock(memory);
    if (!locked) {
        GlobalFree(memory);
        return;
    }
    std::memcpy(locked, wide.c_str(), bytes);
    GlobalUnlock(memory);
    if (!OpenClipboard(nullptr)) {
        GlobalFree(memory);
        return;
    }
    EmptyClipboard();
    SetClipboardData(CF_UNICODETEXT, memory);
    CloseClipboard();
}

std::string ClipboardText() {
    std::wstring wide;
    if (OpenClipboard(nullptr)) {
        if (HANDLE data = GetClipboardData(CF_UNICODETEXT)) {
            if (const auto* text = static_cast<const wchar_t*>(GlobalLock(data))) {
                wide = text;
                GlobalUnlock(data);
            }
        }
        CloseClipboard();
    }
    return win32::Utf8(wide);
}

} // namespace platform
