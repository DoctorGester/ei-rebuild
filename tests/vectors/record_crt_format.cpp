// Record mode for the game CRT's formatted text (crt_format_vectors.h): sprintf and vsprintf run the
// original _output (006F17D4) as the originals do (as verify_crt_format.cpp did), atof its original
// through the trampoline. Every scenario reaches them (file names, interface text, the databases'
// numbers).
#include "crt_format_vectors.h"

#include "ei/hook.h"

#include <climits>
#include <cstdarg>
#include <cstring>
#include <mutex>
#include <vector>

namespace {

using namespace ei::vectors;
using namespace ei::vectors::crt_format;

Sampler g_locale("crt", "@CrtFormatLocale", kVariableSize, 1);
Sampler g_vsprintf("crt", "vsprintf", kVariableSize, 200, 2);
Sampler g_atof("crt", "atof", kVariableSize, 200, 2);

// ---- The locale the conversions read ----

struct Locale {
    char point;
    unsigned short classes[256];
};

Locale CurrentLocale() {
    Locale locale;
    locale.point = *reinterpret_cast<const char*>(kDecimalPoint);
    std::memcpy(locale.classes, *reinterpret_cast<const unsigned short* const*>(kCharClasses), sizeof(locale.classes));
    return locale;
}

bool SameLocale(const Locale& a, const Locale& b) {
    return a.point == b.point && std::memcmp(a.classes, b.classes, sizeof(a.classes)) == 0;
}

// __lc_handle[LC_CTYPE]: nonzero once the game has set its locale.
unsigned LocaleHandle() { return *reinterpret_cast<const unsigned*>(0x007CABF0); }

// Whether the locale now is the fixture's; the fixture is taken (and offered) at the first call after
// the game set its locale.
bool LocaleAsRecorded() {
    static std::mutex lock;
    static bool taken = false;
    static Locale recorded;
    const Locale now = CurrentLocale();
    std::lock_guard<std::mutex> guard(lock);
    if (!taken) {
        if (LocaleHandle() == 0) return false;
        taken = true;
        recorded = now;
        Writer w;
        w.Put(recorded.point);
        w.PutBytes(recorded.classes, sizeof(recorded.classes));
        g_locale.OfferBytes(w, ControlWord());
        return true;
    }
    return SameLocale(now, recorded);
}

// ---- sprintf / vsprintf ----

// The VC6 FILE as _output uses it: a string FILE is _IOWRT | _IOSTRG with _cnt bytes of room.
struct StringFile {
    char* ptr;
    int count;
    char* base;
    int flag;
    int file;
    int charBuffer;
    int bufferSize;
    char* tempName;
};
constexpr int kStringFileFlags = 0x42;

int(__cdecl* const s_output)(StringFile*, const char*, va_list) =
    reinterpret_cast<int(__cdecl*)(StringFile*, const char*, va_list)>(0x006F17D4);

// sprintf / vsprintf as the originals: _output on a string FILE of INT_MAX bytes, then the terminator.
int OriginalVsprintf(char* buffer, const char* format, va_list args) {
    StringFile file{buffer, INT_MAX, buffer, kStringFileFlags, 0, 0, 0, nullptr};
    const int written = s_output(&file, format, args);
    if (--file.count >= 0) *file.ptr = '\0';
    return written;
}

// _output's format states and flags (crt_format.cpp); its __lookuptable (007476D0) read from the exe.
enum State { kNormal, kPercent, kFlag, kWidth, kDot, kPrecision, kSize, kType };
constexpr unsigned kShort = 0x20;
constexpr unsigned kLong = 0x10;
constexpr unsigned kWide = 0x800;
constexpr unsigned kInt64 = 0x8000;
constexpr unsigned short kLeadByte = 0x8000;
const unsigned char* FormatLookup() { return reinterpret_cast<const unsigned char*>(0x007476D0); }

struct Span {
    const char* begin;
    std::size_t size;
};

// Walks `format` as _output does and writes the arguments it reads to `w`. False for what the replay
// does not take (above). `texts` gets the text arguments' bytes, `hasFloat` whether a float is read.
bool PutArguments(Writer& w, const char* format, va_list args, std::vector<Span>& texts, bool& hasFloat) {
    const unsigned char* lookup = FormatLookup();
    const unsigned short* classes = *reinterpret_cast<const unsigned short* const*>(kCharClasses);
    Writer list;
    std::uint32_t count = 0;
    const unsigned char* f = reinterpret_cast<const unsigned char*>(format);
    int state = kNormal;
    unsigned flags = 0;
    int precision = -1;
    unsigned char c;
    while ((c = *f++) != 0) {
        const signed char sc = static_cast<signed char>(c);
        const int cls = sc < ' ' || sc > 'x' ? 0 : lookup[sc - ' '] & 0xF;
        state = lookup[cls * 8 + state] >> 4;
        switch (state) {
        case kNormal:
        normal:
            if ((classes[c] & kLeadByte) && *f++ == 0) return false;   // the original reads on past the end
            break;
        case kPercent:
            flags = 0;
            precision = -1;
            break;
        case kWidth:
            if (c == '*') {
                list.Put(kInt32);
                list.Put<std::int32_t>(va_arg(args, int));
                ++count;
            }
            break;
        case kDot:
            precision = 0;
            break;
        case kPrecision:
            if (c == '*') {
                const int value = va_arg(args, int);
                list.Put(kInt32);
                list.Put<std::int32_t>(value);
                ++count;
                precision = value < 0 ? -1 : value;
            } else {
                precision = precision * 10 + (sc - '0');
            }
            break;
        case kSize:
            if (c == 'I') {
                if (f[0] != '6' || f[1] != '4') {
                    state = kNormal;
                    goto normal;
                }
                f += 2;
                flags |= kInt64;
            } else if (c == 'h') {
                flags |= kShort;
            } else if (c == 'l') {
                flags |= kLong;
            } else if (c == 'w') {
                flags |= kWide;
            }
            break;
        case kType:
            switch (c) {
            case 'C':
            case 'S':
            case 'Z':
            case 'n':
                return false;
            case 'c':
                if (flags & (kLong | kWide)) return false;
                list.Put(kInt32);
                list.Put<std::int32_t>(va_arg(args, int));
                ++count;
                break;
            case 's': {
                if (flags & (kLong | kWide)) return false;
                const char* text = va_arg(args, const char*);
                ++count;
                if (!text) {
                    list.Put(kNullText);
                    break;
                }
                std::size_t length = 0;
                for (int limit = precision == -1 ? INT_MAX : precision; limit != 0 && text[length]; --limit) {
                    if (++length > kMaxText) return false;
                }
                list.Put(kText);
                list.Put(static_cast<std::uint32_t>(length));
                list.PutBytes(text, length);
                texts.push_back({text, length});
                break;
            }
            case 'E':
            case 'G':
            case 'e':
            case 'f':
            case 'g': {
                const double value = va_arg(args, double);
                list.Put(kDouble);
                list.Put(value);
                ++count;
                hasFloat = true;
                break;
            }
            case 'd':
            case 'i':
            case 'u':
            case 'o':
            case 'p':
            case 'X':
            case 'x':
                if (flags & kInt64) {
                    list.Put(kInt64);
                    list.Put<std::int64_t>(va_arg(args, std::int64_t));
                } else {
                    list.Put(kInt32);
                    list.Put<std::int32_t>(va_arg(args, int));
                }
                ++count;
                break;
            }
            break;
        }
    }
    w.Put(count);
    w.PutBytes(list.data(), list.size());
    return true;
}

bool Overlaps(const char* a, std::size_t aSize, const char* b, std::size_t bSize) {
    return a < b + bSize && b < a + aSize;
}

int RecordFormat(char* buffer, const char* format, va_list args) {
    const std::uint16_t cw = ControlWord();
    Writer w;
    std::vector<Span> texts;
    bool hasFloat = false;
    const std::size_t formatLength = strnlen(format, kMaxText + 1);
    bool keep = formatLength <= kMaxText && LocaleAsRecorded();
    if (keep) {
        w.PutString(format);
        keep = PutArguments(w, format, args, texts, hasFloat);
    }
    const int written = OriginalVsprintf(buffer, format, args);
    if (!keep || written < 0 || static_cast<std::size_t>(written) > kMaxOutput) return written;
    const std::size_t outSize = static_cast<std::size_t>(written) + 1;
    if (Overlaps(buffer, outSize, format, formatLength + 1)) return written;
    for (const Span& text : texts)
        if (Overlaps(buffer, outSize, text.begin, text.size + 1)) return written;
    w.Put<std::int32_t>(written);
    w.PutBytes(buffer, static_cast<std::size_t>(written));
    g_vsprintf.OfferBytes(w, cw, hasFloat ? 1 : 0);
    return written;
}

// No trampolines: the originals are _output on a string FILE.
int(__cdecl* s_unusedSprintf)(char*, const char*, ...);
int __cdecl SprintfRecord(char* buffer, const char* format, ...) {
    va_list args;
    va_start(args, format);
    const int written = RecordFormat(buffer, format, args);
    va_end(args);
    return written;
}

int(__cdecl* s_unusedVsprintf)(char*, const char*, va_list);
int __cdecl VsprintfRecord(char* buffer, const char* format, va_list args) { return RecordFormat(buffer, format, args); }

// ---- atof ----

double(__cdecl* s_atof)(const char*);
double __cdecl AtofRecord(const char* text) {
    const std::uint16_t cw = ControlWord();
    const double result = s_atof(text);
    const std::size_t length = strnlen(text, kMaxText + 1);
    if (length > kMaxText || !LocaleAsRecorded()) return result;
    Writer w;
    w.PutString(text);
    w.Put(result);
    bool unusual = length > 17;
    for (std::size_t i = 0; i < length && !unusual; ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        unusual = c >= 0x80 || c == 'e' || c == 'E' || c == 'd' || c == 'D';
    }
    g_atof.OfferBytes(w, cw, unusual ? 1 : 0);
    return result;
}

} // namespace

EI_RECORD(0x006E8715, SprintfRecord, 0, s_unusedSprintf);
EI_RECORD(0x006ECC47, VsprintfRecord, 0, s_unusedVsprintf);
// 006E911A: PUSH EBP; MOV EBP,ESP; SUB ESP,0x18 (6 bytes).
EI_RECORD(0x006E911A, AtofRecord, 6, s_atof);
