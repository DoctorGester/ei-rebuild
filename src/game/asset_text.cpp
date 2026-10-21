// Asset text to UTF-8 and, until U3, back at the ANSI calls; typed text by character (U4). See asset_text.h.
#include "asset_text.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

#include <windows.h>

namespace text {

namespace {

std::string_view Bytes(const mfc::CString& text) {
    return std::string_view(static_cast<const char*>(text), static_cast<std::size_t>(text.GetLength()));
}

bool IsAscii(std::string_view bytes) {
    for (char c : bytes) {
        if (static_cast<unsigned char>(c) >= 0x80) return false;
    }
    return true;
}

// `bytes` in `from` as UTF-16 (empty when `from` rejects them: MB_ERR_INVALID_CHARS for UTF-8).
std::wstring Widen(std::string_view bytes, unsigned from, DWORD flags) {
    const int size = static_cast<int>(bytes.size());
    const int count = MultiByteToWideChar(from, flags, bytes.data(), size, nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(count > 0 ? count : 0), L'\0');
    if (count > 0) MultiByteToWideChar(from, flags, bytes.data(), size, wide.data(), count);
    return wide;
}

std::string Narrow(const std::wstring& wide, unsigned to) {
    const int size = static_cast<int>(wide.size());
    const int count = WideCharToMultiByte(to, 0, wide.data(), size, nullptr, 0, nullptr, nullptr);
    std::string bytes(static_cast<std::size_t>(count > 0 ? count : 0), '\0');
    if (count > 0) WideCharToMultiByte(to, 0, wide.data(), size, bytes.data(), count, nullptr, nullptr);
    return bytes;
}

} // namespace

std::string FromAssetText(std::string_view bytes, unsigned codepage) {
    if constexpr (!kUtf8) {
        (void)codepage;
        return std::string(bytes);
    } else {
        if (IsAscii(bytes)) return std::string(bytes);
        // Every byte of the single-byte code pages the game's data uses maps to one character and back
        // (1250-1252 checked: all 255 non-zero bytes), so the ANSI boundary restores the asset's bytes.
        return Narrow(Widen(bytes, codepage, 0), CP_UTF8);
    }
}

void FromAssetText(mfc::CString* text, unsigned codepage) {
    if constexpr (kUtf8) {
        const std::string_view bytes(static_cast<const char*>(*text), static_cast<std::size_t>(text->GetLength()));
        if (IsAscii(bytes)) return;
        const std::string converted = FromAssetText(bytes, codepage);
        const int length = static_cast<int>(converted.size());
        std::memcpy(text->GetBufferSetLength(length), converted.data(), converted.size());
        text->ReleaseBuffer(length);
    } else {
        (void)text;
        (void)codepage;
    }
}

bool ToAssetText(std::string_view text, std::string* out, unsigned codepage) {
    if constexpr (kUtf8) {
        if (IsAscii(text)) return false;
        const std::wstring wide = Widen(text, CP_UTF8, MB_ERR_INVALID_CHARS);
        if (wide.empty()) return false;   // not UTF-8: 8-bit text that has not been converted yet
        *out = Narrow(wide, codepage);
        return true;
    } else {
        (void)text;
        (void)out;
        (void)codepage;
        return false;
    }
}

int CharacterCount(std::string_view text) {
    if constexpr (!kUtf8) {
        return static_cast<int>(text.size());
    } else {
        constexpr std::size_t kEscapeLength = 5;   // "\007C" and the three colour bytes
        int count = 0;
        for (std::size_t i = 0; i < text.size();) {
            if (text[i] == '\007' && i + 1 < text.size() && text[i + 1] == 'C') {
                const std::size_t length = (std::min)(kEscapeLength, text.size() - i);
                count += static_cast<int>(length);
                i += length;
                continue;
            }
            // A UTF-8 sequence counts by its lead byte; the continuation bytes (10xxxxxx) do not count.
            if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) ++count;
            ++i;
        }
        return count;
    }
}

int CharacterCount(const mfc::CString& text) { return CharacterCount(Bytes(text)); }

// ---- Typing (U4) ---------------------------------------------------------------------------------------

namespace {

// The UTF-8 bytes of code point `character` (U+FFFD for one that is not a character's).
std::string Utf8Of(int character) {
    std::uint32_t c = static_cast<std::uint32_t>(character);
    if (c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) c = 0xFFFD;
    std::string bytes;
    if (c < 0x80) {
        bytes += static_cast<char>(c);
    } else if (c < 0x800) {
        bytes += static_cast<char>(0xC0 | (c >> 6));
        bytes += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        bytes += static_cast<char>(0xE0 | (c >> 12));
        bytes += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        bytes += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        bytes += static_cast<char>(0xF0 | (c >> 18));
        bytes += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        bytes += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        bytes += static_cast<char>(0x80 | (c & 0x3F));
    }
    return bytes;
}

bool IsContinuation(char byte) { return (static_cast<unsigned char>(byte) & 0xC0) == 0x80; }

} // namespace

mfc::CString Typed(const mfc::CString& head, int character) {
    if constexpr (kUtf8) {
        return head + Utf8Of(character).c_str();
    } else {
        return mfc::operator+(head, static_cast<char>(character));
    }
}

int TypedLength(int character) {
    if constexpr (kUtf8) {
        return static_cast<int>(Utf8Of(character).size());
    } else {
        (void)character;
        return 1;
    }
}

int NextCharacter(const mfc::CString& text, int at) {
    if constexpr (!kUtf8) {
        (void)text;
        return at + 1;
    } else {
        const std::string_view bytes = Bytes(text);
        if (at < 0 || at >= static_cast<int>(bytes.size())) return at + 1;
        std::size_t next = static_cast<std::size_t>(at) + 1;
        while (next < bytes.size() && IsContinuation(bytes[next])) ++next;
        return static_cast<int>(next);
    }
}

int PreviousCharacter(const mfc::CString& text, int at) {
    if constexpr (!kUtf8) {
        (void)text;
        return at - 1;
    } else {
        const std::string_view bytes = Bytes(text);
        if (at <= 0 || at > static_cast<int>(bytes.size())) return at - 1;
        std::size_t previous = static_cast<std::size_t>(at) - 1;
        while (previous > 0 && IsContinuation(bytes[previous])) --previous;
        return static_cast<int>(previous);
    }
}

int CharacterStart(const mfc::CString& text, int at) {
    if constexpr (!kUtf8) {
        (void)text;
        return at;
    } else {
        const std::string_view bytes = Bytes(text);
        if (at <= 0 || at >= static_cast<int>(bytes.size())) return at;
        std::size_t start = static_cast<std::size_t>(at);
        while (start > 0 && IsContinuation(bytes[start])) --start;
        return static_cast<int>(start);
    }
}

} // namespace text
