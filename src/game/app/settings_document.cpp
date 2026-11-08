// The settings file's text, its values' text, the catalog of what every setting does and the defaults (no file, no
// game): Document, the Parse / Text functions of the values, Describe, Defaults. See settings.h;
// tests/unit/settings_file.cpp checks them without the game.
#include "game/app/settings.h"

#include "lib/str.h"
#include "lib/text_case.h"

#include <array>
#include <cctype>
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
    std::string_view section;     // the lines' section; the lines below a section not understood have none (`known`)
    bool known = true;
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
                section = sectionName;
                known = true;
                AddSection(sectionName);
            } else {
                known = false;
                problems->push_back("line " + std::to_string(number) + " not understood (and the lines below it): " +
                                    std::string(line));
            }
            continue;
        }
        if (!known) continue;
        if (line.front() == ';' || line.front() == '#') {
            AddSection(section).lines.push_back(Line{{}, std::string(line)});
            continue;
        }
        const std::size_t equals = line.find('=');
        const std::string_view name = equals == std::string_view::npos ? std::string_view() : Trimmed(line.substr(0, equals));
        if (name.empty() || section.empty()) {
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
    const auto read = str::FloatFromChars(text, number);
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

// The catalog rows' defaults (settings_catalog.inc).
constexpr Default Int(int value) { return {Default::Type::number, value}; }
constexpr Default Bool(bool value) { return {Default::Type::flag, value ? 1 : 0}; }
constexpr Default Float(float value) { return {Default::Type::real, 0, value}; }
constexpr Default Text(const char* value) { return {Default::Type::text, 0, 0.0f, value}; }
constexpr Default DesktopSize() { return {Default::Type::desktopSize}; }
constexpr bool kWritten = true;
constexpr bool kLeftOut = false;

constexpr Described kCatalog[] = {
#define EI_SETTING(section, name, value, written, values, description) {section, name, value, written, values, description},
#include "game/app/settings_catalog.inc"
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

std::string DefaultText(const Described& row, const Machine& machine) {
    switch (row.value.type) {
    case Default::Type::number: return IntText(row.value.number);
    case Default::Type::flag: return BoolText(row.value.number != 0);
    case Default::Type::real: return FloatText(row.value.real);
    case Default::Type::text: return row.value.text;
    case Default::Type::desktopSize: return IntText(machine.desktopWidth) + "x" + IntText(machine.desktopHeight);
    }
    return {};
}

// ---- The defaults ----

namespace {

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

bool Numbered(const Described& row) { return std::string_view(row.name).find("<n>") != std::string_view::npos; }

// A numbered row's name for shortcut `number`.
std::string NameFor(const Described& row, int number) {
    std::string name = row.name;
    if (const std::size_t at = name.find("<n>"); at != std::string::npos) name.replace(at, 3, IntText(number));
    return name;
}

} // namespace

Document Defaults(const Machine& machine) {
    Document d;
    d.Comment("", "Evil Islands settings (ei-rebuild.exe). Each [section] holds name = value lines: numbers, true or");
    d.Comment("", "false, text as it is (in \"quotes\" when it starts or ends with a space). Lines starting with ; or #");
    d.Comment("", "are comments. The game rewrites this file when it saves its options; delete it to get the defaults.");
    d.Comment("", "The comment above a line says what it does; docs/settings.md lists every setting the game reads.");

    // Each written row with its description above it; a run of numbered rows (the camera shortcuts) shortcut by
    // shortcut, each row's description above its first line only.
    const Described* row = std::begin(kCatalog);
    while (row != std::end(kCatalog)) {
        const Described* end = row + 1;
        if (Numbered(*row))
            while (end != std::end(kCatalog) && Numbered(*end) && Same(end->section, row->section)) ++end;
        const int numbers = Numbered(*row) ? kCameraShortcuts : 1;
        for (int number = 0; number < numbers; ++number) {
            for (const Described* r = row; r != end; ++r) {
                if (!r->written) continue;
                if (number == 0)
                    for (const std::string& line : DescriptionLines(*r)) d.Comment(r->section, line);
                d.Set(r->section, NameFor(*r, number), DefaultText(*r, machine));
            }
        }
        row = end;
    }
    d.Comment(kAutoExec, "Console lines run before autoexec.cfg, one per line: 1 = <command>.");
    return d;
}

} // namespace settings
