// Upper and lower case of UTF-8 text by a small table (EI_UTF8, ticket U5). See text_case.h.
#include "text_case.h"

namespace text {

namespace {

// Every code point the table changes is below U+0800: two UTF-8 bytes, a lead C2-DF and a trail.
bool IsTwoByteLead(unsigned char c) { return c >= 0xC2 && c <= 0xDF; }
bool IsTrail(unsigned char c) { return (c & 0xC0) == 0x80; }
unsigned CodeOf(unsigned char lead, unsigned char trail) { return (lead & 0x1Fu) << 6 | (trail & 0x3Fu); }
unsigned char LeadOf(unsigned c) { return static_cast<unsigned char>(0xC0 | c >> 6); }
unsigned char TrailOf(unsigned c) { return static_cast<unsigned char>(0x80 | (c & 0x3F)); }

using CaseMap = unsigned (*)(unsigned);

// Byte `i` of `s` once its character is case-mapped by `map`: an ASCII byte by itself, a two-byte
// sequence's lead by the trail after it (read only below `limit`), its trail by the lead before it.
unsigned char MappedByte(const unsigned char* s, std::size_t i, std::size_t limit, CaseMap map) {
    const unsigned char c = s[i];
    if (c < 0x80) return static_cast<unsigned char>(map(c));
    if (IsTwoByteLead(c)) return i + 1 < limit && IsTrail(s[i + 1]) ? LeadOf(map(CodeOf(c, s[i + 1]))) : c;
    if (IsTrail(c) && i > 0 && IsTwoByteLead(s[i - 1])) return TrailOf(map(CodeOf(s[i - 1], c)));
    return c;
}

void MapInPlace(char* text, std::size_t size, CaseMap map) {
    unsigned char* s = reinterpret_cast<unsigned char*>(text);
    for (std::size_t i = 0; i < size && s[i] != 0;) {
        const unsigned char c = s[i];
        if (c < 0x80) {
            s[i] = static_cast<unsigned char>(map(c));
            ++i;
        } else if (IsTwoByteLead(c) && i + 1 < size && IsTrail(s[i + 1])) {
            const unsigned mapped = map(CodeOf(c, s[i + 1]));
            s[i] = LeadOf(mapped);
            s[i + 1] = TrailOf(mapped);
            i += 2;
        } else {
            ++i;
        }
    }
}

} // namespace

unsigned LowerCodePoint(unsigned c) {
    if (c >= 'A' && c <= 'Z') return c + 0x20;
    if (c >= 0xC0 && c <= 0xDE) return c == 0xD7 ? c : c + 0x20;   // Latin-1, not ×
    if (c >= 0x400 && c <= 0x40F) return c + 0x50;                  // Ѐ-Џ
    if (c >= 0x410 && c <= 0x42F) return c + 0x20;                  // А-Я
    switch (c) {
    case 0x152: return 0x153;   // Œ
    case 0x160: return 0x161;   // Š
    case 0x178: return 0xFF;    // Ÿ
    case 0x17D: return 0x17E;   // Ž
    case 0x490: return 0x491;   // Ґ
    default: return c;
    }
}

unsigned UpperCodePoint(unsigned c) {
    if (c >= 'a' && c <= 'z') return c - 0x20;
    if (c >= 0xE0 && c <= 0xFE) return c == 0xF7 ? c : c - 0x20;   // Latin-1, not ÷
    if (c >= 0x430 && c <= 0x44F) return c - 0x20;                  // а-я
    if (c >= 0x450 && c <= 0x45F) return c - 0x50;                  // ѐ-џ
    switch (c) {
    case 0x153: return 0x152;
    case 0x161: return 0x160;
    case 0xFF: return 0x178;
    case 0x17E: return 0x17D;
    case 0x491: return 0x490;
    default: return c;
    }
}

void LowerInPlace(char* text, std::size_t size) { MapInPlace(text, size, LowerCodePoint); }
void UpperInPlace(char* text, std::size_t size) { MapInPlace(text, size, UpperCodePoint); }

int CompareIgnoringCase(const char* a, const char* b, std::size_t n, bool terminated) {
    const unsigned char* pa = reinterpret_cast<const unsigned char*>(a);
    const unsigned char* pb = reinterpret_cast<const unsigned char*>(b);
    // A terminated text can be read past `n` up to its terminator (a lead byte's trail).
    const std::size_t limit = terminated ? static_cast<std::size_t>(-1) : n;
    for (std::size_t i = 0; i < n; ++i) {
        const unsigned char ca = pa[i];
        const unsigned char cb = pb[i];
        if (terminated && (ca == 0 || cb == 0)) return ca == cb ? 0 : ca < cb ? -1 : 1;
        const unsigned char la = MappedByte(pa, i, limit, LowerCodePoint);
        const unsigned char lb = MappedByte(pb, i, limit, LowerCodePoint);
        if (la != lb) return la < lb ? -1 : 1;
    }
    return 0;
}

bool IsPrintableCodePoint(unsigned c) {
    if (c < 0x20 || c == 0x7F) return false;
    if (c >= 0x80 && c < 0xA0) return false;
    if (c >= 0xD800 && c <= 0xDFFF) return false;
    return c <= 0x10FFFF;
}

int DecodeUtf8(const char* text, std::size_t available, unsigned* codePoint) {
    const unsigned char* s = reinterpret_cast<const unsigned char*>(text);
    *codePoint = 0xFFFFFFFFu;
    if (available == 0) return 0;
    const unsigned char lead = s[0];
    if (lead < 0x80) {
        *codePoint = lead;
        return 1;
    }
    int length;
    unsigned c;
    unsigned minimum;
    if (lead >= 0xC2 && lead <= 0xDF) {
        length = 2;
        c = lead & 0x1Fu;
        minimum = 0x80;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
        length = 3;
        c = lead & 0x0Fu;
        minimum = 0x800;
    } else if (lead >= 0xF0 && lead <= 0xF4) {
        length = 4;
        c = lead & 0x07u;
        minimum = 0x10000;
    } else {
        return 1;
    }
    if (available < static_cast<std::size_t>(length)) return 1;
    for (int k = 1; k < length; ++k) {
        if (!IsTrail(s[k])) return 1;
        c = c << 6 | (s[k] & 0x3Fu);
    }
    if (c < minimum || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) return 1;
    *codePoint = c;
    return length;
}

} // namespace text
