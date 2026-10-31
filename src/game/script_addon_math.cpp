// AddNewScriptCommands: addon's arithmetic functions (handlers 10011200-10011A53). Each reads its
// float arguments and stores the result as a float (helpers in script_addon.cpp). addon's x87 code
// ran at the game's 24-bit precision, where each operation rounds to a float: float maths here.
// sin, cos and pow are the C runtime's (addon's MSVC 2010 CRT takes its SSE2 code path under the
// game's FPU control word; our code calls the modern CRT's, the same Intel library); their
// double results are rounded to float, which hides any last-bit difference except at a float
// rounding boundary. floor, ceil and sqrt are exact or correctly rounded in any implementation.
#include "script_addon_handlers.h"

#include "asset_text.h"
#include "console.h"
#include "net_classes.h"
#include "random.h"
#include "world.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/str.h"
#include "lib/text_case.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#pragma function(sin, cos, pow, floor, ceil)

namespace scriptaddon {

namespace {

float* Floats(Call* call) { return call->cmd->floatArgs.data(); }
int FloatCount(Call* call) { return lib::Count(call->cmd->floatArgs); }

double CrtSin(double x) { return std::sin(x); }
double CrtCos(double x) { return std::cos(x); }
double CrtPow(double x, double y) { return std::pow(x, y); }

} // namespace

bool Sin(Call* call) {
    float x = Floats(call)[0];
    *call->result.number = static_cast<float>(CrtSin(x));
    return true;
}

bool Cos(Call* call) {
    float x = Floats(call)[0];
    *call->result.number = static_cast<float>(CrtCos(x));
    return true;
}

bool Pow(Call* call) {
    float x = Floats(call)[0];
    float y = Floats(call)[1];
    *call->result.number = static_cast<float>(CrtPow(x, y));
    return true;
}

// The signature has two floats, but the handler reads four, as in addon: there the last two came from past the end
// of the argument array (heap bytes, any value from run to run), here they are 0.
bool DistancePointPoint(Call* call) {
    float f[4] = {};
    const float* given = Floats(call);
    for (int i = 0, n = std::min(FloatCount(call), 4); i < n; ++i) f[i] = given[i];
    *call->result.number = Distance(f[0], f[1], f[2], f[3]);
    return true;
}

// Add / Mul of more than two: the sum / product in addon's order; of two, the game's own (false).
bool Add(Call* call) {
    if (FloatCount(call) == 2) return false;
    float sum = 0.0f;
    const float* f = Floats(call);
    for (int i = 0, n = FloatCount(call); i < n; ++i) sum = f[i] + sum;
    *call->result.number = sum;
    return true;
}

bool Mul(Call* call) {
    if (FloatCount(call) == 2) return false;
    float product = 1.0f;
    const float* f = Floats(call);
    for (int i = 0, n = FloatCount(call); i < n; ++i) product = f[i] * product;
    *call->result.number = product;
    return true;
}

// True unless an argument truncates to 0.
bool And(Call* call) {
    float result = 1.0f;
    const float* f = Floats(call);
    for (int i = 0, n = FloatCount(call); i < n; ++i)
        if (TruncateOrMin(f[i]) == 0) {
            result = 0.0f;
            break;
        }
    *call->result.number = result;
    return true;
}

// True when an argument does not truncate to 0.
bool Or(Call* call) {
    float result = 0.0f;
    const float* f = Floats(call);
    for (int i = 0, n = FloatCount(call); i < n; ++i)
        if (TruncateOrMin(f[i]) != 0) {
            result = 1.0f;
            break;
        }
    *call->result.number = result;
    return true;
}

// An argument replaces the maximum unless it is (ordered) less: equal and NaN arguments do.
bool Max(Call* call) {
    const float* f = Floats(call);
    float m = f[0];
    for (int i = 1, n = FloatCount(call); i < n; ++i)
        if (!(f[i] < m)) m = f[i];
    *call->result.number = m;
    return true;
}

// An argument replaces the minimum unless it is (ordered) greater.
bool Min(Call* call) {
    const float* f = Floats(call);
    float m = f[0];
    for (int i = 1, n = FloatCount(call); i < n; ++i)
        if (!(f[i] > m)) m = f[i];
    *call->result.number = m;
    return true;
}

// Negated only when below 0 (so -0 and NaN stay).
bool Abs(Call* call) {
    float x = Floats(call)[0];
    float result = x < 0.0f ? -x : x;
    *call->result.number = result;
    return true;
}

// floor(x + 0.5) from 0 up (and for NaN), ceil(x - 0.5) below; the sum is rounded to float first.
bool Round(Call* call) {
    float x = Floats(call)[0];
    *call->result.number = x < 0.0f ? std::ceil(x - 0.5f) : std::floor(x + 0.5f);
    return true;
}

bool Ceil(Call* call) {
    float x = Floats(call)[0];
    *call->result.number = std::ceil(x);
    return true;
}

bool Floor(Call* call) {
    float x = Floats(call)[0];
    *call->result.number = std::floor(x);
    return true;
}

// Random(max) stays the game's (the handler declines one argument); Random(min, max) is the game
// generator's Range when max > min, else min.
bool Random(Call* call) {
    if (FloatCount(call) == 1) return false;
    float low = Floats(call)[0];
    float high = Floats(call)[1];
    if (high > low) {
        *call->result.number = g_random.Range(low, high);
    } else {
        *call->result.number = low;
    }
    return true;
}

} // namespace scriptaddon

// --- Script addon strings ----------------------------------------------------------------------------

// AddNewScriptCommands: addon's string functions and its two text commands. String results are
// assigned to the string addon gets (its c_str(), as the game's string assignment took it) from a
// std::string addon builds first.
// FloatToStr formats with the C runtime's "%.<n>f" and StrToFloat parses with atof.

namespace scriptaddon {

namespace {

// Whether this is a network game.
// Sends a chat line from the server (second argument 0).

const std::string& Arg(Call* call, int i) { return call->cmd->stringArgs[i]; }

// 10010D8F: every string argument appended in order (each by its length).
std::string Joined(Call* call) {
    std::string text;
    for (const std::string& part : call->cmd->stringArgs) text += part;
    return text;
}

// std::string's c_str() assigned to the result (the text up to its first NUL).
void Assign(Call* call, const std::string& text) { *call->result.text = text.c_str(); }

// A NUL-terminated copy changed in place by lib/text_case.h's case (Rebirth's CharLowerA / CharUpperA).
std::string CaseConverted(std::string_view text, bool upper) {
    std::vector<char> buffer(text.begin(), text.end());
    buffer.push_back(0);
    if (upper) text::UpperInPlace(buffer.data());
    else text::LowerInPlace(buffer.data());
    return std::string(buffer.data());
}

} // namespace

// The object's id in decimal ("%I64d" of the sign-extended id).
bool GetObjectStringID(Call* call) {
    EngineObject* object = call->cmd->objectArgs[0]->object;
    if (!object) return InvalidArguments("GetObjectStringID");
    char text[64];
    std::snprintf(text, sizeof(text), "%d", object->id);
    Assign(call, text);
    return true;
}

// "%.<digits>f" (digits 3 by default, clamped to 0..8), then trailing zeros and a trailing '.'
// cut. The scan stops at index 0 without looking at it, so "100" with no decimals becomes "1".
bool FloatToStr(Call* call) {
    float value = call->cmd->floatArgs[0];
    int digits = std::ssize(call->cmd->floatArgs) >= 2 ? TruncateOrMin(call->cmd->floatArgs[1]) : 3;
    if (digits > 8) digits = 8;
    else if (digits < 0) digits = 0;
    std::string text = str::Format("%.*f", digits, static_cast<double>(value));
    int i = static_cast<int>(text.size());
    for (;;) {
        --i;
        if (i <= 0 || text[i] != '0') break;
    }
    text.resize(static_cast<std::size_t>(text[i] == '.' ? i : i + 1));
    *call->result.text = text;
    return true;
}

bool StrToFloat(Call* call) {
    *call->result.number = static_cast<float>(str::ToDouble(Arg(call, 0)));
    return true;
}

bool StrCat(Call* call) {
    Assign(call, Joined(call));
    return true;
}

// The length in characters (a UTF-8 sequence counts once).
bool StrLen(Call* call) {
    *call->result.number = static_cast<float>(text::CharacterCount(Arg(call, 0)));
    return true;
}

// One of the strings, by the game generator's next value modulo their count.
bool RandStr(Call* call) {
    std::uint32_t index = g_random.Next() % static_cast<std::uint32_t>(call->cmd->stringArgs.size());
    *call->result.text = Arg(call, static_cast<int>(index));
    return true;
}

bool StrHas(Call* call) {
    *call->result.number = std::strstr(Arg(call, 0).c_str(), Arg(call, 1).c_str()) ? 1.0f : 0.0f;
    return true;
}

// StrMid(text, start, count): start counts from 1 (from the end when negative: -1 is the last
// character), count 0 means the rest; a negative count leaves the result as it was. Start and count are
// characters (the bytes of each character's UTF-8 sequence).
bool StrMid(Call* call) {
    const std::string& string = Arg(call, 0);
    int length = text::CharacterCount(string);
    int start = TruncateOrMin(call->cmd->floatArgs[0]);
    int count = TruncateOrMin(call->cmd->floatArgs[1]);
    if (start < 0) start += length + 1;
    if (count == 0) count = length;
    else if (count < 0) return true;
    int lowest = length < start ? length : start;
    if (lowest < 1) start = 1;
    else if (length < start) start = length;
    int available = length - start + 1;
    if (count >= available) count = available;
    const char* bytes = string.c_str();
    const char* first = bytes + text::CharacterBytes(string, start - 1);
    const char* last = bytes + text::CharacterBytes(string, start - 1 + count);
    std::string part;
    if (first != last) part.assign(first, static_cast<std::size_t>(last - first));
    Assign(call, part);
    return true;
}

bool StrLower(Call* call) {
    Assign(call, CaseConverted(Arg(call, 0), false));
    return true;
}

bool StrUpper(Call* call) {
    Assign(call, CaseConverted(Arg(call, 0), true));
    return true;
}

// Say(text, ...): in a network game, the joined texts go out as a chat line from the server.
bool Say(Call* call) {
    if (IsMultiplayerClient()) {
        net::ServerSession* server = net::g_gameServer;
        if (server && !(server->flags7 & RefCounted::kDetached)) {
            std::string text = Joined(call);
            server->Announce(text, 0);
        }
    }
    return true;
}

// ConsoleString(text, ...) (extends the game's one-argument command): the joined texts, "\n", flush. One text: the
// game's own (false).
bool ConsoleString(Call* call) {
    if (call->cmd->stringArgs.size() == 1) return false;
    std::string text = Joined(call);
    (console::g_consoleOut << text << "\n").Flush();
    return true;
}

} // namespace scriptaddon
