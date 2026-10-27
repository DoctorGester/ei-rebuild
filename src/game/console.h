#pragma once

// The developer console: the streambuf that collects console
// output, the command executor with its two command maps, the "@"/"#"/"$" script lines
// and the command handlers. See docs/notes/console.md.

#include "game_data.h"
#include "game_strings.h"
#include "../lib/mfc.h"
#include "../lib/crt_iostream.h"
#include "../lib/stlp.h"
#include "../lib/stlp_tree_cstring.h"
#include "../lib/static_object.h"

#include <cstddef>
#include <cstdint>

namespace libcrt { class Streambuf; }   // lib/crt_iostream.h
namespace mfc::impl { struct CStdioFile; }   // lib/mfc_file.h
namespace mfc::impl { class CFont; }   // lib/mfc_gdi.h
struct SpeechRecord;            // unit_client_models.h
class CInterface3D;             // ui.h
class CUiContainer;             // ui.h
class CUiSurface;               // ui.h
namespace acks { struct Speeches; struct EnvironmentSpeeches; }   // acks_db.h

namespace console {

// CString members beyond lib/mfc.h, called on existing strings through this view (no extra data).
struct Str : mfc::CString {
    void AppendChar(char c);                        // operator+=(TCHAR)
    void AppendText(const char* text);              // operator+=(LPCTSTR)
    void TrimLeft(char c);
    void TrimRight(char c);
};
inline Str& Ops(mfc::CString& s) { return static_cast<Str&>(s); }
inline const Str& Ops(const mfc::CString& s) { return static_cast<const Str&>(s); }

// MFC CStringArray as a local or member: mfc::CStringArray constructed and destroyed with the object.
class CStringArray : public mfc::CStringArray {
public:
    CStringArray() { Construct(); }
    ~CStringArray() { Destruct(); }
    CStringArray(const CStringArray&) = delete;
    CStringArray& operator=(const CStringArray&) = delete;
};
static_assert(sizeof(CStringArray) == 0x14);

// The virtual slots of a CStdioFile for code that writes through the file's
// vtable (generateacks' line writers). Declared only.
class __declspec(novtable) CStdioFileVirtuals : public mfc::CFileVirtuals {
public:
    virtual void WriteString(const char* text);
};

// MFC CStdioFile (0x14 bytes: CFile and the FILE*). Locals only.
class CStdioFile {
public:
    enum { modeRead = 0, modeWrite = 1, modeCreate = 0x1000, typeText = 0x4000 };
    CStdioFile();
    // Opened at once; a failure throws a CFileException* (lib/mfc_try.h).
    CStdioFile(const char* name, unsigned flags);
    ~CStdioFile();
    mfc::BOOL Open(const char* name, unsigned flags, void* error);
    mfc::BOOL ReadString(mfc::CString& line);
    // A line with its line end into `buffer` (at most size - 1 characters); null at the end
    // of the file. A read error throws a CFileException*.
    char* ReadString(char* buffer, unsigned size);
    void WriteString(const char* text);
    void Close();
    // The file as code that calls through its vtable sees it.
    CStdioFileVirtuals* Virtuals() { return reinterpret_cast<CStdioFileVirtuals*>(this); }

private:
    mfc::impl::CStdioFile* Impl();

    void* vtable_;
    void* handle_;
    mfc::BOOL closeOnDelete_;
    char* fileName_;
    void* stream_;
};
static_assert(sizeof(CStdioFile) == 0x14);

// The game's (pre-standard) ostream. Only the inline float output touches a member: x_floatused.
class GameOStream : public libcrt::Ostream {
public:
    GameOStream& operator<<(const char* text);
    GameOStream& operator<<(char c);
    GameOStream& operator<<(int n);
    GameOStream& operator<<(double d);
    // Operator<<(ostream& (*)(ostream&)), here only with flush.
    GameOStream& Apply(libcrt::Manipulator manipulator);
    // Apply(flush): the manipulator ostream& flush(ostream&).
    GameOStream& Flush() { return Apply(libcrt::FlushManipulator); }
    // The header's inline operator<<(float): sets x_floatused, prints as double. The game passes
    // the x87 value as is (never rounded to float).
    GameOStream& PutFloat(double d) {
        floatUsed = 1;
        return *this << d;
    }
    // The same, out of line, for a float argument (script_runtime_helpers.cpp).
    GameOStream& PutFloatArgument(float f);
};
static_assert(sizeof(GameOStream) == 8);

// One of the console's eight static ostreams as the original lays it out (0x58 bytes): the ostream part and its
// ios, the virtual base, right after it. Zero until its static initialiser constructs it on its streambuf
// (ostream(streambuf*, 1)); the exit function that initialiser registers destroys it (console_streams.cpp).
struct OStreamObject : GameOStream {
    libcrt::Ios ios;
};
static_assert(sizeof(OStreamObject) == 0x58);

// The ostreams, one per message filter (their streambufs below): the console itself, then all, unitacks (the AI
// alarm's log), net (the network log), rpg (the hit log), graphics (the battle screen's "Terrain Coords" of a click
// on the ground), event, ai (combat's and the AI's logs).
extern OStreamObject g_consoleOut;
extern OStreamObject g_allLogOut;
extern OStreamObject g_unitAcksLogOut;
extern OStreamObject g_netLogOut;
extern OStreamObject g_rpgLogOut;
extern OStreamObject g_graphicsLogOut;
extern OStreamObject g_eventLogOut;
extern OStreamObject g_aiLogOut;
// The console's ostream.
inline GameOStream& Out() { return g_consoleOut; }
// A log stream seen through one of the game's per-file ostream views (inserters only, no data of their own).
template <class View>
View* StreamAs(libcrt::Ostream& stream) {
    static_assert(sizeof(View) == sizeof(libcrt::Ostream));
    return static_cast<View*>(&stream);
}
// `<< '\n' << flush`, how every console message ends.
void EndLine(GameOStream& os);
// Out() << text, then EndLine.
void PrintLine(const char* text);

// The console's streambuf (eight static instances, one per message filter: 0 the console
// itself, then ai 1, event 2, graphics 4, rpg 8, unitacks 0x10, net 0x20, all -1). Characters collect in the
// put area (the 0x400-byte buffer at +0x54) and move into the line (+0x50) on overflow; sync hands the line to the
// console window.
class ConsoleStreambuf final : public libcrt::Streambuf {
public:
    // The base on buffer_, the filter, the empty line.
    explicit ConsoleStreambuf(unsigned filter);
    // The line released (a member), then the base's.
    ~ConsoleStreambuf() override;
    int Sync() override;
    int Overflow(int c) override;
    int Underflow() override;            // EOF

    unsigned filter_;          // +0x4C message category; 0 = always shown
    mfc::CString line_;        // +0x50
    char buffer_[0x400];       // +0x54
};
static_assert(sizeof(ConsoleStreambuf) == 0x454);

// The harness's view of the console (verify builds): sync's line, after overflow(EOF) and before the console window
// gets it. In the original-game run the harness takes the original's sync instead.
extern void (*g_consoleLineObserver)(const char* line);

// The streambufs, one per message filter (console_streams.cpp; zero until their static initialisers construct them).
extern lib::StaticObject<ConsoleStreambuf> g_consoleBuf;       // filter 0
extern lib::StaticObject<ConsoleStreambuf> g_allLogBuf;        // -1
extern lib::StaticObject<ConsoleStreambuf> g_netLogBuf;        // 0x20
extern lib::StaticObject<ConsoleStreambuf> g_unitAcksLogBuf;   // 0x10
extern lib::StaticObject<ConsoleStreambuf> g_rpgLogBuf;        // 8
extern lib::StaticObject<ConsoleStreambuf> g_graphicsLogBuf;   // 4
extern lib::StaticObject<ConsoleStreambuf> g_eventLogBuf;      // 2
extern lib::StaticObject<ConsoleStreambuf> g_aiLogBuf;         // 1

// The message categories shown (bit per filter, "filter" command).
inline unsigned g_filterMask = /*007C7B68*/ {};
// Set by the cheat word; unlocks the second command map and the script lines.
inline bool g_cheatEnabled = /*007C7B64*/ {};

// The console window (the interface manager keeps it). Lines are kept as text;
// the surface is redrawn from them.
class ConsoleWindow {
public:
    // Made by CInterface3D::Create (console_window_create.cpp). The members
    // constructed, `screen` kept, no surface, no command shown (-1), caret, selection, scroll and
    // the flags cleared; a 9-point "Courier New" font (dropped when it cannot be made); the
    // rectangle (0, 0)-(800, 150) and a 256-line history (older lines dropped); the lines drawn.
    // Returns this.
    ConsoleWindow* Construct(CInterface3D* screen);
    void SetRect(const RECT* rect);   // 800x600 coordinates
    void SetHistory(int lines);
    void AddText(const char* text);  // continues the last line; '\n' starts a new one
    // Drawing and input (ui_console_window.cpp):
    void Show(bool shown);                     // creates or releases the surface
    void OnKeyDown(int key, int repeat, int flags);   // editing and scrolling keys
    void OnChar(int key, int repeat, int flags);      // typed characters, Enter, Escape
    void OnKeyUp(int key, int repeat, int flags);     // VK_SHIFT / VK_CONTROL clear shift_ / ctrl_
    void DrawLines();                          // the lines above the input line
    void DrawInput();                          // the input line, selection and caret
    // Called by the interface's 2D redraw while the
    // console is shown. The surface's blit when there is one.
    void Blit();
    // Called by the interface's shutdown, which deletes the window.
    // Frees the surface and the font, then the members (lines, input text, command history).
    void DestroyBody();

    CInterface3D* screen_;           // +0x00 the 3D interface (its width_ and height_ in pixels)
    CUiSurface* surface_;            // +0x04 while shown
    mfc::impl::CFont* font_;         // +0x08 CFont (selected into the surface's CDC)
    CStringArray commands_;          // +0x0C commands entered (input history)
    int commandIndex_;               // +0x20 the one shown by up/down (-1 or size: none)
    mfc::CString input_;             // +0x24 the line being typed
    int caret_;                      // +0x28 caret position (bytes, at a character's start: U4)
    int anchor_;                     // +0x2C other end of the selection (== caret_: none)
    int width_;                      // +0x30 pixels, at least 76
    int height_;                     // +0x34 pixels, at least 69
    RECT rect_;                      // +0x38 the window in 800x600 coordinates (after clamping)
    RECT screenRect_;                // +0x48 the window in pixels
    CStringArray lines_;             // +0x58
    int history_;                    // +0x6C lines kept
    int visibleLines_;               // +0x70
    int scroll_;                     // +0x74 lines scrolled back (0 = at the end)
    bool ctrl_;                      // +0x78 Ctrl held (set by key down 0x11)
    bool shift_;                     // +0x79 Shift held (0x10): the selection grows
    bool shown_;                     // +0x7A
    std::uint8_t unused_7B;
};
static_assert(offsetof(ConsoleWindow, lines_) == 0x58 && offsetof(ConsoleWindow, shown_) == 0x7A);
static_assert(offsetof(ConsoleWindow, commands_) == 0x0C && offsetof(ConsoleWindow, input_) == 0x24 &&
              offsetof(ConsoleWindow, width_) == 0x30 && offsetof(ConsoleWindow, ctrl_) == 0x78);

// Console commands: void handler(CStringArray* args) with the arguments (lower case) in ECX.
using CommandHandler = void(__fastcall*)(CStringArray* args);

// STLport map<CString, CommandHandler> (red-black tree, keys by _mbscmp). The header node's parent
// is the root. Its members are registered by library_trees.cpp.
using CommandMap = stlp::map<mfc::CString, CommandHandler, stlp::CStringLess>;
using CommandNode = CommandMap::Node;
static_assert(sizeof(CommandNode) == 0x18);

// Operator[] (lib_trees::Commands'): the handler slot for `key`, inserted with a null handler
// if missing.
CommandHandler& Command(CommandMap* map, const mfc::CString& key);

static_assert(sizeof(CommandMap) == 0xC);
// The commands always available and those after the cheat word (console_streams.cpp; built empty by their static
// initialisers, destroyed at exit).
extern lib::StaticObject<CommandMap> g_publicCommands;
extern lib::StaticObject<CommandMap> g_cheatCommands;
inline CommandMap* PublicCommands() { return &g_publicCommands.Get(); }
inline CommandMap* CheatCommands() { return &g_cheatCommands.Get(); }

// The registrations the executor makes on every call, in its order: map, name, handler.
struct CommandRegistration {
    bool cheat;
    const char* name;
    CommandHandler handler;
};
extern const CommandRegistration kCommandRegistrations[27];

// Runs one console line (command text in ECX).
void __fastcall Execute(const char* text);
// Rebirth's console additions (console_addon.cpp). AddNewConsoleCommands: the executor registers
// addon's commands on every call; the battle screen's drawing applies "nogui" to its panels.
// AutoExec: the "Console AutoExec" lines, run before three of the game's console executions.
void RegisterAddonCommands();
// What RegisterAddonCommands puts in the maps, in its order ("help" replaces the game's, which
// addon's own "help" calls first).
extern const CommandRegistration kAddonCommandRegistrations[6];
void ApplyAddonGuiVisibility(CUiContainer* screen);
void RunAddonAutoExec();
// Splits a console line at spaces into `out` (a token in double quotes keeps its spaces
// and quotes); returns 1.
int __fastcall Tokenize(const char* text, CStringArray* out);
// Loads prefix + text + " arg"... + suffix into the mission script and runs it; prints
// "Script error: ..." when it does not load.
void __fastcall RunScriptLine(const mfc::CString* text, const CStringArray* args, const char* prefix,
                              const char* suffix);

// Command handlers (console_commands.cpp).
namespace commands {
void __fastcall Help(CStringArray* args);          // help
void __fastcall DebugInfo(CStringArray* args);     // debuginfo
void __fastcall ConsolePos(CStringArray* args);    // console
void __fastcall History(CStringArray* args);       // history
void __fastcall Filter(CStringArray* args);        // filter
void __fastcall Show(CStringArray* args);          // show
void __fastcall ListVar(CStringArray* args);       // listvar
void __fastcall LoadVar(CStringArray* args);       // loadvar
void __fastcall LastFps(CStringArray* args);       // lastfps
void __fastcall Exec(CStringArray* args);          // exec, execute
void __fastcall Give(CStringArray* args);          // give
void __fastcall LootAll(CStringArray* args);       // lootall
void __fastcall Join(CStringArray* args);          // join
void __fastcall HostServer(CStringArray* args);    // server
void __fastcall MemUsage(CStringArray* args);      // memusage
void __fastcall Disconnect(CStringArray* args);    // disconnect
void __fastcall FadeOut(CStringArray* args);       // fadeout
void __fastcall Net(CStringArray* args);           // net
void __fastcall GenerateAcks(CStringArray* args);  // generateacks
void __fastcall Days(CStringArray* args);          // days
void __fastcall PartyResend(CStringArray* args);   // partyresend
void __fastcall Kick(CStringArray* args);          // kick
void __fastcall Ban(CStringArray* args);           // ban
void __fastcall Fps(CStringArray* args);           // fps
void __fastcall Rate(CStringArray* args);          // rate
void __fastcall LocalRate(CStringArray* args);     // localrate
// generateacks' (ECX the file, EDX the list; a null list writes nothing):
// lines for a speech list / an environment speech list. See console_commands.cpp.
void __fastcall WriteSpeechLines(CStdioFileVirtuals* file, const acks::Speeches* list);
void __fastcall WriteEnvironmentSpeechLines(CStdioFileVirtuals* file, const acks::EnvironmentSpeeches* list);
} // namespace commands

} // namespace console
