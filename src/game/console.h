#pragma once

// The developer console: the streambuf that collects console output, the command executor with its two
// command maps, the "@"/"#"/"$" script lines and the command handlers. See docs/notes/console.md.

#include "game/ui_font.h"
#include "platform/platform.h"
#include "lib/bit_flags.h"
#include "lib/str.h"
#include "lib/rect.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

class UiContainer;
class UiSurface;
namespace acks { struct Speeches; struct EnvironmentSpeeches; }
namespace platform { class File; }

namespace console {

// The message categories: one bit each, shown when the "filter" command has it on. The console itself (0) is always
// shown; LogCategory::All is every category's bit.
enum class LogCategory : unsigned {
    Console = 0,
    Ai = 1,
    Event = 2,
    Graphics = 4,
    Rpg = 8,
    UnitAcks = 0x10,
    Net = 0x20,
    All = 0xFFFFFFFF,
};
constexpr bool EnableBitFlags(LogCategory) { return true; }

// The console's streambuf (eight static instances, one per message category). Characters collect in the put
// area (buffer) and move into line on overflow; sync hands the line to the console window.
// MSVC 6's streambuf as the console's used it: the put area is set up by the first overflow while the
// category is shown; a hidden category's overflow leaves it as it is (what it holds shows with the next
// line once the category is shown again).
class ConsoleStreambuf {
public:
    explicit ConsoleStreambuf(LogCategory category);
    ConsoleStreambuf(const ConsoleStreambuf&) = delete;
    ConsoleStreambuf& operator=(const ConsoleStreambuf&) = delete;

    void Put(char c);                    // into the put area, else overflow
    void Write(std::string_view text);   // byte by byte, as Put
    // Overflow(EOF), the line to the console window (an empty one too), then empty it.
    void Sync();

private:
    // Moves the put area (and `c`, unless EOF) into the line when this streambuf's category is shown.
    void Overflow(int c);

    LogCategory category = {};  // LogCategory::Console: always shown
    std::string line;
    std::size_t putEnd = 0;    // the put area's size: 0 until the first overflow while shown
    std::size_t putNext = 0;   // characters in the put area
    char buffer[0x400] = {};
};

// One of the console's eight output streams (MSVC 6's ostream as the game used it: no width, flags,
// precision or tie ever set): text, characters and numbers into its streambuf; Flush is the streambuf's
// sync.
class Stream {
public:
    constexpr explicit Stream(ConsoleStreambuf& newBuffer) : buffer(&newBuffer) {}

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
    ConsoleStreambuf* buffer = nullptr;
};

// The streams, one per message filter (each on its own streambuf, console.cpp): the console itself, then all, unitacks (the AI
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

// The message categories shown (bit per filter, "filter" command).
inline LogCategory g_filterMask = /*007C7B68*/ {};
// Set by the cheat word; unlocks the second command map and the script lines.
inline bool g_cheatEnabled = /*007C7B64*/ {};

constexpr int kHistory = 0x100;   // lines kept
constexpr int kNoCommand = -1;    // no command is shown by up / down

// The console window (the interface manager keeps it). Lines are kept as text;
// the surface is painted from them each frame.
class ConsoleWindow {
public:
    // Made by Interface3D::Create: a 9-point "Courier New" font, the rectangle (0, 0)-(800, 150), a 256-line history.
    ConsoleWindow();
    // The surface and the font deleted; called by the interface's shutdown.
    ~ConsoleWindow();
    ConsoleWindow(const ConsoleWindow&) = delete;
    ConsoleWindow& operator=(const ConsoleWindow&) = delete;
    void SetRect(const Rect& rect);   // 800x600 coordinates
    void OnResize();                  // the rectangle, font and metrics again for the new screen size
    int Px(int layoutPixels) const;   // a size of the 800x600 layout in screen pixels (text and strips scale with the window)
    void SetScale();                  // scale and font from the screen layout
    void SetHistory(int lines);
    void AddText(std::string_view text);  // continues the last line; '\n' starts a new one
    // Drawing and input:
    void Show(bool shown);                     // creates or releases the surface
    void OnKeyDown(platform::Key key, int scan, bool repeat);   // editing and scrolling keys
    void OnChar(int key, int scan);      // typed characters, Enter, Escape
    void OnKeyUp(platform::Key key, int scan);     // Shift / Control clear shift / ctrl
    void ClampScroll();                        // the scroll position within the lines
    void ClampCaret();                         // the caret and the anchor within the input
    // The surface's paint: the lines above the input line; the input line, selection and caret.
    void PaintLines(UiSurface& surface) const;
    void PaintInput(UiSurface& surface) const;
    // Called by the interface's 2D redraw while the console is shown. The surface's blit when there is one.
    void Blit();
    // Keeps the last `count` lines.
    void DropOldLines(int count);

    UiSurface* surface = nullptr;   // while shown
    UiFont font;                     // the console's text
    std::vector<std::string> commands;    // commands entered (input history), the newest first
    int commandIndex = kNoCommand;   // the one shown by up/down (-1 or size: none)
    std::string input;               // the line being typed
    int caret = 0;                   // caret position (bytes, at a character's start: U4)
    int anchor = 0;                  // other end of the selection (== caret: none)
    int width = 0;                   // pixels, at least 76
    int height = 0;                  // pixels, at least 69
    Rect rect;                       // the window in 800x600 coordinates (after clamping)
    Rect screenRect;                 // the window in pixels
    Rect requestedRect;              // the window as last asked for (800x600 coordinates, before clamping)
    float scale = 1.0f;              // screen pixels per layout pixel
    std::vector<std::string> lines;
    int history = kHistory;          // lines kept
    int visibleLines = 0;
    int scroll = 0;                  // lines scrolled back (0 = at the end)
    bool ctrl = false;               // Ctrl held (set by key down 0x11)
    bool shift = false;              // Shift held (0x10): the selection grows
    bool shown = false;
};
using CommandArgs = std::vector<std::string>;
using CommandHandler = void(*)(const CommandArgs& args);

// Keys compared by _mbscmp. Kept as an alias: the spelled-out map type is 50 characters and named at ten sites.
using CommandMap = std::map<std::string, CommandHandler, str::MbsLess>;

// The handler for `key`, or null (nothing inserted).
CommandHandler FindCommand(const CommandMap& map, const std::string& key);

// A command: the map it goes in (those after the cheat word, or those always available), its name and handler.
struct CommandRegistration {
    bool cheat = false;
    const char* name = nullptr;
    CommandHandler handler = nullptr;
};
extern const CommandRegistration kCommandRegistrations[25];

void Execute(const std::string& text);
// Rebirth's console additions. AddNewConsoleCommands: the commands registered after the game's ("help" replaces the
// game's, which addon's own "help" calls first); the battle screen's drawing applies "nogui" to its panels.
// AutoExec: the "Console AutoExec" lines, run before three of the game's console executions.
extern const CommandRegistration kAddonCommandRegistrations[6];
void ApplyAddonGuiVisibility(UiContainer* screen);
void RunAddonAutoExec();
// A console line split at spaces (a token in double quotes keeps its spaces and quotes).
std::vector<std::string> Tokenize(std::string_view text);
// Loads prefix + text + " arg"... + suffix into the mission script and runs it; prints
// "Script error: ..." when it does not load.
void RunScriptLine(const std::string& text, const std::vector<std::string>& args, const char* prefix, const char* suffix);

namespace commands {
void Help(const CommandArgs& args);
void ConsolePos(const CommandArgs& args);    // console
void History(const CommandArgs& args);
void Filter(const CommandArgs& args);
void ListVar(const CommandArgs& args);
void LoadVar(const CommandArgs& args);
void LastFps(const CommandArgs& args);
void Exec(const CommandArgs& args);          // exec, execute
void Give(const CommandArgs& args);
void LootAll(const CommandArgs& args);
void Join(const CommandArgs& args);
void HostServer(const CommandArgs& args);    // server
void MemUsage(const CommandArgs& args);
void Disconnect(const CommandArgs& args);
void FadeOut(const CommandArgs& args);
void Net(const CommandArgs& args);
void GenerateAcks(const CommandArgs& args);
void Days(const CommandArgs& args);
void PartyResend(const CommandArgs& args);
void Kick(const CommandArgs& args);
void Ban(const CommandArgs& args);
void Fps(const CommandArgs& args);
void Rate(const CommandArgs& args);
void LocalRate(const CommandArgs& args);
// generateacks' lines for a speech list / an environment speech list (a null list writes nothing).
void WriteSpeechLines(platform::File* file, const acks::Speeches* list);
void WriteEnvironmentSpeechLines(platform::File* file, const acks::EnvironmentSpeeches* list);
} // namespace commands

} // namespace console
