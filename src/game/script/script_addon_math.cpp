// Rebirth's arithmetic script functions. Each reads its float arguments and stores the result as a float.
#include "game/script/script_addon_handlers.h"

#include "engine/core/asset_text.h"
#include "game/console/console.h"
#include "game/net/net_session.h"
#include "engine/core/random.h"
#include "game/world/world.h"
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

bool Sin(Call* call) {
    float x = ScriptArgs(call->cmd).Float();
    *call->result.number = static_cast<float>(std::sin(x));
    return true;
}

bool Cos(Call* call) {
    float x = ScriptArgs(call->cmd).Float();
    *call->result.number = static_cast<float>(std::cos(x));
    return true;
}

bool Pow(Call* call) {
    ScriptArgs args(call->cmd);
    float x = args.Float();
    float y = args.Float();
    *call->result.number = static_cast<float>(std::pow(x, y));
    return true;
}

// The signature has two floats, but the handler reads four, as Rebirth's does: there the last two came from past the
// end of the argument array (heap bytes, any value from run to run), here they are 0.
bool DistancePointPoint(Call* call) {
    ScriptArgs args(call->cmd);
    float f[4] = {};
    for (int i = 0, n = std::min(args.FloatCount(), 4); i < n; ++i) f[i] = args.Float();
    *call->result.number = Distance(f[0], f[1], f[2], f[3]);
    return true;
}

// Add / Mul of more than two: the sum / product in argument order; of two, the game's own (false).
bool Add(Call* call) {
    ScriptArgs args(call->cmd);
    if (args.FloatCount() == 2) return false;
    float sum = 0.0f;
    for (int i = 0, n = args.FloatCount(); i < n; ++i) sum = args.Float() + sum;
    *call->result.number = sum;
    return true;
}

bool Mul(Call* call) {
    ScriptArgs args(call->cmd);
    if (args.FloatCount() == 2) return false;
    float product = 1.0f;
    for (int i = 0, n = args.FloatCount(); i < n; ++i) product = args.Float() * product;
    *call->result.number = product;
    return true;
}

// True unless an argument truncates to 0.
bool And(Call* call) {
    ScriptArgs args(call->cmd);
    float result = 1.0f;
    for (int i = 0, n = args.FloatCount(); i < n; ++i)
        if (TruncateOrMin(args.Float()) == 0) {
            result = 0.0f;
            break;
        }
    *call->result.number = result;
    return true;
}

// True when an argument does not truncate to 0.
bool Or(Call* call) {
    ScriptArgs args(call->cmd);
    float result = 0.0f;
    for (int i = 0, n = args.FloatCount(); i < n; ++i)
        if (TruncateOrMin(args.Float()) != 0) {
            result = 1.0f;
            break;
        }
    *call->result.number = result;
    return true;
}

// An argument replaces the maximum unless it is (ordered) less: equal and NaN arguments do.
bool Max(Call* call) {
    ScriptArgs args(call->cmd);
    float m = args.Float();
    for (int i = 1, n = args.FloatCount(); i < n; ++i)
        if (const float next = args.Float(); !(next < m)) m = next;
    *call->result.number = m;
    return true;
}

// An argument replaces the minimum unless it is (ordered) greater.
bool Min(Call* call) {
    ScriptArgs args(call->cmd);
    float m = args.Float();
    for (int i = 1, n = args.FloatCount(); i < n; ++i)
        if (const float next = args.Float(); !(next > m)) m = next;
    *call->result.number = m;
    return true;
}

// Negated only when below 0 (so -0 and NaN stay).
bool Abs(Call* call) {
    float x = ScriptArgs(call->cmd).Float();
    float result = x < 0.0f ? -x : x;
    *call->result.number = result;
    return true;
}

// floor(x + 0.5) from 0 up (and for NaN), ceil(x - 0.5) below; the sum is rounded to float first.
bool Round(Call* call) {
    float x = ScriptArgs(call->cmd).Float();
    *call->result.number = x < 0.0f ? std::ceil(x - 0.5f) : std::floor(x + 0.5f);
    return true;
}

bool Ceil(Call* call) {
    float x = ScriptArgs(call->cmd).Float();
    *call->result.number = std::ceil(x);
    return true;
}

bool Floor(Call* call) {
    float x = ScriptArgs(call->cmd).Float();
    *call->result.number = std::floor(x);
    return true;
}

// Random(max) stays the game's (the handler declines one argument); Random(min, max) is the game
// generator's Range when max > min, else min.
bool Random(Call* call) {
    ScriptArgs args(call->cmd);
    if (args.FloatCount() == 1) return false;
    float low = args.Float();
    float high = args.Float();
    if (high > low) {
        *call->result.number = g_random.Range(low, high);
    } else {
        *call->result.number = low;
    }
    return true;
}

} // namespace scriptaddon

// --- Script addon strings ----------------------------------------------------------------------------

// Rebirth's string functions and its two text commands. A string result is cut at its first NUL, as the game's
// string assignment did. FloatToStr formats with the C runtime's "%.<n>f" and StrToFloat parses with atof.

namespace scriptaddon {

namespace {

// Every string argument appended in order (each by its length).
std::string Joined(Call* call) {
    ScriptArgs args(call->cmd);
    std::string text;
    for (int i = 0, n = args.TextCount(); i < n; ++i) text += args.Text();
    return text;
}

// The text up to its first NUL, assigned to the result.
void Assign(Call* call, const std::string& text) { *call->result.text = text.substr(0, text.find('\0')); }

// A NUL-terminated copy changed in place by lib/text_case.h's case.
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
    EngineObject* object = ScriptArgs(call->cmd).Object().object;
    if (!object) return InvalidArguments("GetObjectStringID");
    char text[64];
    std::snprintf(text, sizeof(text), "%d", object->id);
    Assign(call, text);
    return true;
}

// "%.<digits>f" (digits 3 by default, clamped to 0..8), then trailing zeros and a trailing '.'
// cut. The scan stops at index 0 without looking at it, so "100" with no decimals becomes "1".
bool FloatToStr(Call* call) {
    ScriptArgs args(call->cmd);
    float value = args.Float();
    int digits = args.FloatCount() >= 2 ? TruncateOrMin(args.Float()) : 3;
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
    *call->result.number = str::ToFloat(ScriptArgs(call->cmd).Text());
    return true;
}

bool StrCat(Call* call) {
    Assign(call, Joined(call));
    return true;
}

// The length in characters (a UTF-8 sequence counts once).
bool StrLen(Call* call) {
    *call->result.number = static_cast<float>(text::CharacterCount(ScriptArgs(call->cmd).Text()));
    return true;
}

// One of the strings, by the game generator's next value modulo their count.
bool RandStr(Call* call) {
    const std::vector<std::string>& texts = call->cmd->stringArgs;
    std::uint32_t index = g_random.Next() % static_cast<std::uint32_t>(texts.size());
    *call->result.text = texts[index];
    return true;
}

bool StrHas(Call* call) {
    ScriptArgs args(call->cmd);
    const std::string& text = args.Text();
    const std::string& part = args.Text();
    *call->result.number = ScriptBool(text.find(part) != std::string::npos);
    return true;
}

// StrMid(text, start, count): start counts from 1 (from the end when negative: -1 is the last
// character), count 0 means the rest; a negative count leaves the result as it was. Start and count are
// characters (the bytes of each character's UTF-8 sequence).
bool StrMid(Call* call) {
    ScriptArgs args(call->cmd);
    const std::string& string = args.Text();
    int length = text::CharacterCount(string);
    int start = TruncateOrMin(args.Float());
    int count = TruncateOrMin(args.Float());
    if (start < 0) start += length + 1;
    if (count == 0) count = length;
    else if (count < 0) return true;
    int lowest = length < start ? length : start;
    if (lowest < 1) start = 1;
    else if (length < start) start = length;
    int available = length - start + 1;
    if (count >= available) count = available;
    const auto first = static_cast<std::size_t>(text::CharacterBytes(string, start - 1));
    const auto last = static_cast<std::size_t>(text::CharacterBytes(string, start - 1 + count));
    Assign(call, string.substr(first, last - first));
    return true;
}

bool StrLower(Call* call) {
    Assign(call, CaseConverted(ScriptArgs(call->cmd).Text(), false));
    return true;
}

bool StrUpper(Call* call) {
    Assign(call, CaseConverted(ScriptArgs(call->cmd).Text(), true));
    return true;
}

// Say(text, ...): in a network game, the joined texts go out as a chat line from the server.
bool Say(Call* call) {
    if (IsMultiplayerClient()) {
        if (IsLive(net::g_gameServer)) {
            std::string text = Joined(call);
            net::g_gameServer->Announce(text, 0);
        }
    }
    return true;
}

// ConsoleString(text, ...) (extends the game's one-argument command): the joined texts, "\n", flush. One text: the
// game's own (false).
bool ConsoleString(Call* call) {
    if (ScriptArgs(call->cmd).TextCount() == 1) return false;
    std::string text = Joined(call);
    (console::g_consoleOut << text << "\n").Flush();
    return true;
}

} // namespace scriptaddon
