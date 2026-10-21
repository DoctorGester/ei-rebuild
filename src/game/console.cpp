// The developer console: output streambuf, command executor, tokenizer and script lines.
// game.exe 0x005DC150-0x005DD91F and 0x004DC200. See docs/notes/console.md.
#include "console.h"
#include "app_helpers.h"
#include "world.h"
#include "world_glue.h"
#include "game_strings.h"
#include "ui.h"

#include "../lib/crt.h"
#include "lib/mfc_file.h"
#include "lib/crt_iostream.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "originals_engine.h"
#include "originals_library.h"
#include "originals_world.h"
#include "library_trees.h"
#include "game_data.h"
#include "game_singletons.h"
#include "lib/mfc_string.h"

#include <cstring>

#pragma warning(disable : 4740) // naked forwarders: no optimisation to suppress

namespace console {

// ---- Library forwarders -------------------------------------------------------------------------

void Str::AppendChar(char a0) { *reinterpret_cast<mfc::CString*>(this) += a0; }
void Str::AppendText(const char* a0) { *reinterpret_cast<mfc::CString*>((void*)this) += a0; }
void Str::TrimLeft(char a0) { reinterpret_cast<mfc::CString*>(this)->TrimLeft(a0); }
void Str::TrimRight(char a0) { reinterpret_cast<mfc::CString*>(this)->TrimRight(a0); }


// The library's CStdioFile (lib/mfc_file.h) has this layout.
mfc::impl::CStdioFile* CStdioFile::Impl() { return reinterpret_cast<mfc::impl::CStdioFile*>(this); }
CStdioFile::CStdioFile() { Impl()->Construct(); }                                                   // 00701B79
CStdioFile::CStdioFile(const char* name, unsigned flags) { Impl()->ConstructOpen(name, flags); }    // 00701BD4
CStdioFile::~CStdioFile() { Impl()->Destruct(); }                                                   // 00701C6D
mfc::BOOL CStdioFile::Open(const char* name, unsigned flags, void* error) {                         // 00701CB0
    return Impl()->Open(name, flags, static_cast<mfc::impl::CFileException*>(error));
}
mfc::BOOL CStdioFile::ReadString(mfc::CString& line) { return Impl()->ReadLine(reinterpret_cast<mfc::impl::CString&>(line)); }   // 00701E93
char* CStdioFile::ReadString(char* buffer, unsigned size) { return Impl()->ReadString(buffer, size); }   // 00701E4E
void CStdioFile::WriteString(const char* text) { Impl()->WriteString(text); }                       // 00701E23
void CStdioFile::Close() { Impl()->Close(); }                                                       // 00701FE0

GameOStream& GameOStream::operator<<(const char* text) { PutText(text); return *this; }
GameOStream& GameOStream::operator<<(char c) { PutChar(c); return *this; }
GameOStream& GameOStream::operator<<(int a0) { PutLong(static_cast<long>(a0)); return *this; }
GameOStream& GameOStream::operator<<(double v) { PutDouble(v); return *this; }
GameOStream& GameOStream::Apply(libcrt::Manipulator manipulator) { libcrt::Ostream::Apply(manipulator); return *this; }


CommandHandler& Command(CommandMap* map, const mfc::CString& name) { return *lib_trees::Commands::Subscript(map, nullptr, &name); }

namespace {

// Game CRT.

int Mbscmp(const char* a, const char* b) {
    return gamecrt::mbscmp(a, b);
}

// 007C7D50: the interface manager (ConsoleAddText hands a line to its console window, if any).

// 0057F440 (called with the game at 007AFE20 in ECX): a network game is running (server object
// 0079B5D8 present and active).
bool NetworkGame() { return IsMultiplayerClient(); }

// 00455F20: posts WM_QUIT (when the game window exists); the exit code in ECX.



} // namespace

void EndLine(GameOStream& os) { (os << '\n').Apply(libcrt::FlushManipulator); }

void PrintLine(const char* text) { EndLine(Out() << text); }

// ---- Console streambuf --------------------------------------------------------------------------

// 005DC200
ConsoleStreambuf* ConsoleStreambuf::Construct(unsigned filter) {
    Base()->Construct(buffer_, sizeof(buffer_));   // 006FCAD2
    filter_ = filter;
    ::new (&line_) mfc::CString;   // the shared empty string
    ei::SetVtable(this, kVtableAddress);
    return this;
}
EI_REPLACE_METHOD(0x005DC200, ConsoleStreambuf::Construct);

// 005DC260
void ConsoleStreambuf::Destruct() {
    ei::SetVtable(this, kVtableAddress);
    line_.~CString();
    Base()->Destruct();   // 006FCB35
}
EI_REPLACE_METHOD(0x005DC260, ConsoleStreambuf::Destruct);

// 005DC240
ConsoleStreambuf* ConsoleStreambuf::DeletingDestruct(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x005DC240, ConsoleStreambuf::DeletingDestruct);

// 005DC150: moves the put area (and `c`, unless EOF) into the line, when this streambuf's
// category is shown; a hidden category keeps its put area as it is. Always returns 0.
int ConsoleStreambuf::Overflow(int c) {
    if (filter_ == 0 || (FilterMask() & filter_) != 0) {
        const char* end = pptr_;
        for (const char* p = pbase_; p < end; ++p) Ops(line_).AppendChar(*p);
        if (c != -1) Ops(line_).AppendChar(static_cast<char>(c));
        pbase_ = base_;
        pptr_ = base_;
        epptr_ = ebuf_;
    }
    return 0;
}
EI_REPLACE_METHOD(0x005DC150, ConsoleStreambuf::Overflow);

// 005DC1B0: overflow(EOF) through the vtable, the line to the console window, then empty it.
int ConsoleStreambuf::Sync() {
    using OverflowFn = int(__thiscall*)(ConsoleStreambuf*, int);
    ei::VirtualSlot<OverflowFn>(vtable_, 7)(this, -1);
    TheInterface()->ConsoleAddText(line_);
    line_ = game::EmptyText();   // ""
    return 0;
}
EI_REPLACE_METHOD(0x005DC1B0, ConsoleStreambuf::Sync);

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

// 005DC620. Every call (re)registers all commands. The first token is the command; "@", "#" and
// "$" lines (cheat word given, not in a network game) become WorldScript lines. Other commands and
// their arguments are lower-cased; "quit"/"exit" quit; otherwise the public map is tried, then the
// cheat word, then (after it) the cheat map. Looking a command up inserts it (original behaviour).
void __fastcall Execute(const char* text) {
    for (const CommandRegistration& r : kCommandRegistrations) {
        mfc::CString name(r.name);
        Command(r.cheat ? CheatCommands() : PublicCommands(), name) = r.handler;
    }

    mfc::CString command;
    CStringArray args;
    Tokenize(text, &args);
    if (args.GetSize() == 0) return;
    command = args[0];
    args.RemoveAt(0, 1);
    bool local = !NetworkGame();
    // Rebirth's AddNewConsoleCommands (context hook at 005DD565): addon's commands, registered
    // again on every call like the game's own.
    if (ei::rebirth::Enabled(ei::rebirth::Patch::AddNewConsoleCommands)) RegisterAddonCommands();
    char first = static_cast<const char*>(command)[0];
    if (first == '@' || first == '#' || first == '$') {
        if (CheatEnabled() && local) {
            mfc::CString script = Ops(command).Mid(1);
            if (first == '@')
                RunScriptLine(&script, &args, "WorldScript(", ")");
            else if (first == '#')
                RunScriptLine(&script, &args, "WorldScript( ConsoleFloat( ", " ))");
            else
                RunScriptLine(&script, &args, "WorldScript( ConsoleString( ", " ))");
        }
        return;
    }

    Ops(command).MakeLower();
    for (int i = 0; i < args.GetSize(); ++i) Ops(args[i]).MakeLower();
    if (Mbscmp(command, "quit") == 0 || Mbscmp(command, "exit") == 0) {
        app::QuitGame(0);
        return;
    }
    if (Command(PublicCommands(), command)) {
        (Command(PublicCommands(), command))(&args);
        return;
    }
    if (!CheatEnabled()) {
        char* p = Ops(command).GetBuffer(12);
        for (; *p; ++p) *p ^= 0x66;
        Ops(command).ReleaseBuffer(-1);
        if (std::strcmp(command, game::consoletexts::CheatWord()) == 0) {
            CheatEnabled() = true;
            PrintLine("Activated!");
            return;
        }
    } else if (Command(CheatCommands(), command)) {
        if (!NetworkGame()) (Command(CheatCommands(), command))(&args);
        return;
    }
    PrintLine("Unknown command");
}
EI_REPLACE(0x005DC620, Execute);

// 004DC200. States: 0 between tokens, 1 in a token, 2 inside double quotes (which stay in the
// token). Double-byte characters are taken whole.
int __fastcall Tokenize(const char* text, CStringArray* out) {
    mfc::CString token;
    mfc::CString character;
    int state = 0;
    out->SetSize(0, -1);
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
                if (Length(token) != 0) {
                    out->SetAtGrow(out->GetSize(), token);
                    token.Empty();
                }
                p = MbsInc(p);
                continue;
            }
            if (state != 2) {
                p = MbsInc(p);
                continue;
            }
        } else if (c == '"') {
            state = state != 2 ? 2 : 1;
        } else if (state == 0) {
            state = 1;
        }
        if (libcrt::Isleadbyte(static_cast<signed char>(*p))) {
            Ops(character).AssignChar(p[0]);
            Ops(character).AppendChar(p[1]);
        } else {
            Ops(character).AssignChar(p[0]);
        }
        token += character;
        p = MbsInc(p);
    }
    if (Length(token) != 0) {
        out->SetAtGrow(out->GetSize(), token);
        token.Empty();
    }
    return 1;
}
EI_REWRITTEN(0x004DC200, Tokenize);

// 005DD800. The arguments are joined back with single spaces.
void __fastcall RunScriptLine(const mfc::CString* text, const CStringArray* args, const char* prefix,
                              const char* suffix) {
    ScriptErrorInfo error{};
    mfc::CString line(prefix);
    line += *text;
    for (int i = 0; i < args->GetSize(); ++i) {
        Ops(line).AppendText(" ");
        mfc::CString arg((*args)[i]);
        line += arg;
    }
    Ops(line).AppendText(suffix);
    if (!RunScriptText(&line, 1, &error))
        EndLine(Out() << "Script error: " << static_cast<const char*>(error.text));
}
EI_REWRITTEN(0x005DD800, RunScriptLine);

} // namespace console
