#pragma once

// The developer console: the streambuf that collects console output, the command executor with its two
// command maps, the "@"/"#"/"$" script lines and the command handlers. See docs/notes/console.md.

#include "lib/gdi.h"
#include "lib/static_object.h"
#include "lib/str.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>
#include <windows.h>

class CInterface3D;
class CUiContainer;
class CUiSurface;
namespace acks { struct Speeches; struct EnvironmentSpeeches; }
namespace lib { class TextFile; }

namespace console {


// The console's streambuf (eight static instances, one per message filter: 0 the console itself, then
// ai 1, event 2, graphics 4, rpg 8, unitacks 0x10, net 0x20, all -1). Characters collect in the put
// area (buffer_) and move into line_ on overflow; sync hands the line to the console window.
// MSVC 6's streambuf as the console's used it: the put area is set up by the first overflow while the
// category is shown; a hidden category's overflow leaves it as it is (what it holds shows with the next
// line once the category is shown again).
class ConsoleStreambuf {
public:
    explicit ConsoleStreambuf(unsigned filter);
    ConsoleStreambuf(const ConsoleStreambuf&) = delete;
    ConsoleStreambuf& operator=(const ConsoleStreambuf&) = delete;

    void Put(char c);                    // into the put area, else overflow
    void Write(std::string_view text);   // byte by byte, as Put
    // Overflow(EOF), the line to the console window (an empty one too), then empty it.
    void Sync();

private:
    // Moves the put area (and `c`, unless EOF) into the line when this streambuf's category is shown.
    void Overflow(int c);

    unsigned filter_;          // message category; 0 = always shown
    std::string line_;
    std::size_t putEnd_ = 0;   // the put area's size: 0 until the first overflow while shown
    std::size_t putNext_ = 0;  // characters in the put area
    char buffer_[0x400];
};

// One of the console's eight output streams (MSVC 6's ostream as the game used it: no width, flags,
// precision or tie ever set): text, characters and numbers into its streambuf; Flush is the streambuf's
// sync. Zero until its static initialiser attaches it to its streambuf.
class Stream {
public:
    constexpr Stream() = default;
    void Attach(ConsoleStreambuf* buffer) { buffer_ = buffer; }

    Stream& operator<<(const char* text);
    Stream& operator<<(const std::string& text);
    Stream& operator<<(char c);
    Stream& operator<<(int value);        // "%d"
    Stream& operator<<(long value);       // "%ld"
    Stream& operator<<(unsigned value);   // "%u"
    // "%.6g" through str::Format (VC6's float digits). A float is printed as the double it widens to.
    Stream& operator<<(double value);
    Stream& Flush();

private:
    ConsoleStreambuf* buffer_ = nullptr;
};

// The streams, one per message filter (their streambufs below): the console itself, then all, unitacks (the AI
// alarm's log), net (the network log), rpg (the hit log), graphics (the battle screen's "Terrain Coords" of a click
// on the ground), event, ai (combat's and the AI's logs).
extern Stream g_consoleOut;
extern Stream g_allLogOut;
extern Stream g_unitAcksLogOut;
extern Stream g_netLogOut;
extern Stream g_rpgLogOut;
extern Stream g_graphicsLogOut;
extern Stream g_eventLogOut;
extern Stream g_aiLogOut;
// `<< '\n'` and a flush, how every console message ends.
void EndLine(Stream& os);
void PrintLine(const char* text);

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
    explicit ConsoleWindow(CInterface3D* screen);
    // The surface and the font deleted; called by the interface's shutdown.
    ~ConsoleWindow();
    ConsoleWindow(const ConsoleWindow&) = delete;
    ConsoleWindow& operator=(const ConsoleWindow&) = delete;
    void SetRect(const RECT* rect);   // 800x600 coordinates
    void SetHistory(int lines);
    void AddText(std::string_view text);  // continues the last line; '\n' starts a new one
    // Drawing and input:
    void Show(bool shown);                     // creates or releases the surface
    void OnKeyDown(int key, int repeat, int flags);   // editing and scrolling keys
    void OnChar(int key, int repeat, int flags);      // typed characters, Enter, Escape
    void OnKeyUp(int key, int repeat, int flags);     // VK_SHIFT / VK_CONTROL clear shift_ / ctrl_
    void DrawLines();                          // the lines above the input line
    void DrawInput();                          // the input line, selection and caret
    // Called by the interface's 2D redraw while the console is shown. The surface's blit when there is one.
    void Blit();
    // Keeps the last `count` lines.
    void DropOldLines(int count);

    CInterface3D* screen_;           // the 3D interface (its width_ and height_ in pixels)
    CUiSurface* surface_;            // while shown
    gdi::Font font_;                 // selected into the surface's DC
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
};
using CommandHandler = void(*)(std::vector<std::string>* args);

// Keys compared by _mbscmp. Kept as an alias: the spelled-out map type is 50 characters and named at ten sites.
using CommandMap = std::map<std::string, CommandHandler, str::MbsLess>;

// The handler slot for `key`, inserted with a null handler if missing.
CommandHandler& Command(CommandMap* map, const std::string& key);

// The commands always available and those after the cheat word.
extern lib::StaticObject<CommandMap> g_publicCommands;
extern lib::StaticObject<CommandMap> g_cheatCommands;

// The registrations the executor makes on every call, in its order: map, name, handler.
struct CommandRegistration {
    bool cheat;
    const char* name;
    CommandHandler handler;
};
extern const CommandRegistration kCommandRegistrations[27];

void Execute(const std::string& text);
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
int Tokenize(std::string_view text, std::vector<std::string>* out);
// Loads prefix + text + " arg"... + suffix into the mission script and runs it; prints
// "Script error: ..." when it does not load.
void RunScriptLine(const std::string& text, const std::vector<std::string>& args, const char* prefix, const char* suffix);

namespace commands {
void Help(std::vector<std::string>* args);
void DebugInfo(std::vector<std::string>* args);
void ConsolePos(std::vector<std::string>* args);    // console
void History(std::vector<std::string>* args);
void Filter(std::vector<std::string>* args);
void Show(std::vector<std::string>* args);
void ListVar(std::vector<std::string>* args);
void LoadVar(std::vector<std::string>* args);
void LastFps(std::vector<std::string>* args);
void Exec(std::vector<std::string>* args);          // exec, execute
void Give(std::vector<std::string>* args);
void LootAll(std::vector<std::string>* args);
void Join(std::vector<std::string>* args);
void HostServer(std::vector<std::string>* args);    // server
void MemUsage(std::vector<std::string>* args);
void Disconnect(std::vector<std::string>* args);
void FadeOut(std::vector<std::string>* args);
void Net(std::vector<std::string>* args);
void GenerateAcks(std::vector<std::string>* args);
void Days(std::vector<std::string>* args);
void PartyResend(std::vector<std::string>* args);
void Kick(std::vector<std::string>* args);
void Ban(std::vector<std::string>* args);
void Fps(std::vector<std::string>* args);
void Rate(std::vector<std::string>* args);
void LocalRate(std::vector<std::string>* args);
// generateacks' lines for a speech list / an environment speech list (a null list writes nothing).
void WriteSpeechLines(lib::TextFile* file, const acks::Speeches* list);
void WriteEnvironmentSpeechLines(lib::TextFile* file, const acks::EnvironmentSpeeches* list);
} // namespace commands

} // namespace console
