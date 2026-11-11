// The standalone's settings file and its typed accessors. See settings.h.
#include "game/app/settings.h"

#include "platform/log.h"
#include "lib/text_case.h"
#include "platform/platform.h"

#include <mutex>
#include <optional>
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

std::string FileInSettingsFolder() { return platform::DefaultSettingsFolder() + kFileName; }

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
    if (platform::ReadWholeFile(file, text, &error)) return Read::read;
    if (!platform::PathExists(file)) return Read::missing;
    ei::Log("settings: %s cannot be read (error %u)", file, static_cast<unsigned>(error));
    return Read::failed;
}

// The file written whole beside itself (its folder made when missing), then moved over it.
bool Write(Store& s) {
    std::uint32_t error = 0;
    if (!platform::WriteWholeFile(s.file, s.document.Text(), &error)) {
        ei::Log("settings: cannot write %s (error %u)", s.file, static_cast<unsigned>(error));
        return false;
    }
    s.dirty = false;
    return true;
}

// The machine the defaults are for: the harness's desktop, else the primary monitor's mode in pixels (whatever the
// process's DPI awareness). Without a size (no desktop): 1024x768, `known` false.
Machine CurrentMachine(const Store& s, bool* known) {
    Machine machine = {s.desktopWidth, s.desktopHeight};
    if (machine.desktopWidth <= 0 || machine.desktopHeight <= 0)
        if (const std::optional<Size> desktop = platform::PrimaryDesktopSize()) machine = {desktop->w, desktop->h};
    *known = machine.desktopWidth > 0 && machine.desktopHeight > 0;
    if (!*known) machine = {1024, 768};
    return machine;
}

// The defaults for the current machine; the machine they are for. Without a desktop size: for 1024x768, kept in
// memory only, so a start with a desktop writes its own.
Machine UseDefaults(Store& s) {
    bool known = false;
    const Machine machine = CurrentMachine(s, &known);
    if (!known) {
        ei::Log("settings: no desktop size: the defaults for 1024x768, not written this session");
        s.inMemory = true;
    }
    s.document = Defaults(machine);
    return machine;
}

// The first of the places that holds a file, read: the pinned file, else the one next to the exe, then the per-user
// one. With none, or when the file read holds no setting, the defaults, written to the first place that takes them.
// A file that exists but cannot be read is never written over: the defaults are kept in memory, and nothing is
// written this session.
void Load(Store& s) {
    if (s.loaded) return;
    s.loaded = true;
    if (s.prepare) s.prepare(s.file);
    std::vector<std::string> places;
    if (s.pinned) places.push_back(s.file);
    else {
        places.push_back(FileInSettingsFolder());
        if (std::string user = FileForUser(); !user.empty()) places.push_back(std::move(user));
    }
    for (std::size_t i = 0; i < places.size(); ++i) {
        const std::string& place = places[i];
        s.file = place;
        std::string text;
        const Read read = ReadWhole(place, &text);
        if (read == Read::missing) continue;
        if (read == Read::failed) {
            if (s.readOnly) return;
            const Machine machine = UseDefaults(s);
            s.inMemory = true;
            ei::Log("settings: %s left as it is: the defaults (full screen at %dx%d) are used and nothing is written "
                    "this session", place, machine.desktopWidth, machine.desktopHeight);
            return;
        }
        std::vector<std::string> problems;
        s.document.Parse(text, &problems);
        constexpr std::size_t kShown = 8;
        for (std::size_t p = 0; p < problems.size() && p < kShown; ++p)
            ei::Log("settings: %s: %s", place, problems[p]);
        if (problems.size() > kShown) ei::Log("settings: %s: %zu more lines not understood", place, problems.size() - kShown);
        if (s.document.Values() != 0 || s.readOnly) {
            ei::Log("settings: %s: %zu sections, %zu values%s", place, s.document.sections.size(),
                    s.document.Values(), s.readOnly ? " (pinned, read only)" : s.pinned ? " (pinned)" : "");
            return;
        }
        // No setting: the defaults are written over it (it is this place's file).
        ei::Log("settings: %s holds no setting: the defaults are written over it", place);
        places.erase(places.begin(), places.begin() + static_cast<std::ptrdiff_t>(i));
        break;
    }
    if (s.readOnly) {
        ei::Log("settings: the pinned file %s cannot be read", s.file);
        return;
    }
    const Machine machine = UseDefaults(s);
    if (s.inMemory) return;
    for (const std::string& place : places) {
        s.file = place;
        if (Write(s)) {
            ei::Log("settings: %s written with the defaults (full screen at %dx%d)", place, machine.desktopWidth,
                    machine.desktopHeight);
            return;
        }
    }
    s.inMemory = true;
    ei::Log("settings: no settings file can be written: the defaults (full screen at %dx%d) are kept in memory this "
            "session", machine.desktopWidth, machine.desktopHeight);
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
    ei::Log("settings: [%s] %s = %s is not %s: the default is used", section, name, text, type);
}

// The catalog row's default of `name` as text (VideoResolution's for the current machine); empty for a name without a
// row (logged).
std::string DefaultOf(const char* section, const char* name) {
    const Described* row = Describe(section, name);
    if (!row) {
        ei::Log("settings: [%s] %s has no catalog row: read as 0, false or empty", section, name);
        return {};
    }
    Machine machine;
    if (row->value.type == Default::Type::desktopSize) {
        Locked lock;
        bool known = false;
        machine = CurrentMachine(S(), &known);
    }
    return DefaultText(*row, machine);
}

// The value as its type: the file's, else the row's default read as that type.
template <typename T>
T Get(const char* section, const char* name, bool (*parse)(std::string_view, T*), const char* type) {
    T value{};
    std::string text;
    if (Text(section, name, &text)) {
        if (parse(text, &value)) return value;
        NotOfType(section, name, text, type);
    }
    parse(DefaultOf(section, name), &value);
    return value;
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

int GetInt(const char* section, const char* name) { return Get(section, name, ParseInt, "a number"); }
bool GetBool(const char* section, const char* name) { return Get(section, name, ParseBool, "true or false"); }
float GetFloat(const char* section, const char* name) { return Get(section, name, ParseFloat, "a number"); }

std::string GetString(const char* section, const char* name) {
    std::string text;
    return Text(section, name, &text) ? text : DefaultOf(section, name);
}

std::vector<std::string> GetValues(const char* section) {
    std::vector<std::string> values;
    for (Document::Line& line : GetLines(section)) values.push_back(std::move(line.value));
    return values;
}

std::vector<Document::Line> GetLines(const char* section) {
    Locked lock;
    Store& s = S();
    Load(s);
    std::vector<Document::Line> lines;
    for (const Document::Section& found : s.document.sections) {
        if (text::CompareIgnoringCase(found.name, section) != 0) continue;
        for (const Document::Line& line : found.lines)
            if (!line.name.empty()) lines.push_back(line);
    }
    return lines;
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

void Pin(std::string_view file, MissingHandler missing, bool readOnly, PrepareHandler prepare) {
    Locked lock;
    Store& s = S();
    s.document = Document{};
    s.file = std::string(file);
    s.pinned = true;
    s.readOnly = readOnly;
    s.loaded = false;
    s.inMemory = false;
    s.dirty = false;
    s.missing = missing;
    s.prepare = prepare;
}

} // namespace settings
