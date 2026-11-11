// The game settings a harness run reads: the registry snapshot and the pinned install files. See
// harness_settings.h.
#include "harness_settings.h"

#include "harness_starter.h"

#include "image/image_side.h"
#include "lib/text_case.h"
#include "platform/log.h"
#include "platform/platform.h"
#include "system/harness_system.h"

#include <atomic>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <vector>

namespace ei::verify::settings {
namespace {

using ei::Log;
namespace sys = ei::verify::sys;

struct Value {
    std::string name;      // as the snapshot writes it
    unsigned long type = 0;   // starter::kString, kBinary, kDword (the registry's types)
    std::vector<unsigned char> data;
    bool absent = false;   // "name"=-: read, absent on purpose
};

struct Key {
    std::string path;      // as the snapshot writes it
    bool absent = false;   // [-key]
    std::map<std::string, Value> values;   // by lower-case name
};

// config\harness (a function's static: the harness's static constructor sets it before this file's globals would be
// constructed).
std::string& Folder() {
    static std::string folder;
    return folder;
}

// The module's state, made on first use: the harness's static constructor installs it before this file's
// globals would be constructed.
struct State {
    std::map<std::string, Key> keys;   // by lower-case full path ("hkey_current_user\software\...")
    std::set<std::string> problemSet;
    std::vector<std::string> problems;
};
State& S() {
    static State state;
    return state;
}
std::mutex& Lock() {
    static std::mutex lock;
    return lock;
}

void Problem(const std::string& what) {
    bool fresh;
    {
        std::lock_guard<std::mutex> lock(Lock());
        fresh = S().problemSet.insert(what).second;
        if (fresh) S().problems.push_back(what);
    }
    if (fresh) Log("harness: SETTINGS: %s", what.c_str());
}

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// ---- the snapshot file (the .reg format reg export writes, in ASCII) ----

// The quoted string at `p` (escapes \\ and \"); `p` moves past its closing quote.
bool ParseQuoted(const char*& p, std::string* out) {
    if (*p != '"') return false;
    ++p;
    out->clear();
    while (*p && *p != '"') {
        if (*p == '\\' && p[1]) ++p;
        out->push_back(*p++);
    }
    if (*p != '"') return false;
    ++p;
    return true;
}

bool ParseHexBytes(const char* p, std::vector<unsigned char>* out) {
    while (*p) {
        while (*p == ',' || *p == ' ' || *p == '\t') ++p;
        if (!*p) break;
        unsigned byte;
        if (!std::isxdigit(static_cast<unsigned char>(p[0])) || !std::isxdigit(static_cast<unsigned char>(p[1])) ||
            std::sscanf(p, "%2x", &byte) != 1)
            return false;
        out->push_back(static_cast<unsigned char>(byte));
        p += 2;
    }
    return true;
}

bool ParseValue(const char* p, Value* v) {
    if (std::strcmp(p, "-") == 0) {
        v->absent = true;
        return true;
    }
    if (*p == '"') {
        std::string text;
        if (!ParseQuoted(p, &text) || *p) return false;
        v->type = starter::kString;
        v->data.assign(text.begin(), text.end());
        v->data.push_back(0);
        return true;
    }
    if (text::CompareIgnoringCase(p, "dword:", 6) == 0) {
        std::uint32_t dword;
        unsigned parsed;
        if (std::sscanf(p + 6, "%8x", &parsed) != 1) return false;
        dword = parsed;
        v->type = starter::kDword;
        v->data.resize(4);
        std::memcpy(v->data.data(), &dword, 4);
        return true;
    }
    if (text::CompareIgnoringCase(p, "hex:", 4) == 0) {
        v->type = starter::kBinary;
        return ParseHexBytes(p + 4, &v->data);
    }
    unsigned type;
    int used = 0;
    if (std::sscanf(p, "hex(%x):%n", &type, &used) == 1 && used > 0) {
        v->type = type;
        return ParseHexBytes(p + used, &v->data);
    }
    return false;
}

void Load() {
    const std::string path = Folder() + "\\registry.reg";
    FILE* f = sys::OpenFile(path, "rb");
    if (!f) {
        Problem(std::string("cannot read the registry snapshot ") + path);
        return;
    }
    std::string text;
    char chunk[4096];
    for (std::size_t n; (n = std::fread(chunk, 1, sizeof(chunk), f)) > 0;) text.append(chunk, n);
    std::fclose(f);

    Key* key = nullptr;
    int lineNumber = 0, values = 0;
    std::string line;
    for (std::size_t at = 0; at <= text.size();) {
        std::size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        std::string part = text.substr(at, end - at);
        at = end + 1;
        ++lineNumber;
        while (!part.empty() && (part.back() == '\r' || part.back() == ' ' || part.back() == '\t')) part.pop_back();
        std::size_t first = part.find_first_not_of(" \t");
        part = first == std::string::npos ? std::string() : part.substr(first);
        // A value's hex bytes continue on the next line after a backslash.
        if (!part.empty() && part.back() == '\\') {
            part.pop_back();
            line += part;
            continue;
        }
        line += part;
        if (line.empty() || line[0] == ';' || line.rfind("Windows Registry Editor", 0) == 0) {
            line.clear();
            continue;
        }
        bool ok = true;
        if (line[0] == '[' && line.back() == ']') {
            const bool absent = line[1] == '-';
            const std::string keyPath = line.substr(absent ? 2 : 1, line.size() - (absent ? 3 : 2));
            key = &S().keys[Lower(keyPath)];
            key->path = keyPath;
            key->absent = absent;
        } else if (key) {
            const char* p = line.c_str();
            std::string name;
            if (*p == '@') {
                ++p;
            } else {
                ok = ParseQuoted(p, &name);
            }
            Value v;
            v.name = name;
            ok = ok && *p == '=' && ParseValue(p + 1, &v);
            if (ok) {
                key->values[Lower(name)] = v;
                ++values;
            }
        } else {
            ok = false;
        }
        if (!ok) Problem(std::string("registry.reg line ") + std::to_string(lineNumber) + " not understood: " + line);
        line.clear();
    }
    Log("harness: registry snapshot %s: %u keys, %d values; the user's settings are not read", path.c_str(),
        unsigned(S().keys.size()), values);
}

// ---- the standalone's settings file (engine/data/settings.h) ----

// The run's settings file is written from the snapshot's option tree game.exe reads on this install (the original's,
// or Rebirth's: addon.dll's NivalRegistry patch), its keys by section, and Rebirth's switches.
constexpr const char* kOriginalTree = "hkey_current_user\\software\\nival interactive\\evilislands\\";
constexpr const char* kRebirthTree = "hkey_current_user\\software\\gipat.ru\\ei_starter\\evilislands\\";
constexpr const char* kRebirthSwitches = "hkey_current_user\\software\\gipat.ru\\ei_starter\\settings";
constexpr struct {
    const char* key;
    const char* section;
} kSections[] = {
    {"camera settings", "camera"},     {"game specific settings", "game"}, {"general settings", "video"},
    {"network settings", "network"},   {"path settings", "paths"},         {"shadow settings", "shadows"},
    {"statistics output settings", "statistics"}, {"terrain settings", "terrain"},
};
// General Settings' values that go elsewhere.
constexpr const char* kSoundValues[] = {"audio", "audiostreaming", "audiosfx", "audiospeech", "reverse stereo"};

// The section of the value `name` (lower case) of the key `path` (lower case), or null when the file has none.
const char* SectionOf(const std::string& path, const std::string& name) {
    if (path == kRebirthSwitches) return "rebirth";
    const char* tree = starter::RebirthData() ? kRebirthTree : kOriginalTree;
    if (path.compare(0, std::strlen(tree), tree) != 0) return nullptr;
    const std::string key = path.substr(std::strlen(tree));
    for (const auto& s : kSections) {
        if (key != s.key) continue;
        if (key == "general settings") {
            for (const char* sound : kSoundValues)
                if (name == sound) return "sound";
            if (name == "joystick") return "game";
        }
        return s.section;
    }
    return nullptr;
}

// A value as the settings file holds it: a DWORD as its int, a 4-byte binary as its float, a string up to its
// terminator; false for other types (the image reads none).
bool ValueText(unsigned long type, const std::string& data, std::string* text) {
    if (type == starter::kDword && data.size() == 4) {
        std::int32_t value;
        std::memcpy(&value, data.data(), 4);
        *text = std::to_string(value);
        return true;
    }
    if (type == starter::kBinary && data.size() == 4) {
        float value;
        std::memcpy(&value, data.data(), 4);
        char number[32];
        const auto written = std::to_chars(number, number + sizeof(number), value);
        text->assign(number, written.ptr);
        return true;
    }
    if (type == starter::kString) {
        text->assign(data.c_str());
        return true;
    }
    return false;
}

// The values the snapshot marks read and absent ("name"=-), "<section>\<name>" in lower case.
std::set<std::string> g_absent;

// A read of a value the pinned file lacks: a problem unless the snapshot marks it read and absent.
void PinnedMissing(const char* section, const char* name) {
    if (g_absent.count(Lower(std::string(section) + "\\" + name))) return;
    Problem(std::string("setting not in the run's file (registry value not in the snapshot): [") + section + "] " + name);
}

// How the run's settings file is used (the scenario's "settingsfile" line).
enum class Mode { ReadOnly, Writable, Defaults };
Mode g_mode = Mode::ReadOnly;

// Odd values in a section of their own (so the game keeps them as it keeps what it does not know), for the save to
// carry as they are, with a comment.
void AddProbeSection(std::vector<image::SettingsLine>& lines) {
    const char* section = "probe";
    lines.push_back({section, "", "a comment the save keeps"});
    lines.push_back({section, "path", "C:\\Probe Dir\\\"quoted\"\\end\\"});
    lines.push_back({section, "spaces", "  two spaces around  "});
    lines.push_back({section, "quoted", "\"in quotes\""});
    lines.push_back({section, "equals", "a = b ; c # d"});
    lines.push_back({section, "empty", ""});
    lines.push_back({section, "utf8", "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82"});
    lines.push_back({section, "minus zero", "-0"});
    lines.push_back({section, "not a number", "0x10"});
}

// The run's settings file: the snapshot's values (SectionOf), each value the scenario's starter lines answer (any
// key) in place of the snapshot's. Written at the first use of the settings.
void WriteSettingsFile(std::string_view fileName) {
    const std::string file(fileName);
    std::vector<image::SettingsLine> lines;
    lines.push_back({"", "", "The run's settings: config/harness/registry.reg's and the scenario's (harness_settings.h)."});
    for (const auto& [lower, key] : S().keys) {
        if (key.absent) continue;
        for (const auto& [lowerName, value] : key.values) {
            const char* section = SectionOf(lower, lowerName);
            if (!section) continue;
            unsigned long type, size, dword;
            const unsigned char* data;
            const bool answered = starter::Answer(value.name.c_str(), &type, &data, &size, &dword);
            // A value absent on purpose stays absent unless a starter line answers it (the "mod" line's Mod).
            if (value.absent && !answered) {
                g_absent.insert(std::string(section) + "\\" + lowerName);
                continue;
            }
            std::string bytes(value.data.begin(), value.data.end());
            if (answered) {
                bytes.assign(reinterpret_cast<const char*>(data), size);
            } else {
                type = value.type;
            }
            std::string text;
            if (ValueText(type, bytes, &text)) lines.push_back({section, value.name, text});
        }
    }
    if (g_mode == Mode::Writable) AddProbeSection(lines);
    const std::string text = image::SettingsText(lines);
    FILE* f = sys::OpenFile(file, "wb");
    if (!f || std::fwrite(text.data(), 1, text.size(), f) != text.size()) {
        if (f) std::fclose(f);
        Problem(std::string("cannot write the run's settings file ") + file);
        return;
    }
    std::fclose(f);
    Log("harness: settings written to %s (the registry snapshot's %s tree and the starter lines; %s)", file,
        starter::RebirthData() ? "Rebirth's" : "the original's",
        g_mode == Mode::ReadOnly ? "read only" : "writable: the exit save rewrites it");
}

// The run's settings file pinned (read only, or writable): written by WriteSettingsFile at the first use, or with
// `settingsfile defaults` missing (the first use writes the defaults).
void PinSettingsFile() {
    const std::string path = ei::OutputPath(image::SettingsFileName());
    const char* file = path.c_str();
    sys::RemoveFile(path + ".tmp");
    if (g_mode == Mode::Defaults) {
        // No file: the first use writes the defaults, for the image runs' 1280x720 desktop (so the file does not
        // depend on the machine's monitor).
        sys::RemoveFile(path);
        image::DefaultSettings(file, 1280, 720);
        Log("harness: no settings file: the first use writes %s with the defaults", file);
        return;
    }
    sys::RemoveFile(path);
    image::PinSettings(file, &PinnedMissing, g_mode == Mode::ReadOnly, &WriteSettingsFile);
}

} // namespace

bool ParseStep(const std::string& verb, const std::string& argument) {
    if (verb != "settingsfile") return false;
    if (argument == "writable") g_mode = Mode::Writable;
    else if (argument == "defaults") g_mode = Mode::Defaults;
    else Problem("settingsfile needs writable or defaults");
    return true;
}

void Install() {
    std::string folder = platform::EnvironmentVariable("EI_HARNESS_SETTINGS");
    if (folder.empty()) {
        folder = platform::ProgramPath();
        const std::size_t slash = folder.find_last_of("\\/");
        if (slash != std::string::npos) folder.resize(slash);
        // macOS: the program is in build/<config>/ei-rebuild.app/Contents/MacOS; the folder is the bundle's.
        const std::string bundle = ".app/Contents/MacOS";
        if (folder.size() > bundle.size() && folder.compare(folder.size() - bundle.size(), bundle.size(), bundle) == 0)
            folder = folder.substr(0, folder.find_last_of('/', folder.size() - bundle.size()));
        folder += "\\..\\..\\config\\harness";
    }
    Folder() = sys::FullPath(folder);
    if (Folder().empty()) Folder() = folder;
    Load();
    PinSettingsFile();   // the image reads its settings file; nothing in the program reads the registry

    for (const char* file : {"vanilla\\keyboard.ini", "vanilla\\addrbook.ini", "rebirth\\keyboard.ini", "rebirth\\addrbook.ini"}) {
        const std::string path = Folder() + "\\" + file;
        if (!sys::Exists(path)) Problem("pinned install file missing: " + path);
    }
}

bool PinnedFile(const char* relative, char (&out)[260]) {
    if (!relative || (text::CompareIgnoringCase(relative, "config\\keyboard.ini") != 0 &&
                      text::CompareIgnoringCase(relative, "config\\addrbook.ini") != 0))
        return false;
    const char* install = starter::RebirthData() ? "rebirth" : "vanilla";
    if (std::snprintf(out, sizeof(out), "%s\\%s\\%s", Folder().c_str(), install, relative + 7) >= static_cast<int>(sizeof(out)))
        return false;
    static std::atomic<int> logged = 0;
    if (++logged <= 4) Log("harness: %s read from the pinned copy %s", relative, out);
    return true;
}

std::string Failure() {
    std::lock_guard<std::mutex> lock(Lock());
    std::string text;
    for (const std::string& p : S().problems) text += (text.empty() ? "settings: " : "; ") + p;
    return text;
}

} // namespace ei::verify::settings
