#pragma once

// The standalone's settings: ei_settings.ini next to the exe (or %LOCALAPPDATA%\EvilIslands\ei_settings.ini when the
// exe's folder has none and cannot be written; docs/notes/standalone.md, "Settings file"). The options (options.cpp),
// the renderer's switches, Rebirth's switches and console lines are read and written here through typed accessors;
// nothing reads or writes the registry (game.exe, the original game, keeps it).
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

// The sections: the game's options (options.cpp), the renderer's switches (device_mesh.cpp), Rebirth's switches
// (ui_widescreen.cpp, world_client_draw.cpp, mod_folders.cpp) and its console lines (console_addon.cpp).
inline constexpr const char* kPaths = "paths";
inline constexpr const char* kVideo = "video";
inline constexpr const char* kSound = "sound";
inline constexpr const char* kShadows = "shadows";
inline constexpr const char* kTerrain = "terrain";
inline constexpr const char* kCamera = "camera";
inline constexpr const char* kGame = "game";
inline constexpr const char* kNetwork = "network";
inline constexpr const char* kStatistics = "statistics";
inline constexpr const char* kRenderer = "renderer";
inline constexpr const char* kRebirth = "rebirth";
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

// The settings a missing file starts with, for a desktop of `width` x `height` pixels: full screen (a borderless
// window over the monitor) at that size, sound and music at the game's own defaults, every graphics option at its
// best (the starters' best detail level: water waves on, LOD distances 90 and 130, the most frequent relighting;
// trilinear filtering, full-size textures, mipmaps, every shadow, self shadowing), and what a game install writes in
// place of the game's own placeholder defaults (the camera's section, the objects' lighting, the hero's and the
// quest light, the master server). The renderer: Msaa 8 (the device's best when less), Anisotropy 16, VSync on, no
// performance overlay. Rebirth's switches: WideScreen on, the draw radii 5376 (the whole map; Rebirth's own default
// is the original's 100). Everything else (paths, the other options and switches) is left out: the game and Rebirth
// read it as absent and take their own defaults.
Document Defaults(int width, int height);

// ---- The process's settings (the standalone; game.exe uses the registry) ----

// The value of `name` in `section` as its type, or `defaultValue` when it is missing or not of that type (the last
// logged). A boolean reads any number too (not 0: true); a number reads true / false as 1 / 0.
int GetInt(const char* section, const char* name, int defaultValue);
bool GetBool(const char* section, const char* name, bool defaultValue);
float GetFloat(const char* section, const char* name, float defaultValue);
std::string GetString(const char* section, const char* name, const std::string& defaultValue);
// Every value of `section`, in file order (its lines' names apart).
std::vector<std::string> GetValues(const char* section);

// Sets the value; kept until Save.
void SetInt(const char* section, const char* name, int value);
void SetBool(const char* section, const char* name, bool value);
void SetFloat(const char* section, const char* name, float value);
void SetString(const char* section, const char* name, const std::string& value);
// Writes the file when a value changed (.tmp, then moved over it; not in a session on the defaults in memory).
void Save();

// A change of each value read (the harness's and the start-up's redirects of the game paths, standalone_main.cpp):
// called in the order added with the value's name and text, which it may change.
using ReadFilter = void (*)(const char* name, std::string& value);
void AddReadFilter(ReadFilter filter);

// Harness runs: `file` in place of the one next to the exe, and each read of a value it lacks passed to `missing`
// (section, name; may be null). `readOnly`: writes are dropped and a missing file stays missing; else the file is
// written as in the product (Save, .tmp then moved) and a missing file is written with the defaults. `prepare` (may
// be null) writes the file at the first use, once the start-up has set the Rebirth flags. Before the first use.
using MissingHandler = void (*)(const char* section, const char* name);
using PrepareHandler = void (*)(const char* file);
void Pin(const char* file, MissingHandler missing, bool readOnly, PrepareHandler prepare = nullptr);
// Harness runs: the desktop size a missing file's defaults take, in place of the primary monitor's.
void SetDesktopSize(int width, int height);

} // namespace settings
