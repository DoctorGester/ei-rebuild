// Asset text to UTF-8 and back; lengths, cuts and typed text by character. See asset_text.h.
#include "asset_text.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

#include <string>
#include <utility>

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

// The characters of the asset code page's bytes 0x80..0xFF, as Windows maps them (MultiByteToWideChar, which the
// conversions used before): every byte is one character, a byte the code page leaves undefined the C1 control of its
// number, so every byte maps to a character and back and ToAssetText restores the asset's bytes. Bytes below 0x80 are
// ASCII.
constexpr char16_t kUpperHalf1250[128] = {
    0x20AC, 0x0081, 0x201A, 0x0083, 0x201E, 0x2026, 0x2020, 0x2021, 0x0088, 0x2030, 0x0160, 0x2039, 0x015A, 0x0164, 0x017D, 0x0179,
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x0098, 0x2122, 0x0161, 0x203A, 0x015B, 0x0165, 0x017E, 0x017A,
    0x00A0, 0x02C7, 0x02D8, 0x0141, 0x00A4, 0x0104, 0x00A6, 0x00A7, 0x00A8, 0x00A9, 0x015E, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x017B,
    0x00B0, 0x00B1, 0x02DB, 0x0142, 0x00B4, 0x00B5, 0x00B6, 0x00B7, 0x00B8, 0x0105, 0x015F, 0x00BB, 0x013D, 0x02DD, 0x013E, 0x017C,
    0x0154, 0x00C1, 0x00C2, 0x0102, 0x00C4, 0x0139, 0x0106, 0x00C7, 0x010C, 0x00C9, 0x0118, 0x00CB, 0x011A, 0x00CD, 0x00CE, 0x010E,
    0x0110, 0x0143, 0x0147, 0x00D3, 0x00D4, 0x0150, 0x00D6, 0x00D7, 0x0158, 0x016E, 0x00DA, 0x0170, 0x00DC, 0x00DD, 0x0162, 0x00DF,
    0x0155, 0x00E1, 0x00E2, 0x0103, 0x00E4, 0x013A, 0x0107, 0x00E7, 0x010D, 0x00E9, 0x0119, 0x00EB, 0x011B, 0x00ED, 0x00EE, 0x010F,
    0x0111, 0x0144, 0x0148, 0x00F3, 0x00F4, 0x0151, 0x00F6, 0x00F7, 0x0159, 0x016F, 0x00FA, 0x0171, 0x00FC, 0x00FD, 0x0163, 0x02D9,
};
constexpr char16_t kUpperHalf1251[128] = {
    0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021, 0x20AC, 0x2030, 0x0409, 0x2039, 0x040A, 0x040C, 0x040B, 0x040F,
    0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x0098, 0x2122, 0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F,
    0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7, 0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407,
    0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7, 0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457,
    0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417, 0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E, 0x041F,
    0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427, 0x0428, 0x0429, 0x042A, 0x042B, 0x042C, 0x042D, 0x042E, 0x042F,
    0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437, 0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F,
    0x0440, 0x0441, 0x0442, 0x0443, 0x0444, 0x0445, 0x0446, 0x0447, 0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D, 0x044E, 0x044F,
};
constexpr char16_t kUpperHalf1252[128] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178,
    0x00A0, 0x00A1, 0x00A2, 0x00A3, 0x00A4, 0x00A5, 0x00A6, 0x00A7, 0x00A8, 0x00A9, 0x00AA, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x00AF,
    0x00B0, 0x00B1, 0x00B2, 0x00B3, 0x00B4, 0x00B5, 0x00B6, 0x00B7, 0x00B8, 0x00B9, 0x00BA, 0x00BB, 0x00BC, 0x00BD, 0x00BE, 0x00BF,
    0x00C0, 0x00C1, 0x00C2, 0x00C3, 0x00C4, 0x00C5, 0x00C6, 0x00C7, 0x00C8, 0x00C9, 0x00CA, 0x00CB, 0x00CC, 0x00CD, 0x00CE, 0x00CF,
    0x00D0, 0x00D1, 0x00D2, 0x00D3, 0x00D4, 0x00D5, 0x00D6, 0x00D7, 0x00D8, 0x00D9, 0x00DA, 0x00DB, 0x00DC, 0x00DD, 0x00DE, 0x00DF,
    0x00E0, 0x00E1, 0x00E2, 0x00E3, 0x00E4, 0x00E5, 0x00E6, 0x00E7, 0x00E8, 0x00E9, 0x00EA, 0x00EB, 0x00EC, 0x00ED, 0x00EE, 0x00EF,
    0x00F0, 0x00F1, 0x00F2, 0x00F3, 0x00F4, 0x00F5, 0x00F6, 0x00F7, 0x00F8, 0x00F9, 0x00FA, 0x00FB, 0x00FC, 0x00FD, 0x00FE, 0x00FF,
};
static_assert(kAssetCodePage == 1250 || kAssetCodePage == 1251 || kAssetCodePage == 1252,
              "EI_ASSET_CODEPAGE: 1250, 1251 or 1252 (asset_text.cpp has their tables)");
constexpr const char16_t* kUpperHalf =
    kAssetCodePage == 1250 ? kUpperHalf1250 : kAssetCodePage == 1251 ? kUpperHalf1251 : kUpperHalf1252;

// The UTF-8 bytes of code point `c` appended to `out`.
void AppendUtf8(std::string& out, std::uint32_t c) {
    if (c < 0x80) {
        out += static_cast<char>(c);
    } else if (c < 0x800) {
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        out += static_cast<char>(0xE0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (c >> 18));
        out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
}

// The code points of `bytes` when they are well-formed UTF-8 (the shortest form, no surrogate, nothing past
// U+10FFFF, no sequence cut short: what MultiByteToWideChar's MB_ERR_INVALID_CHARS accepted); false otherwise.
bool DecodeUtf8(std::string_view bytes, std::u32string* out) {
    out->clear();
    for (std::size_t i = 0; i < bytes.size();) {
        const unsigned lead = static_cast<unsigned char>(bytes[i]);
        if (lead < 0x80) {
            out->push_back(lead);
            ++i;
            continue;
        }
        int trail;
        unsigned low = 0x80, high = 0xBF;   // the first trail byte's range
        std::uint32_t c;
        if (lead >= 0xC2 && lead <= 0xDF) {
            trail = 1;
            c = lead & 0x1F;
        } else if (lead >= 0xE0 && lead <= 0xEF) {
            trail = 2;
            c = lead & 0x0F;
            if (lead == 0xE0) low = 0xA0;   // not overlong
            if (lead == 0xED) high = 0x9F;  // no surrogate
        } else if (lead >= 0xF0 && lead <= 0xF4) {
            trail = 3;
            c = lead & 0x07;
            if (lead == 0xF0) low = 0x90;   // not overlong
            if (lead == 0xF4) high = 0x8F;  // not past U+10FFFF
        } else {
            return false;
        }
        if (bytes.size() - i <= static_cast<std::size_t>(trail)) return false;
        for (int k = 1; k <= trail; ++k) {
            const unsigned b = static_cast<unsigned char>(bytes[i + k]);
            if (b < (k == 1 ? low : 0x80) || b > (k == 1 ? high : 0xBF)) return false;
            c = (c << 6) | (b & 0x3F);
        }
        out->push_back(c);
        i += static_cast<std::size_t>(trail) + 1;
    }
    return true;
}

// `bytes`, in the asset code page, as UTF-8.
std::string AssetToUtf8(std::string_view bytes) {
    std::string text;
    for (char byte : bytes) {
        const unsigned b = static_cast<unsigned char>(byte);
        AppendUtf8(text, b < 0x80 ? b : kUpperHalf[b - 0x80]);
    }
    return text;
}

// Code points in the asset code page: a character it has no byte for as '?' (never a look-alike), one beyond U+FFFF as
// "??" (WideCharToMultiByte's two UTF-16 units).
std::string ToAssetBytes(const std::u32string& characters) {
    std::string bytes;
    for (char32_t c : characters) {
        if (c < 0x80) {
            bytes += static_cast<char>(c);
            continue;
        }
        if (c > 0xFFFF) {
            bytes += "??";
            continue;
        }
        const char16_t* found = std::find(kUpperHalf, kUpperHalf + 128, static_cast<char16_t>(c));
        bytes += found != kUpperHalf + 128 ? static_cast<char>(0x80 + (found - kUpperHalf)) : '?';
    }
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
    AppendUtf8(bytes, c);
    return bytes;
}

} // namespace

// A colour escape's three bytes are raw (the game builds them), so they are copied as they are, and only the text
// between escapes is converted.
std::string FromAssetText(std::string_view bytes) {
    if (IsAscii(bytes)) return std::string(bytes);
    std::string text;
    for (std::size_t i = 0; i < bytes.size();) {
        const std::size_t escape = NextEscape(bytes, i);
        const std::string_view plain = bytes.substr(i, escape - i);
        text += IsAscii(plain) ? std::string(plain) : AssetToUtf8(plain);
        const std::size_t length = (std::min)(kEscapeLength, bytes.size() - escape);
        text.append(bytes.data() + escape, length);
        i = escape + length;
    }
    return text;
}

void FromAssetText(std::string* text) {
    if (IsAscii(*text)) return;
    *text = FromAssetText(std::string_view(*text));
}

bool ToAssetText(std::string_view text, std::string* out) {
    if (IsAscii(text)) return false;
    std::string bytes;
    std::u32string characters;
    bool converted = false;
    for (std::size_t i = 0; i < text.size();) {
        const std::size_t escape = NextEscape(text, i);
        const std::string_view plain = text.substr(i, escape - i);
        if (IsAscii(plain)) {
            bytes += plain;
        } else {
            if (!DecodeUtf8(plain, &characters)) return false;   // not UTF-8: 8-bit text that has not been converted
            bytes += ToAssetBytes(characters);
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

std::string ToAssetText(std::string_view text) {
    std::string bytes;
    if (!ToAssetText(text, &bytes)) return std::string(text);
    return bytes;
}

std::u32string CodePoints(std::string_view text) {
    std::u32string characters;
    if (DecodeUtf8(text, &characters)) return characters;
    characters.clear();
    for (char byte : text) {
        const unsigned b = static_cast<unsigned char>(byte);
        characters.push_back(b < 0x80 ? b : kUpperHalf[b - 0x80]);
    }
    return characters;
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
