// Asset text to UTF-8 and back; lengths, cuts and typed text by character. See asset_text.h.
#include "asset_text.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

#include <string>
#include <utility>
#include <windows.h>

namespace text {

namespace {

std::string_view Bytes(const std::string& text) {
    return std::string_view(text.c_str(), static_cast<std::size_t>(text.size()));
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

// `wide` in `to`; with WC_NO_BEST_FIT_CHARS a character `to` has no byte for becomes its default character ('?').
std::string Narrow(const std::wstring& wide, unsigned to, DWORD flags) {
    const int size = static_cast<int>(wide.size());
    const int count = WideCharToMultiByte(to, flags, wide.data(), size, nullptr, 0, nullptr, nullptr);
    std::string bytes(static_cast<std::size_t>(count > 0 ? count : 0), '\0');
    if (count > 0) WideCharToMultiByte(to, flags, wide.data(), size, bytes.data(), count, nullptr, nullptr);
    return bytes;
}

constexpr std::size_t kEscapeLength = 5;   // "\007C" and the three colour bytes

bool IsTrailByte(char byte) { return (static_cast<unsigned char>(byte) & 0xC0) == 0x80; }

// A colour escape starts at `text[i]`.
bool IsEscape(std::string_view text, std::size_t i) {
    return text[i] == '\007' && i + 1 < text.size() && text[i + 1] == 'C';
}

// Where the colour escape at or after `from` starts (`text.size()` for none).
std::size_t NextEscape(std::string_view text, std::size_t from) {
    for (std::size_t i = from; i < text.size(); ++i)
        if (IsEscape(text, i)) return i;
    return text.size();
}

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

} // namespace

// A colour escape's three bytes are raw (the game builds them), so they are copied as they are, and only the text
// between escapes is converted.
std::string FromAssetText(std::string_view bytes, unsigned codepage) {
    if (IsAscii(bytes)) return std::string(bytes);
    // Every byte of the single-byte code pages the game's data uses maps to one character and back
    // (1250-1252 checked: all 255 non-zero bytes), so ToAssetText restores the asset's bytes.
    std::string text;
    for (std::size_t i = 0; i < bytes.size();) {
        const std::size_t escape = NextEscape(bytes, i);
        const std::string_view plain = bytes.substr(i, escape - i);
        text += IsAscii(plain) ? std::string(plain) : Narrow(Widen(plain, codepage, 0), CP_UTF8, 0);
        const std::size_t length = (std::min)(kEscapeLength, bytes.size() - escape);
        text.append(bytes.data() + escape, length);
        i = escape + length;
    }
    return text;
}

void FromAssetText(std::string* text, unsigned codepage) {
    if (IsAscii(*text)) return;
    *text = FromAssetText(std::string_view(*text), codepage);
}

bool ToAssetText(std::string_view text, std::string* out, unsigned codepage) {
    if (IsAscii(text)) return false;
    std::string bytes;
    bool converted = false;
    for (std::size_t i = 0; i < text.size();) {
        const std::size_t escape = NextEscape(text, i);
        const std::string_view plain = text.substr(i, escape - i);
        if (IsAscii(plain)) {
            bytes += plain;
        } else {
            const std::wstring wide = Widen(plain, CP_UTF8, MB_ERR_INVALID_CHARS);
            if (wide.empty()) return false;   // not UTF-8: 8-bit text that has not been converted
            bytes += Narrow(wide, codepage, WC_NO_BEST_FIT_CHARS);
            converted = true;
        }
        const std::size_t length = (std::min)(kEscapeLength, text.size() - escape);
        bytes.append(text.data() + escape, length);
        i = escape + length;
    }
    if (!converted) return false;   // ASCII but for the escapes' bytes
    *out = std::move(bytes);
    return true;
}

std::string ToAssetText(std::string_view text, unsigned codepage) {
    std::string bytes;
    if (!ToAssetText(text, &bytes, codepage)) return std::string(text);
    return bytes;
}

int CharacterCount(std::string_view text) {
    int count = 0;
    for (std::size_t i = 0; i < text.size();) {
        if (IsEscape(text, i)) {
            const std::size_t length = (std::min)(kEscapeLength, text.size() - i);
            count += static_cast<int>(length);
            i += length;
            continue;
        }
        // A UTF-8 sequence counts by its lead byte; the continuation bytes (10xxxxxx) do not count.
        if (!IsTrailByte(text[i])) ++count;
        ++i;
    }
    return count;
}

// ---- Lengths and cuts by character ----------------------------------------------------------------

int CharacterLength(const char* p) {
    int length = 1;
    while (IsTrailByte(p[length])) ++length;
    return length;
}

int CharacterBytes(std::string_view text, int characters) {
    if (characters <= 0) return 0;
    // CharacterCount's characters: a colour escape by its bytes, a UTF-8 sequence once.
    int count = 0;
    std::size_t i = 0;
    while (i < text.size() && count < characters) {
        if (IsEscape(text, i)) {
            const std::size_t length = (std::min)({kEscapeLength, text.size() - i,
                                                   static_cast<std::size_t>(characters - count)});
            count += static_cast<int>(length);
            i += length;
            continue;
        }
        ++i;
        while (i < text.size() && IsTrailByte(text[i])) ++i;
        ++count;
    }
    return static_cast<int>(i);
}

std::size_t FittingBytes(std::string_view text, std::size_t room) {
    if (text.size() <= room) return text.size();
    std::size_t n = room;
    while (n > 0 && IsTrailByte(text[n])) --n;
    return n;
}

void CopyFitting(char* to, std::size_t size, std::string_view text) {
    if (size == 0) return;
    const std::size_t n = FittingBytes(text, size - 1);
    std::memcpy(to, text.data(), n);
    to[n] = '\0';
}

// ---- Typing ---------------------------------------------------------------------------------------

std::string Typed(const std::string& head, int character) { return head + Utf8Of(character); }

int TypedLength(int character) { return static_cast<int>(Utf8Of(character).size()); }

int NextCharacter(const std::string& text, int at) {
    const std::string_view bytes = Bytes(text);
    if (at < 0 || at >= static_cast<int>(bytes.size())) return at + 1;
    std::size_t next = static_cast<std::size_t>(at) + 1;
    while (next < bytes.size() && IsTrailByte(bytes[next])) ++next;
    return static_cast<int>(next);
}

int PreviousCharacter(const std::string& text, int at) {
    const std::string_view bytes = Bytes(text);
    if (at <= 0 || at > static_cast<int>(bytes.size())) return at - 1;
    std::size_t previous = static_cast<std::size_t>(at) - 1;
    while (previous > 0 && IsTrailByte(bytes[previous])) --previous;
    return static_cast<int>(previous);
}

int CharacterStart(const std::string& text, int at) {
    const std::string_view bytes = Bytes(text);
    if (at <= 0 || at >= static_cast<int>(bytes.size())) return at;
    std::size_t start = static_cast<std::size_t>(at);
    while (start > 0 && IsTrailByte(bytes[start])) --start;
    return static_cast<int>(start);
}

} // namespace text
