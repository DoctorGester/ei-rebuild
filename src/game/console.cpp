// The developer console: output streambuf, command executor, tokenizer and script lines.
// See docs/notes/console.md.
#include "console.h"
#include "app_helpers.h"
#include "world.h"
#include "world_glue.h"
#include "game_strings.h"
#include "ui.h"

#include "../lib/crt.h"
#include "lib/mfc_file.h"

#include "ei/rebirth.h"
#include "lib/crt.h"
#include "game_data.h"
#include "game_singletons.h"
#include "lib/str.h"

#include <charconv>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace console {

// ---- Helpers ------------------------------------------------------------------------------------

CStdioFile::CStdioFile() = default;
CStdioFile::CStdioFile(const char* name, unsigned flags) : file_(name, flags) {}
CStdioFile::~CStdioFile() = default;
mfc::BOOL CStdioFile::Open(std::string_view name, unsigned flags, mfc::impl::CFileException* error) {
    return file_.Open(std::string(name).c_str(), flags, error);
}
// MFC's CStdioFile::ReadString(CString&), into a string.
mfc::BOOL CStdioFile::ReadString(std::string& line) { return file_.ReadLine(line); }
char* CStdioFile::ReadString(char* buffer, unsigned size) { return file_.ReadString(buffer, size); }
void CStdioFile::WriteString(const char* text) { file_.WriteString(text); }
void CStdioFile::Close() { file_.Close(); }

// ---- Streams ------------------------------------------------------------------------------------

namespace {

template <typename T>
void WriteInteger(ConsoleStreambuf* buffer, T value) {
    char text[12];
    buffer->Write(std::string_view(text, std::to_chars(text, text + sizeof(text), value).ptr - text));
}

} // namespace

Stream& Stream::operator<<(const char* text) {
    buffer_->Write(text);
    return *this;
}
Stream& Stream::operator<<(char c) {
    buffer_->Put(c);
    return *this;
}
Stream& Stream::operator<<(int value) {
    WriteInteger(buffer_, value);
    return *this;
}
Stream& Stream::operator<<(long value) {
    WriteInteger(buffer_, value);
    return *this;
}
Stream& Stream::operator<<(unsigned value) {
    WriteInteger(buffer_, value);
    return *this;
}
Stream& Stream::operator<<(double value) {
    char text[32];   // "%.6g": at most 13 characters ("-1.23457e+308")
    buffer_->Write(std::string_view(text, static_cast<std::size_t>(gamecrt::sprintf(text, "%.6g", value))));
    return *this;
}
Stream& Stream::Flush() {
    buffer_->Sync();
    return *this;
}

CommandHandler& Command(CommandMap* map, const std::string& name) { return (*map)[name]; }

namespace {

int Mbscmp(const char* a, const char* b) {
    return gamecrt::mbscmp(a, b);
}

bool NetworkGame() { return IsMultiplayerClient(); }

} // namespace

void EndLine(Stream& os) { (os << '\n').Flush(); }

void PrintLine(const char* text) { EndLine(Out() << text); }

// ---- Console streambuf --------------------------------------------------------------------------

void (*g_consoleLineObserver)(const char* line) = nullptr;

ConsoleStreambuf::ConsoleStreambuf(unsigned filter) : filter_(filter) {}

void ConsoleStreambuf::Put(char c) {
    if (putNext_ < putEnd_) buffer_[putNext_++] = c;
    else Overflow(static_cast<unsigned char>(c));
}

void ConsoleStreambuf::Write(std::string_view text) {
    for (char c : text) Put(c);
}

void ConsoleStreambuf::Overflow(int c) {
    if (filter_ == 0 || (g_filterMask & filter_) != 0) {
        line_.append(buffer_, putNext_);
        if (c != -1) line_ += static_cast<char>(c);
        putNext_ = 0;
        putEnd_ = sizeof(buffer_);
    }
}

void ConsoleStreambuf::Sync() {
    Overflow(-1);
    if (g_consoleLineObserver) g_consoleLineObserver(line_.c_str());
    TheInterface()->ConsoleAddText(line_.c_str());
    line_ = game::EmptyText();
}

// ---- Executor -----------------------------------------------------------------------------------

const CommandRegistration kCommandRegistrations[27] = {
    {true, "help", commands::Help},
    {true, "debuginfo", commands::DebugInfo},
    {false, "console", commands::ConsolePos},
    {true, "history", commands::History},
    {true, "filter", commands::Filter},
    {true, "show", commands::Show},
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
void Execute(const char* text) {
    for (const CommandRegistration& r : kCommandRegistrations) {
        std::string name = r.name;
        Command(r.cheat ? CheatCommands() : PublicCommands(), name) = r.handler;
    }

    Arguments args;
    Tokenize(text, &args);
    if (args.empty()) return;
    std::string command = args[0];
    args.erase(args.begin());
    bool local = !NetworkGame();
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
    if (Mbscmp(command.c_str(), "quit") == 0 || Mbscmp(command.c_str(), "exit") == 0) {
        app::QuitGame(0);
        return;
    }
    if (Command(PublicCommands(), command)) {
        (Command(PublicCommands(), command))(&args);
        return;
    }
    if (!g_cheatEnabled) {
        // The cheat word is kept XORed with 0x66 (up to the first byte that becomes zero).
        for (char& c : command) c ^= 0x66;
        command.resize(std::strlen(command.c_str()));
        if (std::strcmp(command.c_str(), game::consoletexts::CheatWord()) == 0) {
            g_cheatEnabled = true;
            PrintLine("Activated!");
            return;
        }
    } else if (Command(CheatCommands(), command)) {
        if (!NetworkGame()) (Command(CheatCommands(), command))(&args);
        return;
    }
    PrintLine("Unknown command");
}

// States: 0 between tokens, 1 in a token, 2 inside double quotes (which stay in the
// token). Double-byte characters are taken whole.
int Tokenize(const char* text, std::vector<std::string>* out) {
    std::string token;
    int state = 0;
    out->clear();
    const char* p = text;
    for (;;) {
        int c = 0;
        const char* low = p;
        if (libcrt::Ismbblead(static_cast<unsigned char>(*p))) {
            c = static_cast<signed char>(*p) * 256;
            low = p + 1;
        }
        c += static_cast<signed char>(*low);
        if (c == 0) break;
        if (c == ' ') {
            if (state == 1) {
                state = 0;
                if (token.size() != 0) {
                    out->push_back(token);
                    token.clear();
                }
                p = gamecrt::mbsinc(p);
                continue;
            }
            if (state != 2) {
                p = gamecrt::mbsinc(p);
                continue;
            }
        } else if (c == '"') {
            state = state != 2 ? 2 : 1;
        } else if (state == 0) {
            state = 1;
        }
        // The character's bytes (a zero byte ends the text a CString kept).
        token += p[0];
        if (libcrt::Isleadbyte(static_cast<signed char>(*p)) && p[1] != '\0') token += p[1];
        p = gamecrt::mbsinc(p);
    }
    if (token.size() != 0) {
        out->push_back(token);
        token.clear();
    }
    return 1;
}

// The arguments are joined back with single spaces.
void RunScriptLine(const std::string& text, const Arguments& args, const char* prefix, const char* suffix) {
    ScriptErrorInfo error{};
    std::string line = prefix + text;
    for (const std::string& arg : args) {
        line += ' ';
        line += arg;
    }
    line += suffix;
    if (!RunScriptText(line, 1, &error))
        EndLine(Out() << "Script error: " << error.text.c_str());
}

} // namespace console
