// Replays the game CRT's formatted text (crt_format_vectors.h) against libcrt::Format (src/lib/crt_format.cpp) and
// the host CRT's atof (C26): the count and every byte written, the bits of atof's result.
#include "crt_format_vectors.h"

#include "lib/crt.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

using namespace ei::vectors;
using namespace ei::vectors::crt_format;

// The game's locale as the conversions read it: crt_format.cpp takes the point as '.' and no byte as a lead byte
// (_pctype's _LEADBYTE), and ASCII digits as text::IsDigit has them (_forcdecpt reads the class of a number's text).
bool CheckLocale(Reader& r, char* detail, std::size_t detailSize) {
    const char decimalPoint = r.Get<char>();
    if (decimalPoint != '.') {
        std::snprintf(detail, detailSize, "the decimal point is '%c'", decimalPoint);
        return false;
    }
    for (int c = 0; c < 256; ++c) {
        const unsigned short word = r.Get<unsigned short>();
        constexpr unsigned short kDigit = 0x4, kLeadByte = 0x8000;
        const bool digit = c >= '0' && c <= '9';
        if ((word & kLeadByte) || (c < 0x80 && ((word & kDigit) != 0) != digit)) {
            std::snprintf(detail, detailSize, "the class word of %d is %04X", c, word);
            return false;
        }
    }
    return true;
}
EI_VECTOR_CHECK_BYTES("@CrtFormatLocale", CheckLocale);

template <typename T>
void Push(std::vector<unsigned char>& stack, const T& value) {
    const auto* p = reinterpret_cast<const unsigned char*>(&value);
    stack.insert(stack.end(), p, p + sizeof(T));
}

// The arguments laid out as the caller's stack (x86 va_list), the texts kept in `texts`.
bool BuildArguments(Reader& r, std::vector<unsigned char>& stack, std::vector<std::string>& texts, char* detail,
                    std::size_t detailSize) {
    const std::uint32_t count = r.Get<std::uint32_t>();
    texts.reserve(count);
    for (std::uint32_t i = 0; i < count && r.ok(); ++i) {
        switch (r.Get<std::uint8_t>()) {
        case kInt32: Push(stack, r.Get<std::int32_t>()); break;
        case kInt64: Push(stack, r.Get<std::int64_t>()); break;
        case kDouble: Push(stack, r.Get<double>()); break;
        case kText:
            texts.push_back(r.String());
            Push(stack, texts.back().c_str());
            break;
        case kNullText: Push(stack, static_cast<const char*>(nullptr)); break;
        default:
            std::snprintf(detail, detailSize, "argument %u has an unknown kind", i);
            return false;
        }
    }
    return true;
}

bool CheckVsprintf(Reader& r, char* detail, std::size_t detailSize) {
    const std::string format = r.String();
    std::vector<unsigned char> stack;
    std::vector<std::string> texts;
    if (!BuildArguments(r, stack, texts, detail, detailSize)) return false;
    const std::int32_t expected = r.Get<std::int32_t>();
    const unsigned char* text = r.Bytes(expected > 0 ? static_cast<std::size_t>(expected) : 0);
    if (!r.ok()) return true;   // RunBytes reports the short payload
    stack.resize(stack.size() + 16);   // room past the last argument
    const std::string out = libcrt::Format(format.c_str(), reinterpret_cast<va_list>(stack.data()));
    const int actual = static_cast<int>(out.size());
    if (actual == expected && std::memcmp(out.data(), text, out.size()) == 0) return true;
    std::snprintf(detail, detailSize, "\"%.60s\": %d \"%.*s\", ours %d \"%.80s\"", format.c_str(), expected,
                  expected < 80 ? expected : 80, reinterpret_cast<const char*>(text), actual, out.c_str());
    return false;
}
EI_VECTOR_CHECK_BYTES("vsprintf", CheckVsprintf);

bool CheckAtof(Reader& r, char* detail, std::size_t detailSize) {
    const std::string text = r.String();
    const double expected = r.Get<double>();
    const double actual = std::atof(text.c_str());
    if (Same(expected, actual)) return true;
    std::uint64_t e, a;
    std::memcpy(&e, &expected, sizeof e);
    std::memcpy(&a, &actual, sizeof a);
    std::snprintf(detail, detailSize, "\"%.60s\": %016llX, ours %016llX", text.c_str(), e, a);
    return false;
}
EI_VECTOR_CHECK_BYTES("atof", CheckAtof);

} // namespace
