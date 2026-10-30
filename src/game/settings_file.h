#pragma once

// The standalone's settings file (docs/notes/standalone.md, "Settings file"). The game keeps its settings
// under HKEY_CURRENT_USER: Software\Nival Interactive\EvilIslands (the options, options.cpp) and Rebirth's
// Software\Gipat.Ru\EI_Starter (its game settings and the starter's switches). ei-rebuild.exe keeps the same
// keys and values in ei_settings.ini next to the exe instead (or in %LOCALAPPDATA%\EvilIslands\ei_settings.ini when
// the exe's folder has none and cannot be written): SettingsKey (settings_key.cpp) and the direct readers
// (ReadValue) use it there, and never read or write the registry. When the file is missing, the first use writes it
// with the defaults (DefaultDocument); when it exists but cannot be read (locked, unreadable), the session runs on
// the defaults in memory and writes nothing. game.exe (the original game) keeps the registry.
// The file: a section per key (its path below HKEY_CURRENT_USER), a line per value, as reg export writes them:
//   [Software\Nival Interactive\EvilIslands\General Settings]
//   "FullScreen"=dword:00000001          REG_DWORD
//   "VideoResolution"="1920x1080"        REG_SZ (\\ and \" escaped)
//   "Brightness"=float:0.5               a 4-byte REG_BINARY holding a float (SettingsKey's floats)
//   "Data"=hex:01,02                     REG_BINARY
//   "Lines"=hex(7):61,00,00              any other type, or a string the quoted form cannot hold
// "@" names a key's default value; lines starting with ';' are comments. Every value round-trips bit for bit:
// the bytes are what the game's ANSI registry call gave (UTF-8).
// Harness runs (verify build, tests/verify/harness_settings.h) pin a file of their own (Pin): written from the
// committed registry snapshot, read only (or writable, to check the options save), every read of a value it lacks
// reported.

#include <deque>
#include <string>
#include <vector>

namespace settings_file {

inline constexpr const char* kFileName = "ei_settings.ini";

// A value's type: the registry's numbers, which the game and Rebirth read (a type not named here round-trips in the
// file as hex(<type>)).
enum ValueType : unsigned long {
    kTypeString = 1,         // REG_SZ: text with its terminator
    kTypeExpandString = 2,   // REG_EXPAND_SZ
    kTypeBinary = 3,         // REG_BINARY (SettingsKey's floats: 4 bytes)
    kTypeDword = 4,          // REG_DWORD: 4 bytes, little-endian
    kTypeMultiString = 7,    // REG_MULTI_SZ: strings, each terminated, and an empty one
};

// What Query and ReadValue return: the registry calls' error codes.
enum Status : long {
    kSuccess = 0,
    kNotFound = 2,            // ERROR_FILE_NOT_FOUND: the key or the value is missing
    kInvalidParameter = 87,   // ERROR_INVALID_PARAMETER: data without a size
    kMoreData = 234,          // ERROR_MORE_DATA: the size needed is in *size
};

// One value as RegQueryValueEx gives it: the type (ValueType's, or any other) and the bytes (a string's with its
// terminator).
struct Value {
    std::string name;
    unsigned long type = 0;
    std::string data;
};

struct Key {
    std::string path;   // below HKEY_CURRENT_USER, parts joined by one '\'
    std::vector<Value> values;
};

// A settings file's keys and values, in file order. Paths and names compare without case, as the registry's.
class Document {
public:
    Key* FindKey(const std::string& path);
    Key& AddKey(const std::string& path);   // the key, made when missing
    static Value* FindValue(Key& key, const std::string& name);
    void Set(const std::string& path, const std::string& name, unsigned long type, std::string data);

    std::string Text() const;
    // Reads `text` into the document; each line not understood is skipped and described in `problems`.
    void Parse(const std::string& text, std::vector<std::string>* problems);

    std::deque<Key> keys;   // a deque: a key stays where it is as others are added (SettingsKey holds one)
};

// The trees the options are kept in: the game's, and Rebirth's (its NivalRegistry patch reads the options there; its
// WideScreen layout reads the resolution there on either data).
inline constexpr const char* kOptionTrees[] = {"Software\\Nival Interactive\\EvilIslands",
                                               "Software\\Gipat.Ru\\EI_Starter\\EvilIslands"};

// The settings a missing file starts with, for a desktop of `width` x `height` pixels. In both option trees: full
// screen (a borderless window over the monitor) at that size, sound and music at the game's own defaults, and every
// graphics option at its best (the starters' best detail level: water waves on, LOD distances 90 and 130, the most
// frequent relighting; trilinear filtering, full-size textures, mipmaps, every shadow, self shadowing), and what a game
// install writes in place of the game's own placeholder defaults (the camera's section, the objects' lighting, the
// hero's and the quest light, the master server). The game's Standalone key: Msaa 8 (the device's best when less),
// Anisotropy 16, VSync on. Rebirth's starter switches: WideScreen on, the draw radii 5376 (the whole map; Rebirth's
// own default is the original's 100). Everything else (paths, the other options and starter switches) is left out:
// the game and Rebirth read it as absent and take their own defaults.
Document DefaultDocument(int width, int height);

// `sub` below `path` as a key path: the parts of both, empty ones (doubled or outer '\') skipped.
std::string JoinPath(const char* path, const char* sub);

// ---- The process's settings (the standalone; game.exe uses the registry) ----

// Opens the key `path` (below HKEY_CURRENT_USER), making it when missing as RegCreateKeyEx does: the handle
// SettingsKey holds.
void* CreateKey(const char* path);
// RegQueryValueEx of the value `name` of `sub` below `key` (a CreateKey handle, or null: below
// HKEY_CURRENT_USER): 0, ERROR_MORE_DATA (the size needed in *size) or ERROR_FILE_NOT_FOUND (key or value
// missing).
long Query(const void* key, const char* sub, const char* name, unsigned long* type, void* data, unsigned long* size);
// RegSetValueEx of `name` of `sub` below `key` (made when missing): 0. Kept until Flush.
long Set(const void* key, const char* sub, const char* name, unsigned long type, const void* data, unsigned long size);
// Writes the file when a value changed (not in a session on the defaults in memory).
void Flush();

// RegQueryValueEx of the value `name` of HKEY_CURRENT_USER\<path>, for the readers that open, query and close
// a key directly: the settings file's.
long ReadValue(const char* path, const char* name, unsigned long* type, void* data, unsigned long* size);

// A change of each value read (the harness's and the start-up's redirects of the game paths, standalone_main.cpp):
// called in the order added with the value's name, type and bytes, which it may change.
using ReadFilter = void (*)(const char* name, unsigned long type, std::string& data);
void AddReadFilter(ReadFilter filter);

// Harness runs: `file` in place of the one next to the exe, and each read of a key or value it lacks passed to
// `missing` (key path, value name; may be null). `readOnly`: writes are dropped and a missing file stays missing;
// else the file is written as in the product (Flush at SettingsKey::Close, .tmp then moved) and a missing file is
// written with the defaults. Before the first use.
using MissingHandler = void (*)(const char* path, const char* name);
void Pin(const char* file, MissingHandler missing, bool readOnly = true);
// Harness runs: the desktop size a missing file's defaults take, in place of the primary monitor's.
void SetDesktopSize(int width, int height);

} // namespace settings_file
