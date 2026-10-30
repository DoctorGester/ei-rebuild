// The standalone's settings file and its typed accessors. See settings.h.
#include "settings.h"

#include "ei/hook.h"
#include "hook/log.h"
#include "platform/platform.h"

#include <mutex>
#include <string>

namespace settings {
namespace {

struct Store {
    Document document;
    std::string file;             // empty until loaded: the one next to the exe or the per-user one, or the pinned one
    bool loaded = false;
    bool pinned = false;          // a harness run's file
    bool readOnly = false;        // pinned and read only: nothing written, no defaults
    bool inMemory = false;        // the file could not be read or made: the defaults, nothing written this session
    bool dirty = false;
    int desktopWidth = 0;         // the harness's desktop for the defaults (0: the primary monitor's)
    int desktopHeight = 0;
    MissingHandler missing = nullptr;
    PrepareHandler prepare = nullptr;   // the harness's writer of the pinned file
    std::vector<ReadFilter> filters;
};
Store& S() {
    static Store* store = new Store;   // never destroyed: the game may still read at exit
    return *store;
}
std::mutex g_lock;

struct Locked {
    std::lock_guard<std::mutex> guard{g_lock};
};

std::string FileNextToExe() {
    std::string path = platform::ProgramPath();
    path.resize(path.find_last_of('\\') + 1);
    return path + kFileName;
}

// %LOCALAPPDATA%\EvilIslands\ei_settings.ini (empty without LOCALAPPDATA).
std::string FileForUser() {
    std::string folder = platform::UserDataFolder();
    if (folder.empty()) return {};
    return folder + "EvilIslands\\" + kFileName;
}

enum class Read { read, missing, failed };

// The whole file, shared with any writer (an editor or a sync tool holding it open). `missing` only when the file
// or its folder does not exist.
Read ReadWhole(const std::string& file, std::string* text) {
    std::uint32_t error = 0;
    switch (platform::ReadWholeFile(file, text, &error)) {
    case platform::WholeFile::read: return Read::read;
    case platform::WholeFile::missing: return Read::missing;
    case platform::WholeFile::failed: break;
    }
    ei::Log("settings: %s cannot be read (error %lu)", file.c_str(), error);
    return Read::failed;
}

// The file written whole beside itself (its folder made when missing), then moved over it.
bool Write(Store& s) {
    std::uint32_t error = 0;
    if (!platform::WriteWholeFile(s.file, s.document.Text(), &error)) {
        ei::Log("settings: cannot write %s (error %lu)", s.file.c_str(), error);
        return false;
    }
    s.dirty = false;
    return true;
}

// The defaults for the primary monitor's mode in pixels (whatever the process's DPI awareness), or the harness's
// desktop. Without a size (no desktop): for 1024x768, kept in memory only, so a start with a desktop writes its own.
void UseDefaults(Store& s, int* width, int* height) {
    *width = s.desktopWidth;
    *height = s.desktopHeight;
    if (*width <= 0 || *height <= 0) platform::PrimaryDesktopSize(width, height);
    if (*width <= 0 || *height <= 0) {
        ei::Log("settings: no desktop size (%dx%d): the defaults for 1024x768, not written this session", *width, *height);
        *width = 1024;
        *height = 768;
        s.inMemory = true;
    }
    s.document = Defaults(*width, *height);
}

// The first of the places that holds a file, read: the pinned file, else the one next to the exe, then the per-user
// one. With none, or when the file read holds no setting, the defaults, written to the first place that takes them.
// A file that exists but cannot be read is never written over: the defaults are kept in memory, and nothing is
// written this session.
void Load(Store& s) {
    if (s.loaded) return;
    s.loaded = true;
    if (s.prepare) s.prepare(s.file.c_str());
    std::vector<std::string> places;
    if (s.pinned) places.push_back(s.file);
    else {
        places.push_back(FileNextToExe());
        if (std::string user = FileForUser(); !user.empty()) places.push_back(std::move(user));
    }
    int width = 0, height = 0;
    for (std::size_t i = 0; i < places.size(); ++i) {
        const std::string& place = places[i];
        s.file = place;
        std::string text;
        const Read read = ReadWhole(place, &text);
        if (read == Read::missing) continue;
        if (read == Read::failed) {
            if (s.readOnly) return;
            UseDefaults(s, &width, &height);
            s.inMemory = true;
            ei::Log("settings: %s left as it is: the defaults (full screen at %dx%d) are used and nothing is written "
                    "this session", place.c_str(), width, height);
            return;
        }
        std::vector<std::string> problems;
        s.document.Parse(text, &problems);
        constexpr std::size_t kShown = 8;
        for (std::size_t p = 0; p < problems.size() && p < kShown; ++p)
            ei::Log("settings: %s: %s", place.c_str(), problems[p].c_str());
        if (problems.size() > kShown) ei::Log("settings: %s: %zu more lines not understood", place.c_str(), problems.size() - kShown);
        if (s.document.Values() != 0 || s.readOnly) {
            ei::Log("settings: %s: %zu sections, %zu values%s", place.c_str(), s.document.sections.size(),
                    s.document.Values(), s.readOnly ? " (pinned, read only)" : s.pinned ? " (pinned)" : "");
            return;
        }
        // No setting: the defaults are written over it (it is this place's file).
        ei::Log("settings: %s holds no setting: the defaults are written over it", place.c_str());
        places.erase(places.begin(), places.begin() + static_cast<std::ptrdiff_t>(i));
        break;
    }
    if (s.readOnly) {
        ei::Log("settings: the pinned file %s cannot be read", s.file.c_str());
        return;
    }
    UseDefaults(s, &width, &height);
    if (s.inMemory) return;
    for (const std::string& place : places) {
        s.file = place;
        if (Write(s)) {
            ei::Log("settings: %s written with the defaults (full screen at %dx%d)", place.c_str(), width, height);
            return;
        }
    }
    s.inMemory = true;
    ei::Log("settings: no settings file can be written: the defaults (full screen at %dx%d) are kept in memory this "
            "session", width, height);
}

// The value's text after the read filters, or false (reported to the harness) when it is missing.
bool Text(const char* section, const char* name, std::string* text) {
    Locked lock;
    Store& s = S();
    Load(s);
    const std::string* value = s.document.Find(section, name);
    if (!value) {
        if (s.missing) s.missing(section, name);
        return false;
    }
    *text = *value;
    for (ReadFilter filter : s.filters) filter(name, *text);
    return true;
}

// A value that is not of its type: logged, the default taken.
void NotOfType(const char* section, const char* name, const std::string& text, const char* type) {
    ei::Log("settings: [%s] %s = %s is not %s: the default is used", section, name, text.c_str(), type);
}

void SetText(const char* section, const char* name, const std::string& value) {
    Locked lock;
    Store& s = S();
    Load(s);
    if (s.readOnly) return;   // a harness run writes no settings
    if (const std::string* old = s.document.Find(section, name); old && *old == value) return;
    s.document.Set(section, name, value);
    s.dirty = true;
}

} // namespace

int GetInt(const char* section, const char* name, int defaultValue) {
    std::string text;
    int value = defaultValue;
    if (Text(section, name, &text) && !ParseInt(text, &value)) {
        NotOfType(section, name, text, "a number");
        value = defaultValue;
    }
    return value;
}

bool GetBool(const char* section, const char* name, bool defaultValue) {
    std::string text;
    bool value = defaultValue;
    if (Text(section, name, &text) && !ParseBool(text, &value)) {
        NotOfType(section, name, text, "true or false");
        value = defaultValue;
    }
    return value;
}

float GetFloat(const char* section, const char* name, float defaultValue) {
    std::string text;
    float value = defaultValue;
    if (Text(section, name, &text) && !ParseFloat(text, &value)) {
        NotOfType(section, name, text, "a number");
        value = defaultValue;
    }
    return value;
}

std::string GetString(const char* section, const char* name, const std::string& defaultValue) {
    std::string text;
    return Text(section, name, &text) ? text : defaultValue;
}

std::vector<std::string> GetValues(const char* section) {
    Locked lock;
    Store& s = S();
    Load(s);
    std::vector<std::string> values;
    for (const Document::Section& found : s.document.sections) {
        if (_stricmp(found.name.c_str(), section) != 0) continue;
        for (const Document::Line& line : found.lines)
            if (!line.name.empty()) values.push_back(line.value);
    }
    return values;
}

void SetInt(const char* section, const char* name, int value) { SetText(section, name, IntText(value)); }
void SetBool(const char* section, const char* name, bool value) { SetText(section, name, BoolText(value)); }
void SetFloat(const char* section, const char* name, float value) { SetText(section, name, FloatText(value)); }
void SetString(const char* section, const char* name, const std::string& value) { SetText(section, name, value); }

void Save() {
    Locked lock;
    Store& s = S();
    if (s.loaded && !s.readOnly && !s.inMemory && s.dirty) Write(s);
}

void AddReadFilter(ReadFilter filter) {
    Locked lock;
    S().filters.push_back(filter);
}

void SetDesktopSize(int width, int height) {
    Locked lock;
    S().desktopWidth = width;
    S().desktopHeight = height;
}

void Pin(const char* file, MissingHandler missing, bool readOnly, PrepareHandler prepare) {
    Locked lock;
    Store& s = S();
    s.document = Document{};
    s.file = file;
    s.pinned = true;
    s.readOnly = readOnly;
    s.loaded = false;
    s.inMemory = false;
    s.dirty = false;
    s.missing = missing;
    s.prepare = prepare;
}

} // namespace settings
