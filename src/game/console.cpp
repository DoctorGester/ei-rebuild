// The developer console: output streambuf, command executor, tokenizer and script lines.
// game.exe 0x005DC150-0x005DD91F and 0x004DC200. See docs/notes/console.md.
#include "console.h"
#include "game_strings.h"

#include "../lib/crt.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "originals_engine.h"
#include "originals_library.h"
#include "originals_world.h"

#include <cstring>

#pragma warning(disable : 4740) // naked forwarders: no optimisation to suppress

namespace console {

// ---- Library forwarders -------------------------------------------------------------------------

void Str::AppendChar(char a0) { CStringAppendCharOriginal((void*)this, nullptr, a0); }
void Str::AppendText(const char* a0) { CStringAppendTextOriginal((void*)this, nullptr, a0); }
void Str::TrimLeft(char a0) { CStringTrimLeftCharOriginal((void*)this, nullptr, a0); }
void Str::TrimRight(char a0) { CStringTrimRightCharOriginal((void*)this, nullptr, a0); }


__declspec(naked) CStdioFile::CStdioFile() EI_ORIGINAL(0x00701B79)
__declspec(naked) CStdioFile::CStdioFile(const char*, unsigned) EI_ORIGINAL(0x00701BD4)
__declspec(naked) CStdioFile::~CStdioFile() EI_ORIGINAL(0x00701C6D)
__declspec(naked) mfc::BOOL CStdioFile::Open(const char*, unsigned, void*) EI_ORIGINAL(0x00701CB0)
__declspec(naked) mfc::BOOL CStdioFile::ReadString(mfc::CString&) EI_ORIGINAL(0x00701E93)
__declspec(naked) char* CStdioFile::ReadString(char*, unsigned) EI_ORIGINAL(0x00701E4E)
__declspec(naked) void CStdioFile::WriteString(const char*) EI_ORIGINAL(0x00701E23)
__declspec(naked) void CStdioFile::Close() EI_ORIGINAL(0x00701FE0)

GameOStream& GameOStream::operator<<(const char* text) { return *static_cast<GameOStream*>(OstreamPutString(this, nullptr, text)); }
GameOStream& GameOStream::operator<<(char c) { return *static_cast<GameOStream*>(OstreamPutChar(this, nullptr, static_cast<char>(c))); }
GameOStream& GameOStream::operator<<(int a0) { return *static_cast<GameOStream*>(OstreamPutSignedLongOriginal(this, nullptr, static_cast<long>(a0))); }
GameOStream& GameOStream::operator<<(double v) { return *static_cast<GameOStream*>(OstreamPutDoubleOriginal(this, nullptr, v)); }
GameOStream& GameOStream::Apply(void* manipulator) { return *static_cast<GameOStream*>(OstreamManipulate(this, nullptr, manipulator)); }

__declspec(naked) CommandHandler* CommandMap::Index(const mfc::CString&) EI_ORIGINAL(0x005E0B20)

namespace {

// Game CRT.

int Mbscmp(const char* a, const char* b) {
    return gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(a), reinterpret_cast<const unsigned char*>(b));
}

// The old iostream streambuf base.
struct StreambufBaseAt {
    void Init(char* buffer, int length);   // 006FCAD2 streambuf(char*, int)
    void Destruct();                       // 006FCB35 ~streambuf
};
__declspec(naked) void StreambufBaseAt::Init(char*, int) EI_ORIGINAL(0x006FCAD2)
__declspec(naked) void StreambufBaseAt::Destruct() EI_ORIGINAL(0x006FCB35)

// 007C7D50: the interface manager; 005F7EB0 hands a line to its console window (+0x14), if any.
struct InterfaceAt {
    void AddConsoleLine(const char* text);   // 005F7EB0
};
__declspec(naked) void InterfaceAt::AddConsoleLine(const char*) EI_ORIGINAL(0x005F7EB0)
inline InterfaceAt* Interface() { return reinterpret_cast<InterfaceAt*>(0x007C7D50); }

// 0057F440 (called with the game at 007AFE20 in ECX): a network game is running (server object
// 0079B5D8 present and active).
bool NetworkGame() { return IsNetworkClientOriginal(); }

// 00455F20: posts WM_QUIT (when the game window exists); the exit code in ECX.

// 005785D0 (on 007AFEA6): loads `text` into the mission script 007C29CC, runs one tick when it
// loaded and `run` is set, and copies the loader's error into `error`. Returns nonzero on success.
struct MissionAt {
    int LoadScript(mfc::CString* text, int run, ScriptLoadError* error);
};
__declspec(naked) int MissionAt::LoadScript(mfc::CString*, int, ScriptLoadError*) EI_ORIGINAL(0x005785D0)

// Calls between the console functions go through the original addresses, so that each runs
// through its own verify wrapper in verify builds (and through any other mod's patch).
__declspec(naked) int __fastcall TokenizeAt(const char*, CStringArray*) EI_ORIGINAL(0x004DC200)
__declspec(naked) void __fastcall RunScriptLineAt(const mfc::CString*, const CStringArray*, const char*,
                                                  const char*) EI_ORIGINAL(0x005DD800)

void* const kFlush = reinterpret_cast<void*>(0x00435000);   // ostream& flush(ostream&)

} // namespace

void EndLine(GameOStream& os) { (os << '\n').Apply(kFlush); }

void PrintLine(const char* text) { EndLine(Out() << text); }

// ---- Console streambuf --------------------------------------------------------------------------

// 005DC200
ConsoleStreambuf* ConsoleStreambuf::Construct(unsigned filter) {
    reinterpret_cast<StreambufBaseAt*>(this)->Init(buffer_, sizeof(buffer_));
    filter_ = filter;
    line_ = *reinterpret_cast<char**>(0x00796E48);   // CString(): the shared empty string
    vtable_ = reinterpret_cast<void**>(kConsoleStreambufVtable);
    return this;
}
EI_REPLACE_METHOD(0x005DC200, ConsoleStreambuf::Construct);

// 005DC260
void ConsoleStreambuf::Destruct() {
    vtable_ = reinterpret_cast<void**>(kConsoleStreambufVtable);
    Line().~CString();
    reinterpret_cast<StreambufBaseAt*>(this)->Destruct();
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
        for (const char* p = pbase_; p < end; ++p) Ops(Line()).AppendChar(*p);
        if (c != -1) Ops(Line()).AppendChar(static_cast<char>(c));
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
    reinterpret_cast<OverflowFn>(vtable_[7])(this, -1);
    Interface()->AddConsoleLine(line_);
    Line() = game::EmptyText();   // ""
    return 0;
}
EI_REPLACE_METHOD(0x005DC1B0, ConsoleStreambuf::Sync);

// ---- Executor -----------------------------------------------------------------------------------

const CommandRegistration kCommandRegistrations[27] = {
    {true, 0x007924C4, 0x005DF5E0},    // help
    {true, 0x007924B8, 0x005DD920},    // debuginfo
    {false, 0x007924B0, 0x005DDBE0},   // console
    {true, 0x007924A8, 0x005DDF80},    // history
    {true, 0x007924A0, 0x005DDFE0},    // filter
    {true, 0x00792498, 0x005DE460},    // show
    {true, 0x00792490, 0x005DEA00},    // listvar
    {true, 0x00792488, 0x005DEC90},    // loadvar
    {false, 0x00792480, 0x005DEEE0},   // lastfps
    {false, 0x00792478, 0x005DEF50},   // exec
    {false, 0x00792470, 0x005DEF50},   // execute
    {true, 0x00792468, 0x005DF0F0},    // give
    {true, 0x00792460, 0x005DF8E0},    // fadeout
    {true, 0x00792458, 0x005DF3A0},    // join
    {true, 0x00792450, 0x005DF490},    // server
    {true, 0x00792444, 0x005DF5A0},    // memusage
    {true, 0x0079243C, 0x005DF5B0},    // lootall
    {true, 0x00792438, 0x005DF9B0},    // net
    {true, 0x0079242C, 0x005DFC10},    // disconnect
    {true, 0x0079241C, 0x005DFDA0},    // generateacks
    {true, 0x00792414, 0x005E00D0},    // days
    {true, 0x00792408, 0x005E0570},    // partyresend
    {false, 0x00792400, 0x005E0590},   // kick
    {false, 0x007923FC, 0x005E06E0},   // ban
    {false, 0x007923F8, 0x005E0830},   // fps
    {false, 0x007923F0, 0x005E0990},   // rate
    {false, 0x007923E4, 0x005E0A10},   // localrate
};

// 005DC620. Every call (re)registers all commands. The first token is the command; "@", "#" and
// "$" lines (cheat word given, not in a network game) become WorldScript lines. Other commands and
// their arguments are lower-cased; "quit"/"exit" quit; otherwise the public map is tried, then the
// cheat word, then (after it) the cheat map. Looking a command up inserts it (original behaviour).
void __fastcall Execute(const char* text) {
    for (const CommandRegistration& r : kCommandRegistrations) {
        mfc::CString name(GameText(r.name));
        *(r.cheat ? CheatCommands() : PublicCommands())->Index(name) = reinterpret_cast<CommandHandler>(r.handler);
    }

    mfc::CString command;
    CStringArray args;
    TokenizeAt(text, &args);
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
                RunScriptLineAt(&script, &args, GameText(0x00790940), GameText(0x0078D71C));   // "WorldScript(" ")"
            else if (first == '#')   // "WorldScript( ConsoleFloat( " " ))"
                RunScriptLineAt(&script, &args, GameText(0x007923C4), GameText(0x007923E0));
            else                     // "WorldScript( ConsoleString( " " ))"
                RunScriptLineAt(&script, &args, GameText(0x007923A4), GameText(0x007923E0));
        }
        return;
    }

    Ops(command).MakeUpper();
    for (int i = 0; i < args.GetSize(); ++i) Ops(args[i]).MakeUpper();
    if (Mbscmp(command, GameText(0x0079239C)) == 0 || Mbscmp(command, GameText(0x00792394)) == 0) {   // quit, exit
        QuitGame(0);
        return;
    }
    if (*PublicCommands()->Index(command)) {
        (*PublicCommands()->Index(command))(&args);
        return;
    }
    if (!CheatEnabled()) {
        char* p = Ops(command).GetBuffer(12);
        for (; *p; ++p) *p ^= 0x66;
        Ops(command).ReleaseBuffer(-1);
        if (std::strcmp(command, GameText(kCheatWordXor66)) == 0) {
            CheatEnabled() = true;
            PrintLine(GameText(0x00792388));   // "Activated!"
            return;
        }
    } else if (*CheatCommands()->Index(command)) {
        if (!NetworkGame()) (*CheatCommands()->Index(command))(&args);
        return;
    }
    PrintLine(GameText(0x0078D848));   // "Unknown command"
}
EI_REPLACE(0x005DC620, Execute);
EI_REBIRTH_IMPLEMENTS(0x005DC620, ei::rebirth::Patch::AddNewConsoleCommands);

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
        if (IsMbbLead(static_cast<unsigned char>(*p))) {
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
        if (IsLeadByte(static_cast<signed char>(*p))) {
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
EI_REPLACE(0x004DC200, Tokenize);

// 005DD800. The arguments are joined back with single spaces.
void __fastcall RunScriptLine(const mfc::CString* text, const CStringArray* args, const char* prefix,
                              const char* suffix) {
    ScriptLoadError error;
    mfc::CString line(prefix);
    line += *text;
    for (int i = 0; i < args->GetSize(); ++i) {
        Ops(line).AppendText(GameText(0x0078DA40));   // " "
        mfc::CString arg((*args)[i]);
        line += arg;
    }
    Ops(line).AppendText(suffix);
    if (!reinterpret_cast<MissionAt*>(0x007AFEA6)->LoadScript(&line, 1, &error))
        EndLine(Out() << GameText(0x007924CC) << static_cast<const char*>(error.text));   // "Script error: "
}
EI_REPLACE(0x005DD800, RunScriptLine);

} // namespace console
