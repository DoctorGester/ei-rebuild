// UTF-8 and the system's UTF-16 (platform_win32.h): every text the Windows backend passes to a wide call or takes from
// one.
#include "platform/win32/platform_win32.h"

namespace text {

std::wstring Wide(std::string_view text) {
    if (text.empty()) return {};
    const int size = static_cast<int>(text.size());
    UINT codepage = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    int count = ::MultiByteToWideChar(codepage, flags, text.data(), size, nullptr, 0);
    if (count <= 0) {   // not UTF-8: the ANSI code page's bytes, as the ANSI calls take them
        codepage = CP_ACP;
        flags = 0;
        count = ::MultiByteToWideChar(codepage, flags, text.data(), size, nullptr, 0);
    }
    std::wstring wide(static_cast<std::size_t>(count > 0 ? count : 0), L'\0');
    if (count > 0) ::MultiByteToWideChar(codepage, flags, text.data(), size, wide.data(), count);
    return wide;
}

std::string Narrow(std::wstring_view wide) {
    if (wide.empty()) return {};
    const int size = static_cast<int>(wide.size());
    const int count = ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), size, nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<std::size_t>(count > 0 ? count : 0), '\0');
    if (count > 0) ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), size, utf8.data(), count, nullptr, nullptr);
    return utf8;
}

} // namespace text
