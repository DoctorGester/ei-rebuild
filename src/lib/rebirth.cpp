// Flags for Rebirth's patches (include/ei/rebirth.h).
#include "ei/rebirth.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace ei::rebirth {

namespace detail {
bool g_enabled[kPatchCount];   // zero-initialised: every patch off until LoadSettings
}

namespace {
const char* const kNames[] = {
#define EI_REBIRTH_PATCH(name, kind) #name,
#include "ei/rebirth_patches.inc"
#undef EI_REBIRTH_PATCH
};
static_assert(sizeof(kNames) / sizeof(kNames[0]) == kPatchCount);

const Kind kKinds[] = {
#define EI_REBIRTH_PATCH(name, kind) Kind::kind,
#include "ei/rebirth_patches.inc"
#undef EI_REBIRTH_PATCH
};
static_assert(sizeof(kKinds) / sizeof(kKinds[0]) == kPatchCount);

int g_pluginNPlayers = 7;
int g_pluginPause = 1;
char g_secondMaster[256];
char g_queued[1024];                         // QueueSpec runs before any C++ object may be used

void SetAll(bool on) {
    for (bool& e : detail::g_enabled) e = on;
}

void SetDefaults() {
    for (int i = 0; i < kPatchCount; ++i) detail::g_enabled[i] = kKinds[i] == Kind::Fix;
}

// ---- ei_rebirth.ini's [Rebirth], read as the system's profile calls read it (GetPrivateProfileInt / String) ----

struct Entry {
    std::string key;
    std::string value;
};

constexpr std::string_view kBlanks = " \t";

std::string_view Trimmed(std::string_view text) {
    const std::size_t first = text.find_first_not_of(kBlanks);
    if (first == std::string_view::npos) return {};
    return text.substr(first, text.find_last_not_of(kBlanks) - first + 1);
}

bool SameName(std::string_view a, std::string_view b) {
    return a.size() == b.size() && _strnicmp(a.data(), b.data(), a.size()) == 0;
}

// The file `path` (UTF-8, or ASCII) as a path.
std::filesystem::path FilePath(const char* path) {
    return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(path)));
}

// The key = value lines of the file's first section named `section` (case ignored, the name trimmed), keys and values
// trimmed of spaces and tabs; lines starting with ';' and lines without '=' left out. None when the file cannot be read.
std::vector<Entry> SectionEntries(const char* path, std::string_view section) {
    std::vector<Entry> entries;
    std::ifstream file(FilePath(path), std::ios::binary);
    if (!file) return entries;
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    enum class Place { before, inside, after } place = Place::before;
    for (std::size_t at = 0; at < text.size() && place != Place::after;) {
        std::size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        std::string_view line = Trimmed(std::string_view(text).substr(at, end - at));
        at = end + 1;
        if (!line.empty() && line.back() == '\r') line = Trimmed(line.substr(0, line.size() - 1));
        if (line.empty() || line.front() == ';') continue;
        if (line.front() == '[') {
            const std::size_t close = line.find(']');
            const std::string_view name = Trimmed(line.substr(1, close == std::string_view::npos ? line.npos : close - 1));
            if (place == Place::inside) place = Place::after;
            else if (SameName(name, section)) place = Place::inside;
            continue;
        }
        const std::size_t equals = line.find('=');
        if (place != Place::inside || equals == std::string_view::npos) continue;
        entries.push_back({std::string(Trimmed(line.substr(0, equals))), std::string(Trimmed(line.substr(equals + 1)))});
    }
    return entries;
}

// The value of `key`'s first line; null when there is none.
const std::string* Find(const std::vector<Entry>& entries, std::string_view key) {
    for (const Entry& entry : entries)
        if (SameName(entry.key, key)) return &entry.value;
    return nullptr;
}

// GetPrivateProfileString: the value, a pair of the same quotes around it taken off; empty when the key is not there.
std::string TextValue(const std::vector<Entry>& entries, std::string_view key) {
    const std::string* value = Find(entries, key);
    if (!value) return {};
    if (value->size() >= 2 && (value->front() == '"' || value->front() == '\'') && value->back() == value->front())
        return value->substr(1, value->size() - 2);
    return *value;
}

// GetPrivateProfileInt: TextValue's text read as an optional sign, then 0x and hex digits or decimal digits (what
// follows them ignored; none: 0), wrapped to 32 bits; `missing` when that text is empty (no key, no value).
int IntValue(const std::vector<Entry>& entries, std::string_view key, int missing) {
    const std::string text = TextValue(entries, key);
    if (text.empty()) return missing;
    std::string_view digits = text;
    const bool negative = digits.front() == '-';
    if (digits.front() == '-' || digits.front() == '+') digits.remove_prefix(1);
    std::uint32_t number = 0;
    if (digits.size() > 2 && digits[0] == '0' && digits[1] == 'x') {
        for (char c : digits.substr(2)) {
            const int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (digit < 0) break;
            number = number * 16 + static_cast<std::uint32_t>(digit);
        }
    } else {
        for (char c : digits) {
            if (c < '0' || c > '9') break;
            number = number * 10 + static_cast<std::uint32_t>(c - '0');
        }
    }
    return static_cast<int>(negative ? 0u - number : number);
}

// The environment variable `name`; empty when it is not set or too long.
std::string Environment(const char* name) {
    char value[1024];
    std::size_t size = 0;
    if (getenv_s(&size, value, sizeof(value), name) != 0 || size == 0) return {};
    return value;
}
} // namespace

void Set(Patch p, bool on) { detail::g_enabled[static_cast<int>(p)] = on; }

Kind KindOf(Patch p) { return kKinds[static_cast<int>(p)]; }

int PluginNPlayers() { return g_pluginNPlayers; }
void SetPluginNPlayers(int players) { g_pluginNPlayers = players; }

int PluginPause() { return g_pluginPause; }
void SetPluginPause(int mode) { g_pluginPause = mode; }

const char* PluginSecondMaster() { return g_secondMaster; }
void SetPluginSecondMaster(const char* address) { strncpy_s(g_secondMaster, address, _TRUNCATE); }

const char* Name(Patch p) { return kNames[static_cast<int>(p)]; }

bool FromName(const char* name, Patch* out) {
    for (int i = 0; i < kPatchCount; ++i) {
        if (_stricmp(kNames[i], name) == 0) {
            *out = static_cast<Patch>(i);
            return true;
        }
    }
    return false;
}

bool ApplySpec(const char* spec) {
    bool ok = true;
    const char* p = spec;
    while (*p) {
        while (*p == ',' || *p == ' ' || *p == ';' || *p == '\t') ++p;
        if (!*p) break;
        char token[96];
        std::size_t n = 0;
        while (*p && *p != ',' && *p != ' ' && *p != ';' && *p != '\t') {
            if (n + 1 < sizeof(token)) token[n++] = *p;
            ++p;
        }
        token[n] = 0;
        if (_strnicmp(token, "SecondMaster=", 13) == 0) {
            SetPluginSecondMaster(token + 13);
            continue;
        }
        bool on = true;
        if (char* eq = std::strchr(token, '=')) {
            on = eq[1] != '0';
            *eq = 0;
        }
        Patch patch;
        if (_stricmp(token, "all") == 0) SetAll(on);
        else if (_stricmp(token, "none") == 0) SetAll(!on);
        else if (_stricmp(token, "defaults") == 0) SetDefaults();
        else if (FromName(token, &patch)) Set(patch, on);
        else ok = false;
    }
    return ok;
}

void QueueSpec(const char* spec) {
    strcat_s(g_queued, spec);
    strcat_s(g_queued, ";");
}

void LoadSettings(const char* iniPath, const char* profile) {
    const std::vector<Entry> entries = SectionEntries(iniPath, "Rebirth");
    const int all = IntValue(entries, "Default", -1);
    if (all >= 0) SetAll(all != 0);
    else SetDefaults();
    std::error_code error;
    if (profile && !std::filesystem::exists(FilePath(iniPath), error)) ApplySpec(profile);
    for (int i = 0; i < kPatchCount; ++i) {
        const int value = IntValue(entries, kNames[i], -1);
        if (value >= 0) detail::g_enabled[i] = value != 0;
    }
    strncpy_s(g_secondMaster, TextValue(entries, "SecondMaster").c_str(), _TRUNCATE);
    const std::string environment = Environment("EI_REBIRTH");
    if (!environment.empty()) ApplySpec(environment.c_str());
    ApplySpec(g_queued);
}

} // namespace ei::rebirth
