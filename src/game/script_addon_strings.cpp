// AddNewScriptCommands: addon's string functions and its two text commands (handlers
// 10011051-10011C65, 1001170B, 1001177D). String results are assigned to the CString addon gets
// (CString::operator=(const char*), 00700AF9) from a std::string addon builds first.
//
// FloatToStr formats with the C runtime's "%.<n>f" (addon's MSVC 2010 CRT; here the game's, the
// same float formatting code) and StrToFloat parses with atof (likewise the game's).
#include "script_addon_handlers.h"

#include "console.h"
#include "ei/hook.h"
#include "lib/crt.h"
#include "random.h"
#include "originals_library.h"

#include <windows.h>

#include <cstring>
#include <string>
#include <vector>

#pragma warning(disable : 4740) // naked forwarders

namespace scriptaddon {

namespace {

// 0057F440 on 007AFE20: whether this is a network game.
__declspec(naked) bool __fastcall NetworkIsMultiplayer(void*) EI_ORIGINAL(0x0057F440)
// 0041A310 on the game server (0079B5DC): sends a chat line from the server (second argument 0).
__declspec(naked) void __fastcall ServerSay(void*, void*, const char*, int) EI_ORIGINAL(0x0041A310)

IsaacRandom* GameRandom() { return reinterpret_cast<IsaacRandom*>(0x007A9D40); }
void* const kFlushManipulator = reinterpret_cast<void*>(0x00435000);

float* FloatResult(Call* call) { return static_cast<float*>(call->result); }
mfc::CString* StringResult(Call* call) { return static_cast<mfc::CString*>(call->result); }
const mfc::CString& Arg(Call* call, int i) { return call->cmd->stringArgs.data[i]; }
int Length(const mfc::CString& s) { return reinterpret_cast<const int*>(static_cast<const char*>(s))[-2]; }

// 10010D8F: every string argument appended in order (each by its CString length).
std::string Joined(Call* call) {
    std::string text;
    const MfcArray<mfc::CString>& strings = call->cmd->stringArgs;
    for (int i = 0; i < strings.size; ++i) text.append(static_cast<const char*>(strings.data[i]), Length(strings.data[i]));
    return text;
}

// std::string's c_str() assigned to the result (the text up to its first NUL).
void Assign(Call* call, const std::string& text) { *StringResult(call) = text.c_str(); }

// A NUL-terminated copy changed in place by CharLowerA / CharUpperA (the user's code page).
std::string CaseConverted(const char* text, bool upper) {
    std::vector<char> buffer(text, text + std::strlen(text) + 1);
    if (upper) CharUpperA(buffer.data());
    else CharLowerA(buffer.data());
    return std::string(buffer.data());
}

} // namespace

// The object's id in decimal ("%I64d" of the sign-extended id).
bool GetObjectStringID(Call* call) {
    CEngineObject* object = call->cmd->objectArgs.data[0]->object;
    if (!object) return InvalidArguments("GetObjectStringID");
    char text[64];
    gamecrt::sprintf(text, "%d", object->id_);
    Assign(call, text);
    return true;
}

// "%.<digits>f" (digits 3 by default, clamped to 0..8), then trailing zeros and a trailing '.'
// cut. The scan stops at index 0 without looking at it, so "100" with no decimals becomes "1".
bool FloatToStr(Call* call) {
    float value;
    Store(&value, call->cmd->floatArgs.data[0]);
    int digits = call->cmd->floatArgs.size >= 2 ? Ftol(call->cmd->floatArgs.data[1]) : 3;
    if (digits > 8) digits = 8;
    else if (digits < 0) digits = 0;
    char format[16];
    gamecrt::sprintf(format, "%%.%df", digits);
    char text[100];
    int i = gamecrt::sprintf(text, format, static_cast<double>(value));
    for (;;) {
        --i;
        if (i <= 0 || text[i] != '0') break;
    }
    if (text[i] == '.') text[i] = 0;
    else text[i + 1] = 0;
    *StringResult(call) = text;
    return true;
}

bool StrToFloat(Call* call) {
    Store(FloatResult(call), static_cast<float>(GameAtof(Arg(call, 0))));
    return true;
}

bool StrCat(Call* call) {
    Assign(call, Joined(call));
    return true;
}

bool StrLen(Call* call) {
    Store(FloatResult(call), static_cast<float>(Length(Arg(call, 0))));
    return true;
}

// One of the strings, by the game generator's next value modulo their count.
bool RandStr(Call* call) {
    std::uint32_t index = GameRandom()->Next() % static_cast<std::uint32_t>(call->cmd->stringArgs.size);
    *StringResult(call) = static_cast<const char*>(Arg(call, static_cast<int>(index)));
    return true;
}

bool StrHas(Call* call) {
    Store(FloatResult(call), std::strstr(Arg(call, 0), Arg(call, 1)) ? 1.0f : 0.0f);
    return true;
}

// StrMid(text, start, count): start counts from 1 (from the end when negative: -1 is the last
// character), count 0 means the rest; a negative count leaves the result as it was.
bool StrMid(Call* call) {
    const char* text = Arg(call, 0);
    int length = Length(Arg(call, 0));
    int start = Ftol(call->cmd->floatArgs.data[0]);
    int count = Ftol(call->cmd->floatArgs.data[1]);
    if (start < 0) start += length + 1;
    if (count == 0) count = length;
    else if (count < 0) return true;
    int lowest = length < start ? length : start;
    if (lowest < 1) start = 1;
    else if (length < start) start = length;
    int available = length - start + 1;
    if (count >= available) count = available;
    const char* first = text + start - 1;
    const char* last = text + start - 1 + count;
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
    if (NetworkIsMultiplayer(reinterpret_cast<void*>(0x007AFE20))) {
        void* server = *reinterpret_cast<void**>(0x0079B5DC);
        if (server && !(*reinterpret_cast<std::uint32_t*>(static_cast<char*>(server) + 4) & 0x80000000u)) {
            std::string text = Joined(call);
            ServerSay(server, nullptr, text.c_str(), 0);
        }
    }
    return true;
}

// ConsoleString(text, ...) (replaces the game's one-argument command): the joined texts, "\n",
// flush.
bool ConsoleString(Call* call) {
    std::string text = Joined(call);
    (console::Out() << text.c_str() << "\n").Apply(kFlushManipulator);
    return true;
}

} // namespace scriptaddon
