#pragma once

// The standalone's settings: ei_settings.ini next to the exe (on macOS in ~/Library/Application Support/ei-rebuild; or %LOCALAPPDATA%\EvilIslands\ei_settings.ini when the
// exe's folder has none and cannot be written; docs/notes/standalone.md, "Settings file"). The options (options.cpp),
// the renderer's switches, the mod, EI_Plugin's values and the console lines are read and written here through typed
// accessors; nothing reads or writes the registry.
//
// The file is a plain ini:
//   ; a comment (a line starting with ';' or '#'; kept when the game rewrites the file)
//   [video]
//   VideoResolution = 1920x1080
//   FullScreen = true
//   Brightness = 0.5
// Sections are letters, digits, spaces, '_' and '-'; a value is everything after the first '=' with the spaces around
// it trimmed (in double quotes when it starts or ends with a space or starts with a quote: the outer pair is
// dropped). Numbers are decimal, floats as short as reads back to the same float, booleans true / false (1 / 0,
// yes / no and on / off read too). Sections and names compare without case. Text is UTF-8.
// When the file is missing, or holds no setting (an empty file, or one in another format such as the registry-shaped
// file of earlier builds), the first use writes the defaults (Defaults) over it; when it exists but cannot be read
// (locked, unreadable), the session runs on the defaults in memory and writes nothing.
// Harness runs (verify build, tests/verify/harness_settings.h) pin a file of their own (Pin): written from the
// committed registry snapshot, read only (or writable, to check the options save), every read of a value it lacks
// reported.

#include <string>
#include <string_view>
#include <vector>

namespace settings {

inline constexpr const char* kFileName = "ei_settings.ini";

// The sections: the game's options (options.cpp), the renderer's switches (device_mesh.cpp), EI_Plugin's second master
// ([network] SecondMaster) and Rebirth's console lines (console_addon.cpp).
inline constexpr const char* kPaths = "paths";
inline constexpr const char* kVideo = "video";
inline constexpr const char* kSound = "sound";
inline constexpr const char* kShadows = "shadows";
inline constexpr const char* kTerrain = "terrain";
inline constexpr const char* kCamera = "camera";
inline constexpr const char* kGame = "game";
inline constexpr const char* kNetwork = "network";
inline constexpr const char* kRenderer = "renderer";
inline constexpr const char* kAutoExec = "autoexec";

// A settings file's text in memory: its sections in file order, each with its lines in order. Sections and names
// compare without case; a name keeps the spelling it was first given.
class Document {
public:
    // A `name = value` line, or (name empty) a comment line, kept as `value`.
    struct Line {
        std::string name;
        std::string value;
    };
    struct Section {
        std::string name;   // empty: the lines before the first section
        std::vector<Line> lines;
    };

    // The value of `name` in `section`, or null.
    const std::string* Find(std::string_view section, std::string_view name) const;
    // Sets the value, the section added at the end and the line at the section's end when missing.
    void Set(std::string_view section, std::string_view name, std::string_view value);
    // A comment line ("; " and `text`) at the end of `section` (empty: the lines before the first section).
    void Comment(std::string_view section, std::string_view text);
    // The number of values (comments not counted).
    std::size_t Values() const;

    std::string Text() const;
    // Reads `text` into the document; each line not understood (a value outside a section, a bad section name and
    // the lines below it) is skipped and described in `problems`.
    void Parse(std::string_view text, std::vector<std::string>* problems);

    std::vector<Section> sections;

private:
    Section* FindSection(std::string_view name);
    Section& AddSection(std::string_view name);
};

// The values' text.
std::string IntText(int value);
std::string FloatText(float value);
std::string BoolText(bool value);
// A value's text read as its type: false when it is not one (nothing stored).
bool ParseInt(std::string_view text, int* value);
bool ParseFloat(std::string_view text, float* value);
bool ParseBool(std::string_view text, bool* value);

// ---- The catalog: what every setting does (settings_catalog.inc; tools/gen_settings_doc.py writes docs/settings.md)

// A setting's default: what a new file holds when the row is written there, and what a read takes when the value is
// absent or not of its type. A number, a boolean, a float or a text; or the machine's (desktopSize: Machine).
struct Default {
    enum class Type { number, flag, real, text, desktopSize };
    Type type = Type::text;
    int number = 0;          // number; flag: 1 or 0
    float real = 0.0f;       // real
    const char* text = "";   // text
};

// What the machine decides of the defaults: the desktop's size in pixels (VideoResolution).
struct Machine {
    int desktopWidth = 0;
    int desktopHeight = 0;
};

struct Described {
    const char* section = nullptr;
    const char* name = nullptr;  // "<n>": a camera shortcut's number (0 .. kCameraShortcuts - 1); "<name>": any name
    Default value;               // the default
    bool written = false;        // a new file holds it (Defaults)
    const char* values = nullptr;  // the values it takes, short
    const char* description = nullptr;  // what it does, for the player
};
// The camera shortcuts: "<n>" in a catalog name.
inline constexpr int kCameraShortcuts = 4;
// Every setting the game reads, by section, in the order of settings_catalog.inc.
const Described* CatalogBegin();
const Described* CatalogEnd();
// The catalog's row for `name` in `section` (both without case), or null.
const Described* Describe(std::string_view section, std::string_view name);
// The row's default as a value's text (IntText, BoolText, FloatText; the desktop's size as WxH).
std::string DefaultText(const Described& row, const Machine& machine);

// The settings a missing file starts with, for `machine`: the catalog's rows marked written, in its order (a run of
// "<n>" rows shortcut by shortcut), each line with its description above it (once for the lines of a pattern), and
// the file's header comment. Full screen (a borderless window over the monitor) at the desktop's size, the movies on
// (the installers' Video 2), sound at full volume, every shadow, what a game install writes (the camera's section,
// the selection arrows, the master server) and the renderer's quality and vsync. The rest is left out: the game reads
// it as absent and takes the row's default.
Document Defaults(const Machine& machine);

// ---- The process's settings ----

// The value of `name` in `section` as its type, or the catalog row's default (Describe) when it is missing or not of
// that type (the last logged; a name without a row logged and read as 0, false or empty). A boolean reads any number
// too (not 0: true); a number reads true / false as 1 / 0.
int GetInt(const char* section, const char* name);
bool GetBool(const char* section, const char* name);
float GetFloat(const char* section, const char* name);
std::string GetString(const char* section, const char* name);
// Every value of `section`, in file order (its lines' names apart).
std::vector<std::string> GetValues(const char* section);
// Every name = value line of `section`, in file order (comments left out; a value it lacks is not one read).
std::vector<Document::Line> GetLines(const char* section);

// Sets the value; kept until Save.
void SetInt(const char* section, const char* name, int value);
void SetBool(const char* section, const char* name, bool value);
void SetFloat(const char* section, const char* name, float value);
void SetString(const char* section, const char* name, const std::string& value);
// Writes the file when a value changed (.tmp, then moved over it; not in a session on the defaults in memory).
void Save();

// A change of each value read (the harness's and the start-up's redirects of the game paths, src/main.cpp):
// called in the order added with the value's name and text, which it may change.
using ReadFilter = void (*)(const char* name, std::string& value);
void AddReadFilter(ReadFilter filter);

// Harness runs: `file` in place of the one next to the exe, and each read of a value it lacks passed to `missing`
// (section, name; may be null). `readOnly`: writes are dropped and a missing file stays missing; else the file is
// written as in the product (Save, .tmp then moved) and a missing file is written with the defaults. `prepare` (may
// be null) writes the file at the first use. Before the first use.
using MissingHandler = void (*)(const char* section, const char* name);
using PrepareHandler = void (*)(std::string_view file);
void Pin(std::string_view file, MissingHandler missing, bool readOnly, PrepareHandler prepare = nullptr);
// Harness runs: the desktop size the defaults take (a missing file's, VideoResolution's), in place of the primary
// monitor's.
void SetDesktopSize(int width, int height);

} // namespace settings
