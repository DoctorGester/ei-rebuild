// Replays the game CRT's formatted text (crt_format_vectors.h) against libcrt::Vsprintf and libcrt::Atof
// (src/lib/crt_format.cpp): the count and every byte written, the bits of atof's result.
#include "crt_format_vectors.h"

#include "game/game_data.h"
#include "lib/crt.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

using namespace ei::vectors;
using namespace ei::vectors::crt_format;

unsigned short g_classes[256];   // _pctype points here once the fixture ran

bool CheckLocale(Reader& r, char*, std::size_t) {
    ei::GameData<char>(kDecimalPoint) = r.Get<char>();
    for (unsigned short& word : g_classes) word = r.Get<unsigned short>();
    ei::GameData<const unsigned short*>(kCharClasses) = g_classes;
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
    std::vector<char> out(kMaxOutput * 16, '\x7F');
    const int actual = libcrt::Vsprintf(out.data(), format.c_str(), reinterpret_cast<va_list>(stack.data()));
    if (actual == expected && std::memcmp(out.data(), text, static_cast<std::size_t>(expected)) == 0 &&
        out[static_cast<std::size_t>(expected)] == '\0')
        return true;
    std::snprintf(detail, detailSize, "\"%.60s\": %d \"%.*s\", ours %d \"%.80s\"", format.c_str(), expected,
                  expected < 80 ? expected : 80, reinterpret_cast<const char*>(text), actual,
                  actual >= 0 ? out.data() : "");
    return false;
}
EI_VECTOR_CHECK_BYTES("vsprintf", CheckVsprintf);

bool CheckAtof(Reader& r, char* detail, std::size_t detailSize) {
    const std::string text = r.String();
    const double expected = r.Get<double>();
    const double actual = libcrt::Atof(text.c_str());
    if (Same(expected, actual)) return true;
    std::uint64_t e, a;
    std::memcpy(&e, &expected, sizeof e);
    std::memcpy(&a, &actual, sizeof a);
    std::snprintf(detail, detailSize, "\"%.60s\": %016llX, ours %016llX", text.c_str(), e, a);
    return false;
}
EI_VECTOR_CHECK_BYTES("atof", CheckAtof);

} // namespace
