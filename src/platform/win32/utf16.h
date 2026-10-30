#pragma once

// UTF-8 and the system's UTF-16, for the Windows implementation's wide calls.

#include <string>
#include <string_view>
#include <windows.h>

namespace platform::win32 {

inline std::wstring Utf16(std::string_view text) {
    if (text.empty()) return {};
    const int size = static_cast<int>(text.size());
    const int count = MultiByteToWideChar(CP_UTF8, 0, text.data(), size, nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(count > 0 ? count : 0), L'\0');
    if (count > 0) MultiByteToWideChar(CP_UTF8, 0, text.data(), size, wide.data(), count);
    return wide;
}

inline std::string Utf8(std::wstring_view wide) {
    if (wide.empty()) return {};
    const int size = static_cast<int>(wide.size());
    const int count = WideCharToMultiByte(CP_UTF8, 0, wide.data(), size, nullptr, 0, nullptr, nullptr);
    std::string text(static_cast<std::size_t>(count > 0 ? count : 0), '\0');
    if (count > 0) WideCharToMultiByte(CP_UTF8, 0, wide.data(), size, text.data(), count, nullptr, nullptr);
    return text;
}

} // namespace platform::win32
