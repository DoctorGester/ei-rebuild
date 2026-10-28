// AddNewScriptCommands: addon's string functions and its two text commands. String results are
// assigned to the string addon gets (its c_str(), as CString::operator=(const char*) took it) from a
// std::string addon builds first.
// FloatToStr formats with the C runtime's "%.<n>f" and StrToFloat parses with atof.
#include "net_classes.h"
#include "world.h"
#include "script_addon_handlers.h"

#include "console.h"
#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/str.h"
#include "random.h"
#include "game_data.h"
#include "system_text.h"

#include <cstdio>
#include <cstdlib>
#include <windows.h>

#include <cstring>
#include <string>
#include <vector>

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
void Assign(Call* call, const std::string& text) { *call->StringResult() = text.c_str(); }

// A NUL-terminated copy changed in place by CharLowerA / CharUpperA (the user's code page).
std::string CaseConverted(const char* text, bool upper) {
    std::vector<char> buffer(text, text + std::strlen(text) + 1);
    if (upper) text::utf8::CharUpperA(buffer.data());
    else text::utf8::CharLowerA(buffer.data());
    return std::string(buffer.data());
}

} // namespace

// The object's id in decimal ("%I64d" of the sign-extended id).
bool GetObjectStringID(Call* call) {
    CEngineObject* object = call->cmd->objectArgs[0]->object;
    if (!object) return InvalidArguments("GetObjectStringID");
    char text[64];
    std::snprintf(text, sizeof(text), "%d", object->id_);
    Assign(call, text);
    return true;
}

// "%.<digits>f" (digits 3 by default, clamped to 0..8), then trailing zeros and a trailing '.'
// cut. The scan stops at index 0 without looking at it, so "100" with no decimals becomes "1".
bool FloatToStr(Call* call) {
    float value;
    Store(&value, call->cmd->floatArgs[0]);
    int digits = std::ssize(call->cmd->floatArgs) >= 2 ? Ftol(call->cmd->floatArgs[1]) : 3;
    if (digits > 8) digits = 8;
    else if (digits < 0) digits = 0;
    std::string text = str::Format("%.*f", digits, static_cast<double>(value));
    int i = static_cast<int>(text.size());
    for (;;) {
        --i;
        if (i <= 0 || text[i] != '0') break;
    }
    text.resize(static_cast<std::size_t>(text[i] == '.' ? i : i + 1));
    *call->StringResult() = text;
    return true;
}

bool StrToFloat(Call* call) {
    Store(call->FloatResult(), static_cast<float>(str::ToDouble(Arg(call, 0))));
    return true;
}

bool StrCat(Call* call) {
    Assign(call, Joined(call));
    return true;
}

// The length in characters (a UTF-8 sequence counts once).
bool StrLen(Call* call) {
    Store(call->FloatResult(), static_cast<float>(text::CharacterCount(Arg(call, 0))));
    return true;
}

// One of the strings, by the game generator's next value modulo their count.
bool RandStr(Call* call) {
    std::uint32_t index = IsaacRandom::Global().Next() % static_cast<std::uint32_t>(call->cmd->stringArgs.size());
    *call->StringResult() = Arg(call, static_cast<int>(index));
    return true;
}

bool StrHas(Call* call) {
    Store(call->FloatResult(), std::strstr(Arg(call, 0).c_str(), Arg(call, 1).c_str()) ? 1.0f : 0.0f);
    return true;
}

// StrMid(text, start, count): start counts from 1 (from the end when negative: -1 is the last
// character), count 0 means the rest; a negative count leaves the result as it was. Start and count are
// characters (the bytes of each character's UTF-8 sequence).
bool StrMid(Call* call) {
    const std::string& string = Arg(call, 0);
    int length = text::CharacterCount(string);
    int start = Ftol(call->cmd->floatArgs[0]);
    int count = Ftol(call->cmd->floatArgs[1]);
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
    Assign(call, CaseConverted(Arg(call, 0).c_str(), false));
    return true;
}

bool StrUpper(Call* call) {
    Assign(call, CaseConverted(Arg(call, 0).c_str(), true));
    return true;
}

// Say(text, ...): in a network game, the joined texts go out as a chat line from the server.
bool Say(Call* call) {
    if (IsMultiplayerClient()) {
        net::ServerSession* server = net::GameServer();
        if (server && !(server->flags7_ & CRefCounted::kDetached)) {
            std::string text = Joined(call);
            server->Announce(text.c_str(), 0);
        }
    }
    return true;
}

// ConsoleString(text, ...) (replaces the game's one-argument command): the joined texts, "\n",
// flush.
bool ConsoleString(Call* call) {
    std::string text = Joined(call);
    (console::Out() << text.c_str() << "\n").Flush();
    return true;
}

} // namespace scriptaddon
