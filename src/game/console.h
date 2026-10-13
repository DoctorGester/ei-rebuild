#pragma once

// The developer console (game.exe 0x005DC000-0x005E0B20): the streambuf that collects console
// output, the command executor 005DC620 with its two command maps, the "@"/"#"/"$" script lines
// and the command handlers. See docs/notes/console.md.

#include "../lib/mfc.h"

#include <cstddef>
#include <cstdint>

namespace ui_gdi { struct GameFont; }

namespace console {

// A string constant of game.exe, passed by its original address (Rebirth may patch the data).
inline const char* GameText(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }

// CString length (stored before the characters).
inline int Length(const mfc::CString& s) { return reinterpret_cast<const int*>(static_cast<const char*>(s))[-2]; }

// CString members beyond lib/mfc.h, called on existing strings through this view (no extra data).
struct Str : mfc::CString {
    mfc::CString Mid(int first) const;              // 006FD9B4 (006FDA6D is Right, mfc::CString::Right)
    mfc::CString Mid(int first, int count) const;   // 006FD9D7
    void AppendChar(char c);                        // 00700D73 operator+=(TCHAR)
    void AppendText(const char* text);              // 00700D4C operator+=(LPCTSTR)
    void AssignChar(char c);                        // 006FD559 operator=(TCHAR)
    char* GetBuffer(int minLength);                 // 00700DA0
    void ReleaseBuffer(int newLength);              // 00700DEF
    void MakeLower();                               // 00700EFE (also mfc::CString::MakeLower;
                                                    // it calls _mbslwr, MFC's MakeLower)
    void TrimLeft(char c);                          // 006FE1D3 TrimLeft(TCHAR)
    void TrimRight(char c);                         // 006FE0DD TrimRight(TCHAR)
};
inline Str& Ops(mfc::CString& s) { return static_cast<Str&>(s); }
inline const Str& Ops(const mfc::CString& s) { return static_cast<const Str&>(s); }

// MFC CStringArray as a local or member: mfc::CStringArray constructed and destroyed with the object.
class CStringArray : public mfc::CStringArray {
public:
    CStringArray() { Construct(); }     // 006FE925
    ~CStringArray() { Destruct(); }     // 006FE958
    CStringArray(const CStringArray&) = delete;
    CStringArray& operator=(const CStringArray&) = delete;
};
static_assert(sizeof(CStringArray) == 0x14);

// MFC CStdioFile (0x14 bytes: CFile and the FILE*). Locals only; raw members for the forwarders.
class CStdioFile {
public:
    enum { modeRead = 0, modeWrite = 1, modeCreate = 0x1000, typeText = 0x4000 };
    CStdioFile();                                                   // 00701B79
    ~CStdioFile();                                                  // 00701C6D
    mfc::BOOL Open(const char* name, unsigned flags, void* error);  // 00701CB0
    mfc::BOOL ReadString(mfc::CString& line);                       // 00701E93
    void WriteString(const char* text);                             // 00701E23
    void Close();                                                   // 00701FE0

private:
    void* vtable_;
    void* handle_;
    mfc::BOOL closeOnDelete_;
    char* fileName_;
    void* stream_;
};
static_assert(sizeof(CStdioFile) == 0x14);

// The game's (pre-standard) ostream. Only the inline float output touches a member: x_floatused.
class GameOStream {
public:
    GameOStream& operator<<(const char* text);   // 006FC31D
    GameOStream& operator<<(char c);             // 006FC0B0
    GameOStream& operator<<(int n);              // 006FC91C
    GameOStream& operator<<(double d);           // 006FC7FA
    // 00434FE0: operator<<(ostream& (*)(ostream&)), here only with flush (00435000).
    GameOStream& Apply(void* manipulator);
    // Apply(flush): the manipulator ostream& flush(ostream&) at 00435000.
    GameOStream& Flush() { return Apply(reinterpret_cast<void*>(0x00435000)); }
    // The header's inline operator<<(float): sets x_floatused, prints as double. The game passes
    // the x87 value as is (never rounded to float).
    GameOStream& PutFloat(double d) {
        floatUsed_ = 1;
        return *this << d;
    }
    // 004BA9D0: the same, out of line, for a float argument (script_runtime_helpers.cpp).
    GameOStream& PutFloatArgument(float f);

private:
    void* vbtable_;
    int floatUsed_;   // +4
};

// 007C55A8: the console's ostream (its streambuf is ConsoleStreambuf 007C5868).
inline GameOStream& Out() { return *reinterpret_cast<GameOStream*>(0x007C55A8); }
// 007C5760: another of the console's ostreams (its message filter not identified); the battle
// screen prints the "Terrain Coords" of a click on the ground to it (00638EF0).
inline GameOStream& Out7C5760() { return *reinterpret_cast<GameOStream*>(0x007C5760); }
// `<< '\n' << flush`, how every console message ends.
void EndLine(GameOStream& os);
// Out() << text, then EndLine.
void PrintLine(const char* text);

// The console's streambuf (vtable 00740034; nine static instances, one per message filter: 0 the
// console itself 007C5868, then ai 1, event 2, graphics 4, rpg 8, unitacks 0x10, net 0x20, all -1).
// Characters collect in the put area (the 0x400-byte buffer at +0x54) and move into the line
// (+0x50) on overflow; sync hands the line to the console window.
class ConsoleStreambuf {
public:
    ConsoleStreambuf* Construct(unsigned filter);         // 005DC200
    void Destruct();                                      // 005DC260
    ConsoleStreambuf* DeletingDestruct(unsigned flags);   // 005DC240 (vtable slot 0)
    int Sync();                                           // 005DC1B0 (slot 1)
    int Overflow(int c);                                  // 005DC150 (slot 7)

    mfc::CString& Line() { return *reinterpret_cast<mfc::CString*>(&line_); }

    void** vtable_;
    int allocated_;            // +0x04 streambuf members (old iostream)
    int unbuffered_;           // +0x08
    int lastChar_;             // +0x0C
    char* base_;               // +0x10
    char* ebuf_;               // +0x14
    char* pbase_;              // +0x18
    char* pptr_;               // +0x1C
    char* epptr_;              // +0x20
    char* eback_;              // +0x24
    char* gptr_;               // +0x28
    char* egptr_;              // +0x2C
    int lockFlag_;             // +0x30
    std::uint8_t lock_[0x18];  // +0x34 CRITICAL_SECTION
    unsigned filter_;          // +0x4C message category; 0 = always shown
    char* line_;               // +0x50 CString
    char buffer_[0x400];       // +0x54
};
static_assert(sizeof(ConsoleStreambuf) == 0x454);
static_assert(offsetof(ConsoleStreambuf, base_) == 0x10 && offsetof(ConsoleStreambuf, filter_) == 0x4C &&
              offsetof(ConsoleStreambuf, line_) == 0x50 && offsetof(ConsoleStreambuf, buffer_) == 0x54);

constexpr std::uintptr_t kConsoleStreambufVtable = 0x00740034;
inline ConsoleStreambuf* MainStreambuf() { return reinterpret_cast<ConsoleStreambuf*>(0x007C5868); }

// 007C7B68: the message categories shown (bit per filter, "filter" command).
inline unsigned& FilterMask() { return *reinterpret_cast<unsigned*>(0x007C7B68); }
// 007C7B64: set by the cheat word; unlocks the second command map and the script lines.
inline bool& CheatEnabled() { return *reinterpret_cast<bool*>(0x007C7B64); }

// The console window (the interface manager 007C7D50 keeps it at +0x14). Lines are kept as text;
// the surface is redrawn by 005E4800.
class ConsoleWindow {
public:
    // 005E3CE0 (RET 4; console_window_create.cpp; CInterface3D::Create makes it). The members
    // constructed, `screen` kept, no surface, no command shown (-1), caret, selection, scroll and
    // the flags cleared; a 9-point "Courier New" font (dropped when it cannot be made); the
    // rectangle (0, 0)-(800, 150) and a 256-line history (older lines dropped); the lines drawn.
    // Returns this.
    ConsoleWindow* Construct(int* screen);
    void SetRect(const int* rect);   // 005E3E90 (800x600 coordinates)
    void SetHistory(int lines);      // 005E3F90
    void AddText(const char* text);  // 005E4090 (continues the last line; '\n' starts a new one)
    // Drawing and input (ui_console_window.cpp):
    void Show(bool shown);                     // 005E3FD0 creates or releases the surface
    void OnKeyDown(int key, int repeat, int flags);   // 005E4180 editing and scrolling keys
    void OnChar(int key, int repeat, int flags);      // 005E44A0 typed characters, Enter, Escape
    void OnKeyUp(int key, int repeat, int flags);     // 005E47E0 VK_SHIFT / VK_CONTROL clear shift_ / ctrl_
    void DrawLines();                          // 005E4800 the lines above the input line
    void DrawInput();                          // 005E4A10 the input line, selection and caret
    // 005E3FC0 (ui_small_members.cpp; the interface's 2D redraw 005F5320 calls it while the
    // console is shown). The surface's slot 0 (its blit) when there is one; its EAX is returned.
    int Blit();
    // 005E3DF0 (ui_interface_shutdown.cpp; the interface's shutdown deletes the window with it).
    // Frees the surface and the font, then the members (lines, input text, command history).
    void DestroyBody();

    int* screen_;                    // +0x00 the 3D interface; [0] width, [1] height in pixels
    void* surface_;                  // +0x04 CUiSurface while shown
    ui_gdi::GameFont* font_;         // +0x08 CFont (selected into the surface's CDC)
    CStringArray commands_;          // +0x0C commands entered (input history)
    int commandIndex_;               // +0x20 the one shown by up/down (-1 or size: none)
    mfc::CString input_;             // +0x24 the line being typed
    int caret_;                      // +0x28 caret position (characters)
    int anchor_;                     // +0x2C other end of the selection (== caret_: none)
    int width_;                      // +0x30 pixels, at least 76
    int height_;                     // +0x34 pixels, at least 69
    int rect_[4];                    // +0x38 the window in 800x600 coordinates (after clamping)
    int screenRect_[4];              // +0x48 the window in pixels
    CStringArray lines_;             // +0x58
    int history_;                    // +0x6C lines kept
    int visibleLines_;               // +0x70
    int scroll_;                     // +0x74 lines scrolled back (0 = at the end)
    bool ctrl_;                      // +0x78 Ctrl held (set by key down 0x11)
    bool shift_;                     // +0x79 Shift held (0x10): the selection grows
    bool shown_;                     // +0x7A
    std::uint8_t unknown_7B;
};
static_assert(offsetof(ConsoleWindow, lines_) == 0x58 && offsetof(ConsoleWindow, shown_) == 0x7A);
static_assert(offsetof(ConsoleWindow, commands_) == 0x0C && offsetof(ConsoleWindow, input_) == 0x24 &&
              offsetof(ConsoleWindow, width_) == 0x30 && offsetof(ConsoleWindow, ctrl_) == 0x78);

inline ConsoleWindow* TheConsoleWindow() { return *reinterpret_cast<ConsoleWindow**>(0x007C7D50 + 0x14); }

// Console commands: void handler(CStringArray* args) with the arguments (lower case) in ECX.
using CommandHandler = void(__fastcall*)(CStringArray* args);

// STLport map<CString, CommandHandler> (red-black tree). The header node's parent is the root.
struct CommandNode {
    int color;
    CommandNode* parent;
    CommandNode* left;
    CommandNode* right;
    char* key;                 // CString
    CommandHandler handler;
};
static_assert(sizeof(CommandNode) == 0x18);

struct CommandMap {
    CommandNode* header;
    unsigned count;
    // 005E0B20: operator[]: the handler slot for `key`, inserted with a null handler if missing.
    CommandHandler* Index(const mfc::CString& key);
};

// 007C7B28: commands always available; 007C7B38: commands after the cheat word.
inline CommandMap* PublicCommands() { return reinterpret_cast<CommandMap*>(0x007C7B28); }
inline CommandMap* CheatCommands() { return reinterpret_cast<CommandMap*>(0x007C7B38); }

// The registrations the executor makes on every call, in its order: map, name, handler address.
struct CommandRegistration {
    bool cheat;
    std::uintptr_t name;
    std::uintptr_t handler;
};
extern const CommandRegistration kCommandRegistrations[27];
// 00740028: the cheat word, each character XOR 0x66.
constexpr std::uintptr_t kCheatWordXor66 = 0x00740028;

// 005DC620: runs one console line (command text in ECX).
void __fastcall Execute(const char* text);
// Rebirth's console additions (console_addon.cpp). AddNewConsoleCommands: the executor registers
// addon's commands on every call; the battle screen's drawing applies "nogui" to its panels.
// AutoExec: the "Console AutoExec" lines, run before three of the game's console executions.
void RegisterAddonCommands();
// What RegisterAddonCommands puts in the maps, in its order ("help" replaces the game's, which
// addon's own "help" calls first).
struct AddonCommandRegistration {
    bool cheat;
    const char* name;
    CommandHandler handler;
};
extern const AddonCommandRegistration kAddonCommandRegistrations[6];
void ApplyAddonGuiVisibility(void* screen);
void RunAddonAutoExec();
// 004DC200: splits a console line at spaces into `out` (a token in double quotes keeps its spaces
// and quotes); returns 1.
int __fastcall Tokenize(const char* text, CStringArray* out);
// 005DD800: loads prefix + text + " arg"... + suffix into the mission script and runs it; prints
// "Script error: ..." when it does not load.
void __fastcall RunScriptLine(const mfc::CString* text, const CStringArray* args, const char* prefix,
                              const char* suffix);

// The error the mission script loader reports (text, code, line).
struct ScriptLoadError {
    mfc::CString text;
    int code = 0;
    int line = 0;
};

// Command handlers (console_commands.cpp).
namespace commands {
void __fastcall Help(CStringArray* args);          // 005DF5E0 help
void __fastcall DebugInfo(CStringArray* args);     // 005DD920 debuginfo
void __fastcall ConsolePos(CStringArray* args);    // 005DDBE0 console
void __fastcall History(CStringArray* args);       // 005DDF80 history
void __fastcall Filter(CStringArray* args);        // 005DDFE0 filter
void __fastcall Show(CStringArray* args);          // 005DE460 show
void __fastcall ListVar(CStringArray* args);       // 005DEA00 listvar
void __fastcall LoadVar(CStringArray* args);       // 005DEC90 loadvar
void __fastcall LastFps(CStringArray* args);       // 005DEEE0 lastfps
void __fastcall Exec(CStringArray* args);          // 005DEF50 exec, execute
void __fastcall Give(CStringArray* args);          // 005DF0F0 give
void __fastcall LootAll(CStringArray* args);       // 005DF5B0 lootall
void __fastcall FadeOut(CStringArray* args);       // 005DF8E0 fadeout
void __fastcall Net(CStringArray* args);           // 005DF9B0 net
void __fastcall Days(CStringArray* args);          // 005E00D0 days
void __fastcall PartyResend(CStringArray* args);   // 005E0570 partyresend
void __fastcall Kick(CStringArray* args);          // 005E0590 kick
void __fastcall Ban(CStringArray* args);           // 005E06E0 ban
void __fastcall Fps(CStringArray* args);           // 005E0830 fps
void __fastcall Rate(CStringArray* args);          // 005E0990 rate
void __fastcall LocalRate(CStringArray* args);     // 005E0A10 localrate
} // namespace commands

} // namespace console
