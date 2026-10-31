// The settings file's text, its values' text, the catalog of what every setting does and the defaults (no file, no
// game): Document, the Parse / Text functions of the values, Describe, Defaults. See settings.h;
// tests/vectors/replay_settings.cpp checks them without the game.
#include "settings.h"

#include "lib/text_case.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

namespace settings {
namespace {

bool Same(std::string_view a, std::string_view b) {
    return a.size() == b.size() && text::CompareIgnoringCase(a.data(), b.data(), a.size()) == 0;
}

bool Space(char c) { return c == ' ' || c == '\t'; }

std::string_view Trimmed(std::string_view text) {
    while (!text.empty() && (Space(text.front()) || text.front() == '\r')) text.remove_prefix(1);
    while (!text.empty() && (Space(text.back()) || text.back() == '\r')) text.remove_suffix(1);
    return text;
}

// A section's name: letters, digits, spaces, '_' and '-'.
bool SectionName(std::string_view name) {
    if (name.empty()) return false;
    for (char c : name) {
        const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        if (!letter && c != ' ' && c != '_' && c != '-') return false;
    }
    return true;
}

// The value as a line holds it: in double quotes when it starts or ends with a space or starts with a quote; a line
// break (which no line can hold) as a space.
std::string ValueText(const std::string& value) {
    std::string out = value;
    for (char& c : out)
        if (c == '\r' || c == '\n') c = ' ';
    if (!out.empty() && (Space(out.front()) || Space(out.back()) || out.front() == '"')) out = '"' + out + '"';
    return out;
}

constexpr const char* kTrue[] = {"true", "yes", "on"};
constexpr const char* kFalse[] = {"false", "no", "off"};

} // namespace

// ---- The document ----

Document::Section* Document::FindSection(std::string_view name) {
    for (Section& section : sections)
        if (Same(section.name, name)) return &section;
    return nullptr;
}

Document::Section& Document::AddSection(std::string_view name) {
    if (Section* section = FindSection(name)) return *section;
    // The lines before the first section stay first.
    if (name.empty()) return *sections.insert(sections.begin(), Section{});
    sections.push_back(Section{std::string(name), {}});
    return sections.back();
}

const std::string* Document::Find(std::string_view section, std::string_view name) const {
    for (const Section& s : sections) {
        if (!Same(s.name, section)) continue;
        for (const Line& line : s.lines)
            if (!line.name.empty() && Same(line.name, name)) return &line.value;
    }
    return nullptr;
}

void Document::Set(std::string_view section, std::string_view name, std::string_view value) {
    Section& s = AddSection(section);
    for (Line& line : s.lines) {
        if (!line.name.empty() && Same(line.name, name)) {
            line.value = value;
            return;
        }
    }
    s.lines.push_back(Line{std::string(name), std::string(value)});
}

void Document::Comment(std::string_view section, std::string_view text) {
    AddSection(section).lines.push_back(Line{{}, "; " + std::string(text)});
}

std::size_t Document::Values() const {
    std::size_t count = 0;
    for (const Section& section : sections)
        for (const Line& line : section.lines) count += !line.name.empty();
    return count;
}

std::string Document::Text() const {
    std::string out;
    for (const Section& section : sections) {
        if (!section.name.empty()) {
            if (!out.empty()) out += "\r\n";
            out += "[" + section.name + "]\r\n";
        }
        for (const Line& line : section.lines) {
            if (line.name.empty()) out += line.value;
            else if (line.value.empty()) out += line.name + " =";
            else out += line.name + " = " + ValueText(line.value);
            out += "\r\n";
        }
    }
    return out;
}

void Document::Parse(std::string_view text, std::vector<std::string>* problems) {
    if (text.substr(0, 3) == "\xEF\xBB\xBF") text.remove_prefix(3);   // a UTF-8 BOM
    const char* section = "";     // the lines' section; null below a section not understood
    std::string sectionName;
    int number = 0;
    while (!text.empty()) {
        std::size_t end = text.find('\n');
        if (end == std::string_view::npos) end = text.size();
        const std::string_view line = Trimmed(text.substr(0, end));
        text.remove_prefix(end < text.size() ? end + 1 : end);
        ++number;
        if (line.empty()) continue;
        if (line.front() == '[') {
            const std::string_view name = Trimmed(line.substr(1, line.size() - 1 - (line.back() == ']')));
            if (line.back() == ']' && SectionName(name)) {
                sectionName = name;
                section = sectionName.c_str();
                AddSection(sectionName);
            } else {
                section = nullptr;
                problems->push_back("line " + std::to_string(number) + " not understood (and the lines below it): " +
                                    std::string(line));
            }
            continue;
        }
        if (!section) continue;
        if (line.front() == ';' || line.front() == '#') {
            AddSection(section).lines.push_back(Line{{}, std::string(line)});
            continue;
        }
        const std::size_t equals = line.find('=');
        const std::string_view name = equals == std::string_view::npos ? std::string_view() : Trimmed(line.substr(0, equals));
        if (name.empty() || !*section) {
            problems->push_back("line " + std::to_string(number) + " not understood: " + std::string(line));
            continue;
        }
        std::string_view value = Trimmed(line.substr(equals + 1));
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') value = value.substr(1, value.size() - 2);
        Set(section, name, value);
    }
}

// ---- The values' text ----

std::string IntText(int value) { return std::to_string(value); }

std::string FloatText(float value) {
    std::array<char, 32> text;
    const auto written = std::to_chars(text.data(), text.data() + text.size(), value);
    return std::string(text.data(), written.ptr);
}

std::string BoolText(bool value) { return value ? "true" : "false"; }

bool ParseInt(std::string_view text, int* value) {
    for (const char* word : kTrue)
        if (Same(text, word)) return *value = 1, true;
    for (const char* word : kFalse)
        if (Same(text, word)) return *value = 0, true;
    if (!text.empty() && text.front() == '+') text.remove_prefix(1);
    // Up to 4294967295: the registry's unsigned DWORDs read as the int of the same bits.
    std::int64_t number = 0;
    const auto read = std::from_chars(text.data(), text.data() + text.size(), number);
    if (text.empty() || read.ec != std::errc() || read.ptr != text.data() + text.size() || number < INT32_MIN ||
        number > UINT32_MAX)
        return false;
    *value = static_cast<int>(static_cast<std::uint32_t>(number));
    return true;
}

bool ParseFloat(std::string_view text, float* value) {
    if (!text.empty() && text.front() == '+') text.remove_prefix(1);
    float number = 0;
    const auto read = std::from_chars(text.data(), text.data() + text.size(), number);
    if (text.empty() || read.ec != std::errc() || read.ptr != text.data() + text.size()) return false;
    *value = number;
    return true;
}

bool ParseBool(std::string_view text, bool* value) {
    int number = 0;
    if (!ParseInt(text, &number)) return false;
    *value = number != 0;
    return true;
}

// ---- The catalog ----

namespace {

constexpr Described kCatalog[] = {
#define EI_SETTING(section, name, defaultText, values, description) {section, name, defaultText, values, description},
#include "settings_catalog.inc"
#undef EI_SETTING
};

char Lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

// A catalog name against a setting's: without case; "<n>" takes one or more digits, "<name>" the whole rest.
bool Matches(std::string_view pattern, std::string_view name) {
    std::size_t p = 0, n = 0;
    while (p < pattern.size()) {
        if (pattern.substr(p) == "<name>") return n < name.size();
        if (pattern.substr(p, 3) == "<n>") {
            const std::size_t start = n;
            while (n < name.size() && name[n] >= '0' && name[n] <= '9') ++n;
            if (n == start) return false;
            p += 3;
            continue;
        }
        if (n >= name.size() || Lower(pattern[p]) != Lower(name[n])) return false;
        ++p;
        ++n;
    }
    return n == name.size();
}

} // namespace

const Described* CatalogBegin() { return std::begin(kCatalog); }
const Described* CatalogEnd() { return std::end(kCatalog); }

const Described* Describe(std::string_view section, std::string_view name) {
    for (const Described& row : kCatalog)
        if (Same(row.section, section) && Matches(row.name, name)) return &row;
    return nullptr;
}

// ---- The defaults ----

namespace {

// What a game install writes and the game's own defaults lack (the original's installer and Rebirth's agree; the
// game's defaults are placeholders: a camera turning about 7 and pitching about 700 times as fast, objects lit half
// as bright). The camera's whole section:
struct InstallFloat {
    const char* name;
    float value;
};
constexpr InstallFloat kInstallCamera[] = {
    {"CameraMinHeight", 3.0f},
    {"CameraMaxHeight", 70.0f},
    {"CameraDefaultHeight", 2.0f},
    {"CameraDefaultXDeploy", 30.0f},
    {"CameraDefaultYDeploy", 30.0f},
    {"CameraMinDistanceToCarrier", 10.0f},
    {"CameraMaxDistanceToCarrier", 50.0f},
    {"CameraMinLimitDistanceToCarrier", 3.0f},
    {"CameraMaxLimitDistanceToCarrier", 70.0f},
    {"CameraDefaultDistanceToCarrier", 30.0f},
    {"NearClipDistance", 2.0f},
    {"FarClipDistance", 100.0f},
    {"CameraDefaultMinPitch", 30.0f},
    {"CameraDefaultMaxPitch", 60.0f},
    {"CameraBriefingMinDistanceToCarrier", 6.0f},
    {"CameraBriefingMaxDistanceToCarrier", 27.0f},
    {"CameraBriefingMinLimitDistanceToCarrier", 3.0f},
    {"CameraBriefingMaxLimitDistanceToCarrier", 30.0f},
    {"CameraBriefingMinPitch", 40.0f},
    {"CameraBriefingMaxPitch", 60.0f},
    {"CameraTerrainSensetiveArea", 10.0f},
    {"CameraDefaultScrollSpeed", 1.5f},
    {"CameraDefaultScrollPowerMouse", 1.0f},
    {"CameraDefaultScrollPowerKeyboard", 0.46f},
    {"CameraDefaultRotateSpeed", 0.15f},
    {"CameraDefaultRotatePowerMouse", 1.0f},
    {"CameraDefaultRotatePowerKeyboard", 0.46f},
    {"CameraDefaultPitchSpeed", 0.0015f},
    {"CameraDefaultPitchPowerMouse", 1.0f},
    {"CameraDefaultPitchPowerKeyboard", 0.46f},
    {"CameraDefaultZoomSpeed", 2.0f},
    {"CameraDefaultZoomPowerMouse", 1.0f},
    {"CameraDefaultZoomPowerKeyboard", 0.46f},
};
// Each of the four shortcuts: the default distance, looking along Z, 45 degrees of pitch.
constexpr InstallFloat kInstallShortcut[] = {{"AxisX", 0.0f}, {"AxisY", 0.0f}, {"AxisZ", 1.0f},
                                             {"Angle", 0.0f}, {"Distance", 30.0f}, {"Pitch", 45.0f}};
// The game settings that differ from the game's own: the selection type, the hero's and the quest light (R, G, B of
// 255, radius).
struct InstallInt {
    const char* name;
    int value;
};
constexpr InstallInt kInstallGameInts[] = {
    {"SelectionType", 1}, {"Hero Light R", 216},  {"Hero Light G", 216}, {"Hero Light B", 116},     {"Hero Light Radius", 5},
    {"Quest Light R", 120}, {"Quest Light G", 72}, {"Quest Light B", 120}, {"Quest Light Radius", 5},
};

// A setting's description as comment lines of at most kCommentWidth characters: the text, then its values in
// parentheses (left out for true / false).
constexpr std::size_t kCommentWidth = 108;
std::vector<std::string> DescriptionLines(const Described& row) {
    std::string text = row.description;
    if (std::string_view(row.values) != "true / false") text += std::string(" (") + row.values + ")";
    std::vector<std::string> lines;
    std::string line;
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t end = text.find(' ', at);
        if (end == std::string::npos) end = text.size();
        const std::string_view word(text.data() + at, end - at);
        if (!line.empty() && line.size() + 1 + word.size() > kCommentWidth) {
            lines.push_back(line);
            line.clear();
        }
        if (!line.empty()) line += ' ';
        line += word;
        at = end + 1;
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

} // namespace

Document Defaults(int width, int height) {
    Document d;
    // Each line with its description above it; a pattern's (the camera shortcuts') above its first line only.
    std::vector<const Described*> described;
    auto set = [&](const char* section, const std::string& name, const std::string& value) {
        const Described* row = Describe(section, name);
        if (row && std::find(described.begin(), described.end(), row) == described.end()) {
            described.push_back(row);
            for (const std::string& line : DescriptionLines(*row)) d.Comment(section, line);
        }
        d.Set(section, name, value);
    };
    d.Comment("", "Evil Islands settings (ei-rebuild.exe). Each [section] holds name = value lines: numbers, true or");
    d.Comment("", "false, text as it is (in \"quotes\" when it starts or ends with a space). Lines starting with ; or #");
    d.Comment("", "are comments. The game rewrites this file when it saves its options; delete it to get the defaults.");
    d.Comment("", "The comment above a line says what it does; docs/settings.md lists every setting the game reads.");

    set(kVideo, "VideoResolution", std::to_string(width) + "x" + std::to_string(height));
    set(kVideo, "FullScreen", BoolText(true));
    set(kVideo, "Video", IntText(2));             // the movies, stretched to the screen's width (the installers' value)
    set(kVideo, "VideoColorDepthInBits", IntText(32));
    set(kVideo, "Mipmapping", BoolText(true));
    set(kVideo, "Filtering", IntText(2));        // smoothed (1 is the same)
    set(kVideo, "TextureQuality", IntText(0));   // no mip level dropped (texture.cpp)

    set(kSound, "Audio", BoolText(true));
    for (const char* volume : {"AudioStreaming", "AudioSFX", "AudioSpeech"}) set(kSound, volume, IntText(100));

    set(kShadows, "ShadowQuality", IntText(2));
    for (const char* shadow : {"UnitShadow", "BuildingShadow", "FloraShadow"})
        set(kShadows, shadow, BoolText(true));

    // Rebirth's starter (EIStarter.exe 1.46) writes water waves on for its best landscape detail.
    for (const char* feature : {"EnableWaterWaves", "EnableSelfShadowing", "EnableFootprints", "EnableBloodprints",
                                "EnableFireprints"})
        set(kTerrain, feature, BoolText(true));

    // The install's camera and game settings.
    set(kCamera, "CameraFlipXAxis", BoolText(false));
    set(kCamera, "CameraFlipYAxis", BoolText(false));
    for (const InstallFloat& value : kInstallCamera) set(kCamera, value.name, FloatText(value.value));
    set(kCamera, "CameraBorderScrollArea", IntText(1));
    set(kCamera, "CameraFrameSelectionSensetiveArea", IntText(30));
    for (int shortcut = 0; shortcut < 4; ++shortcut)
        for (const InstallFloat& value : kInstallShortcut)
            set(kCamera, "CameraShortcut " + std::to_string(shortcut) + " " + value.name, FloatText(value.value));
    for (const InstallInt& value : kInstallGameInts) set(kGame, value.name, IntText(value.value));
    set(kGame, "ObjectsLightingCoeff", FloatText(2.0f));
    set(kGame, "DefaultMapName", "Zone1.mpr");
    set(kGame, "DefaultStreamingSound", "melody.mp3");
    set(kNetwork, "Master Server Name", "a3master.nival.com");

    // The renderer's (device_mesh.cpp).
    set(kRenderer, "VSync", BoolText(true));
    set(kRenderer, "Msaa", IntText(8));
    set(kRenderer, "Anisotropy", IntText(16));
    set(kRenderer, "PerfOverlay", BoolText(false));

    // Rebirth's patches (rebirth_hook.cpp): none named, each takes the game folder's profile.
    d.Comment(kPatches, "Rebirth's patches, name = true or false (docs/settings.md lists them). A patch not named here takes");
    d.Comment(kPatches, "the game folder's profile: Rebirth's in a Rebirth install, else none. NPlayers, Pause: EI_Plugin's");
    d.Comment(kPatches, "player count and pause mode. SecondMaster: a second master server, host[:port].");

    // Rebirth's switches (mod_folders.cpp).
    d.Comment(kRebirth, "Read with Rebirth's mod folder patches on ([patches]). AddonPath: the mod's folder.");
    d.Comment(kAutoExec, "Console lines run before autoexec.cfg, one per line: 1 = <command>.");
    return d;
}

} // namespace settings
