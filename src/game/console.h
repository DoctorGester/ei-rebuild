#pragma once

// The developer console: the streambuf that collects console output, the command executor with its two
// command maps, the "@"/"#"/"$" script lines and the command handlers. See docs/notes/console.md.

#include "game_data.h"
#include "game_strings.h"
#include "../lib/mfc.h"
#include "../lib/mfc_file.h"
#include "../lib/crt_iostream.h"
#include "../lib/stlp.h"
#include "../lib/str.h"
#include "../lib/static_object.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace libcrt { class Streambuf; }
namespace mfc::impl { class CFont; }
class CInterface3D;
class CUiContainer;
class CUiSurface;
namespace acks { struct Speeches; struct EnvironmentSpeeches; }

namespace console {

// MFC CStdioFile as a local: opened and closed like the C++ class.
class CStdioFile {
public:
    enum { modeRead = 0, modeWrite = 1, modeCreate = 0x1000, typeText = 0x4000 };
    CStdioFile();
    CStdioFile(const char* name, unsigned flags);
    ~CStdioFile();
    CStdioFile(const CStdioFile&) = delete;
    CStdioFile& operator=(const CStdioFile&) = delete;
    mfc::BOOL Open(std::string_view name, unsigned flags, mfc::impl::CFileException* error);
    // The next line into `line`; false at the end of the file.
    mfc::BOOL ReadString(std::string& line);
    // A line with its line end into `buffer` (at most size - 1 characters); null at the end
    // of the file. A read error throws a CFileException*.
    char* ReadString(char* buffer, unsigned size);
    void WriteString(const char* text);
    void Close();

private:
    mfc::impl::CStdioFile file_;
};

// The game's (pre-standard) ostream. Only the inline float output touches a member: x_floatused.
class GameOStream : public libcrt::Ostream {
public:
    GameOStream& operator<<(const char* text);
    GameOStream& operator<<(char c);
    GameOStream& operator<<(int n);
    GameOStream& operator<<(double d);
    GameOStream& Apply(libcrt::Manipulator manipulator);
    GameOStream& Flush() { return Apply(libcrt::FlushManipulator); }
    // The header's inline operator<<(float): sets x_floatused, prints as double. The game passes
    // the x87 value as is (never rounded to float).
    GameOStream& PutFloat(double d) {
        floatUsed = 1;
        return *this << d;
    }
    GameOStream& PutFloatArgument(float f);
};
static_assert(sizeof(GameOStream) == 8);

// One of the console's eight static ostreams: the ostream part and its ios (the virtual base) right after it.
// Zero until its static initialiser constructs it on its streambuf; the initialiser's exit function destroys it.
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
inline GameOStream& Out() { return g_consoleOut; }
// A log stream seen through one of the game's per-file ostream views (inserters only, no data of their own).
template <class View>
View* StreamAs(libcrt::Ostream& stream) {
    static_assert(sizeof(View) == sizeof(libcrt::Ostream));
    return static_cast<View*>(&stream);
}
// `<< '\n' << flush`, how every console message ends.
void EndLine(GameOStream& os);
void PrintLine(const char* text);

// The console's streambuf (eight static instances, one per message filter: 0 the console itself, then
// ai 1, event 2, graphics 4, rpg 8, unitacks 0x10, net 0x20, all -1). Characters collect in the put
// area (buffer_) and move into line_ on overflow; sync hands the line to the console window.
class ConsoleStreambuf final : public libcrt::Streambuf {
public:
    explicit ConsoleStreambuf(unsigned filter);
    ~ConsoleStreambuf() override;
    int Sync() override;
    int Overflow(int c) override;
    int Underflow() override;            // EOF

    unsigned filter_;          // message category; 0 = always shown
    std::string line_;
    char buffer_[0x400];
};

// The harness's view of the console (verify builds): sync's line, after overflow(EOF) and before the
// console window gets it. In the original-game run the harness hooks the original's sync instead.
extern void (*g_consoleLineObserver)(const char* line);

// The streambufs, one per message filter (zero until their static initialisers construct them).
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
    // Made by CInterface3D::Create: a 9-point "Courier New" font, the rectangle (0, 0)-(800, 150), a 256-line history.
    ConsoleWindow* Construct(CInterface3D* screen);
    void SetRect(const RECT* rect);   // 800x600 coordinates
    void SetHistory(int lines);
    void AddText(const char* text);  // continues the last line; '\n' starts a new one
    // Drawing and input:
    void Show(bool shown);                     // creates or releases the surface
    void OnKeyDown(int key, int repeat, int flags);   // editing and scrolling keys
    void OnChar(int key, int repeat, int flags);      // typed characters, Enter, Escape
    void OnKeyUp(int key, int repeat, int flags);     // VK_SHIFT / VK_CONTROL clear shift_ / ctrl_
    void DrawLines();                          // the lines above the input line
    void DrawInput();                          // the input line, selection and caret
    // Called by the interface's 2D redraw while the console is shown. The surface's blit when there is one.
    void Blit();
    // Called by the interface's shutdown, which deletes the window.
    void DestroyBody();
    // Keeps the last `count` lines.
    void DropOldLines(int count);

    CInterface3D* screen_;           // the 3D interface (its width_ and height_ in pixels)
    CUiSurface* surface_;            // while shown
    mfc::impl::CFont* font_;         // selected into the surface's CDC
    std::vector<std::string> commands_;   // commands entered (input history), the newest first
    int commandIndex_;               // the one shown by up/down (-1 or size: none)
    std::string input_;              // the line being typed
    int caret_;                      // caret position (bytes, at a character's start: U4)
    int anchor_;                     // other end of the selection (== caret_: none)
    int width_;                      // pixels, at least 76
    int height_;                     // pixels, at least 69
    RECT rect_;                      // the window in 800x600 coordinates (after clamping)
    RECT screenRect_;                // the window in pixels
    std::vector<std::string> lines_;
    int history_;                    // lines kept
    int visibleLines_;
    int scroll_;                     // lines scrolled back (0 = at the end)
    bool ctrl_;                      // Ctrl held (set by key down 0x11)
    bool shift_;                     // Shift held (0x10): the selection grows
    bool shown_;
    std::uint8_t unused_7B;
};
// A console command's arguments (lower case).
using Arguments = std::vector<std::string>;
using CommandHandler = void(*)(Arguments* args);

// Keys compared by _mbscmp.
using CommandMap = std::map<std::string, CommandHandler, str::MbsLess>;

// The handler slot for `key`, inserted with a null handler if missing.
CommandHandler& Command(CommandMap* map, const std::string& key);

// The commands always available and those after the cheat word.
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

void Execute(const char* text);
// Rebirth's console additions. AddNewConsoleCommands: the executor registers
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
int Tokenize(const char* text, std::vector<std::string>* out);
// Loads prefix + text + " arg"... + suffix into the mission script and runs it; prints
// "Script error: ..." when it does not load.
void RunScriptLine(const std::string& text, const Arguments& args, const char* prefix, const char* suffix);

namespace commands {
void Help(Arguments* args);
void DebugInfo(Arguments* args);
void ConsolePos(Arguments* args);    // console
void History(Arguments* args);
void Filter(Arguments* args);
void Show(Arguments* args);
void ListVar(Arguments* args);
void LoadVar(Arguments* args);
void LastFps(Arguments* args);
void Exec(Arguments* args);          // exec, execute
void Give(Arguments* args);
void LootAll(Arguments* args);
void Join(Arguments* args);
void HostServer(Arguments* args);    // server
void MemUsage(Arguments* args);
void Disconnect(Arguments* args);
void FadeOut(Arguments* args);
void Net(Arguments* args);
void GenerateAcks(Arguments* args);
void Days(Arguments* args);
void PartyResend(Arguments* args);
void Kick(Arguments* args);
void Ban(Arguments* args);
void Fps(Arguments* args);
void Rate(Arguments* args);
void LocalRate(Arguments* args);
// generateacks' lines for a speech list / an environment speech list (a null list writes nothing).
void WriteSpeechLines(CStdioFile* file, const acks::Speeches* list);
void WriteEnvironmentSpeechLines(CStdioFile* file, const acks::EnvironmentSpeeches* list);
} // namespace commands

} // namespace console
