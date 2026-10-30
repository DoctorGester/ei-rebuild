// The developer console: output streambuf, command executor, tokenizer and script lines.
// See docs/notes/console.md.
#include "console.h"
#include "app_helpers.h"
#include "world.h"
#include "world_glue.h"
#include "game_strings.h"
#include "ui.h"

#include "lib/file_io.h"

#include "ei/rebirth.h"
#include "game_singletons.h"
#include "lib/str.h"

#include <charconv>
#include <cstring>
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

CommandHandler& Command(CommandMap* map, const std::string& name) { return (*map)[name]; }

void EndLine(Stream& os) { (os << '\n').Flush(); }

void PrintLine(const char* text) { EndLine(g_consoleOut << text); }

// ---- Console streambuf --------------------------------------------------------------------------

void (*g_consoleLineObserver)(const char* line) = nullptr;

ConsoleStreambuf::ConsoleStreambuf(unsigned newFilter) : filter(newFilter) {}

void ConsoleStreambuf::Put(char c) {
    if (putNext < putEnd) buffer[putNext++] = c;
    else Overflow(static_cast<unsigned char>(c));
}

void ConsoleStreambuf::Write(std::string_view text) {
    for (char c : text) Put(c);
}

void ConsoleStreambuf::Overflow(int c) {
    if (filter == 0 || (g_filterMask & filter) != 0) {
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
    line = game::kEmptyText;
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

// Every call (re)registers all commands. The first token is the command; "@", "#" and
// "$" lines (cheat word given, not in a network game) become WorldScript lines. Other commands and
// their arguments are lower-cased; "quit"/"exit" quit; otherwise the public map is tried, then the
// cheat word, then (after it) the cheat map. Looking a command up inserts it (original behaviour).
void Execute(const std::string& text) {
    for (const CommandRegistration& r : kCommandRegistrations) {
        std::string name = r.name;
        Command(r.cheat ? &g_cheatCommands : &g_publicCommands, name) = r.handler;
    }

    std::vector<std::string> args = Tokenize(text);
    if (args.empty()) return;
    std::string command = args[0];
    args.erase(args.begin());
    bool local = !IsMultiplayerClient();
    // Rebirth's AddNewConsoleCommands: addon's commands, registered again on every call like the game's own.
    if (ei::rebirth::Enabled(ei::rebirth::Patch::AddNewConsoleCommands)) RegisterAddonCommands();
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
    for (std::string& arg : args) str::MakeLower(arg);
    if (str::Compare(command, "quit") == 0 || str::Compare(command, "exit") == 0) {
        app::QuitGame(0);
        return;
    }
    if (Command(&g_publicCommands, command)) {
        (Command(&g_publicCommands, command))(&args);
        return;
    }
    if (!g_cheatEnabled) {
        // The cheat word is kept XORed with 0x66 (up to the first byte that becomes zero).
        for (char& c : command) c ^= 0x66;
        command.resize(std::strlen(command.c_str()));
        if (str::Compare(command, game::consoletexts::CheatWord()) == 0) {
            g_cheatEnabled = true;
            PrintLine("Activated!");
            return;
        }
    } else if (Command(&g_cheatCommands, command)) {
        if (!IsMultiplayerClient()) (Command(&g_cheatCommands, command))(&args);
        return;
    }
    PrintLine("Unknown command");
}

// States: 0 between tokens, 1 in a token, 2 inside double quotes (which stay in the
// token).
std::vector<std::string> Tokenize(std::string_view source) {
    const std::string text(source);   // the scan below stops at the terminator
    std::vector<std::string> tokens;
    std::string token;
    int state = 0;
    const char* p = text.c_str();
    for (;;) {
        const char c = *p;
        if (c == 0) break;
        if (c == ' ') {
            if (state == 1) {
                state = 0;
                if (token.size() != 0) {
                    tokens.push_back(token);
                    token.clear();
                }
                ++p;
                continue;
            }
            if (state != 2) {
                ++p;
                continue;
            }
        } else if (c == '"') {
            state = state != 2 ? 2 : 1;
        } else if (state == 0) {
            state = 1;
        }
        token += c;
        ++p;
    }
    if (token.size() != 0) {
        tokens.push_back(token);
        token.clear();
    }
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
    if (!RunScriptText(line, 1, &error))
        EndLine(g_consoleOut << "Script error: " << error.text);
}

} // namespace console
