// The developer console: output streambuf, command executor, tokenizer and script lines.
// See docs/notes/console.md.
#include "console.h"

#include "app_helpers.h"
#include "game_singletons.h"
#include "game_strings.h"
#include "ui.h"
#include "world.h"
#include "lib/str.h"
#include "platform/platform.h"

#include <charconv>
#include <memory>
#include <string>
#include <vector>

namespace console {

// ---- Streams ------------------------------------------------------------------------------------

namespace {

template <typename T>
void WriteInteger(ConsoleStreambuf* buffer, T value) {
    char text[12];
    buffer->Write(std::string_view(text, std::to_chars(text, text + sizeof(text), value).ptr - text));
}

} // namespace

Stream& Stream::operator<<(const char* text) {
    buffer->Write(text);
    return *this;
}
Stream& Stream::operator<<(const std::string& text) { return *this << text.c_str(); }
Stream& Stream::operator<<(char c) {
    buffer->Put(c);
    return *this;
}
Stream& Stream::operator<<(int value) {
    WriteInteger(buffer, value);
    return *this;
}
Stream& Stream::operator<<(long value) {
    WriteInteger(buffer, value);
    return *this;
}
Stream& Stream::operator<<(unsigned value) {
    WriteInteger(buffer, value);
    return *this;
}
Stream& Stream::operator<<(double value) {
    buffer->Write(str::Format("%.6g", value));
    return *this;
}
Stream& Stream::Flush() {
    buffer->Sync();
    return *this;
}

// ---- Helpers ------------------------------------------------------------------------------------

CommandHandler FindCommand(const CommandMap& map, const std::string& name) {
    auto found = map.find(name);
    return found != map.end() ? found->second : nullptr;
}

void EndLine(Stream& os) { (os << '\n').Flush(); }

void PrintLine(const char* text) { EndLine(g_consoleOut << text); }

// ---- Console streambuf --------------------------------------------------------------------------

void (*g_consoleLineObserver)(const char* line) = nullptr;

ConsoleStreambuf::ConsoleStreambuf(LogCategory newCategory) : category(newCategory) {}

void ConsoleStreambuf::Put(char c) {
    if (putNext < putEnd) buffer[putNext++] = c;
    else Overflow(static_cast<unsigned char>(c));
}

void ConsoleStreambuf::Write(std::string_view text) {
    for (char c : text) Put(c);
}

void ConsoleStreambuf::Overflow(int c) {
    if (category == kLogConsole || (g_filterMask & category) != 0) {
        line.append(buffer, putNext);
        if (c != -1) line += static_cast<char>(c);
        putNext = 0;
        putEnd = sizeof(buffer);
    }
}

void ConsoleStreambuf::Sync() {
    Overflow(-1);
    if (g_consoleLineObserver) g_consoleLineObserver(line.c_str());
    g_interface.ConsoleAddText(line);
    line.clear();
}

// ---- Executor -----------------------------------------------------------------------------------

const CommandRegistration kCommandRegistrations[25] = {
    {true, "help", commands::Help},
    {false, "console", commands::ConsolePos},
    {true, "history", commands::History},
    {true, "filter", commands::Filter},
    {true, "listvar", commands::ListVar},
    {true, "loadvar", commands::LoadVar},
    {false, "lastfps", commands::LastFps},
    {false, "exec", commands::Exec},
    {false, "execute", commands::Exec},
    {true, "give", commands::Give},
    {true, "fadeout", commands::FadeOut},
    {true, "join", commands::Join},
    {true, "server", commands::HostServer},
    {true, "memusage", commands::MemUsage},
    {true, "lootall", commands::LootAll},
    {true, "net", commands::Net},
    {true, "disconnect", commands::Disconnect},
    {true, "generateacks", commands::GenerateAcks},
    {true, "days", commands::Days},
    {true, "partyresend", commands::PartyResend},
    {false, "kick", commands::Kick},
    {false, "ban", commands::Ban},
    {false, "fps", commands::Fps},
    {false, "rate", commands::Rate},
    {false, "localrate", commands::LocalRate},
};

namespace {

// The two maps: the commands always available and those after the cheat word. The game's commands, then Rebirth's
// over them.
struct CommandMaps {
    CommandMap publicCommands;
    CommandMap cheatCommands;

    CommandMaps() {
        for (const CommandRegistration& r : kCommandRegistrations) Register(r);
        for (const CommandRegistration& r : kAddonCommandRegistrations) Register(r);
    }

private:
    void Register(const CommandRegistration& r) { (r.cheat ? cheatCommands : publicCommands)[r.name] = r.handler; }
};

const CommandMaps& Maps() {
    static const CommandMaps maps;
    return maps;
}

} // namespace

// The first token is the command; "@", "#" and
// "$" lines (cheat word given, not in a network game) become WorldScript lines. Otherwise the command word is
// lower-cased (the arguments keep their case); "quit"/"exit" quit; else the public map is tried, then the cheat word,
// then (after it) the cheat map.
void Execute(const std::string& text) {
    std::vector<std::string> args = Tokenize(text);
    if (args.empty()) return;
    std::string command = args[0];
    args.erase(args.begin());
    bool local = !IsMultiplayerClient();
    char first = command[0];
    if (first == '@' || first == '#' || first == '$') {
        if (g_cheatEnabled && local) {
            std::string script = str::Mid(command, 1);
            if (first == '@')
                RunScriptLine(script, args, "WorldScript(", ")");
            else if (first == '#')
                RunScriptLine(script, args, "WorldScript( ConsoleFloat( ", " ))");
            else
                RunScriptLine(script, args, "WorldScript( ConsoleString( ", " ))");
        }
        return;
    }

    str::MakeLower(command);
    if (str::Compare(command, "quit") == 0 || str::Compare(command, "exit") == 0) {
        app::QuitGame(0);
        return;
    }
    if (CommandHandler handler = FindCommand(Maps().publicCommands, command)) {
        handler(args);
        return;
    }
    if (!g_cheatEnabled) {
        // The cheat word is kept XORed with 0x66; the whole command must match it.
        for (char& c : command) c ^= 0x66;
        if (command == game::consoletexts::CheatWord()) {
            g_cheatEnabled = true;
            PrintLine("Activated!");
            return;
        }
    } else if (CommandHandler handler = FindCommand(Maps().cheatCommands, command)) {
        if (!IsMultiplayerClient()) handler(args);
        return;
    }
    PrintLine("Unknown command");
}

// A token's quotes stay in it.
std::vector<std::string> Tokenize(std::string_view source) {
    enum class State { Between, InToken, Quoted };
    const std::string text(source);   // the scan below stops at the terminator
    std::vector<std::string> tokens;
    std::string token;
    State state = State::Between;
    for (const char c : text) {
        if (c == 0) break;
        if (c == ' ') {
            if (state == State::InToken) {
                state = State::Between;
                if (!token.empty()) {
                    tokens.push_back(token);
                    token.clear();
                }
                continue;
            }
            if (state != State::Quoted) continue;
        } else if (c == '"') {
            state = state != State::Quoted ? State::Quoted : State::InToken;
        } else if (state == State::Between) {
            state = State::InToken;
        }
        token += c;
    }
    if (!token.empty()) tokens.push_back(token);
    return tokens;
}

// The arguments are joined back with single spaces.
void RunScriptLine(const std::string& text, const std::vector<std::string>& args, const char* prefix, const char* suffix) {
    ScriptErrorInfo error{};
    std::string line = prefix + text;
    for (const std::string& arg : args) {
        line += ' ';
        line += arg;
    }
    line += suffix;
    if (!g_serverWorld.RunCommandScript(line, true, &error))
        EndLine(g_consoleOut << "Script error: " << error.text);
}

// ---- The streams --------------------------------------------------------------------------------

// The eight streambufs (one per message category) and the eight streams on them, plain globals of ours. A stream only
// names its streambuf (constant-initialised). See docs/notes/console.md.

namespace {

ConsoleStreambuf g_consoleBuf(kLogConsole);
ConsoleStreambuf g_allLogBuf(kLogAll);
ConsoleStreambuf g_netLogBuf(kLogNet);
ConsoleStreambuf g_unitAcksLogBuf(kLogUnitAcks);
ConsoleStreambuf g_rpgLogBuf(kLogRpg);
ConsoleStreambuf g_graphicsLogBuf(kLogGraphics);
ConsoleStreambuf g_eventLogBuf(kLogEvent);
ConsoleStreambuf g_aiLogBuf(kLogAi);

} // namespace

constinit Stream g_consoleOut(g_consoleBuf);
constinit Stream g_allLogOut(g_allLogBuf);
constinit Stream g_unitAcksLogOut(g_unitAcksLogBuf);
constinit Stream g_netLogOut(g_netLogBuf);
constinit Stream g_rpgLogOut(g_rpgLogBuf);
constinit Stream g_graphicsLogOut(g_graphicsLogBuf);
constinit Stream g_eventLogOut(g_eventLogBuf);
constinit Stream g_aiLogOut(g_aiLogBuf);

} // namespace console
