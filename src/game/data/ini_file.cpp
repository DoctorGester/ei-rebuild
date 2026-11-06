// IniFile: the game's .ini files, read whole and parsed here (ini_file.h).
#include "game/data/ini_file.h"

#include "game/core/asset_text.h"
#include "lib/str.h"
#include "lib/text_case.h"
#include "platform/platform.h"

#include <cstdlib>
#include <string>
#include <vector>

namespace {

constexpr char kListSeparator = ',';

// `text` without the spaces, tabs and carriage returns at either end.
std::string_view Trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) text.remove_suffix(1);
    return text;
}

} // namespace

void IniFile::Load(std::string_view path) {
    std::string bytes;
    platform::ReadWholeFile(std::string(path), &bytes);   // a missing file leaves it empty
    text = text::FromAssetText(bytes);
}

std::vector<std::string_view> IniFile::Entries(std::string_view section) const {
    std::vector<std::string_view> entries;
    bool active = false;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        const std::string_view line = Trim(std::string_view(text).substr(start, end - start));
        start = end + 1;
        if (line.empty() || line.front() == ';') continue;
        if (line.front() == '[') {
            const std::size_t close = line.find(']');
            active = close != std::string_view::npos && text::CompareIgnoringCase(line.substr(1, close - 1), section) == 0;
        } else if (active && line.find('=') != std::string_view::npos) {
            entries.push_back(line);
        }
    }
    return entries;
}

std::vector<std::string> IniFile::GetKeyNames(const char* section) const {
    std::vector<std::string> names;
    for (std::string_view entry : Entries(section)) names.emplace_back(Trim(entry.substr(0, entry.find('='))));
    return names;
}

std::string IniFile::GetString(const char* section, const char* key, const char* defaultValue) const {
    for (std::string_view entry : Entries(section)) {
        const std::size_t equals = entry.find('=');
        if (text::CompareIgnoringCase(Trim(entry.substr(0, equals)), key) != 0) continue;
        std::string_view value = Trim(entry.substr(equals + 1));
        if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') && value.front() == value.back())
            value = value.substr(1, value.size() - 2);
        return std::string(value);
    }
    return defaultValue;
}

int IniFile::GetInt(const char* section, const char* key, int defaultValue) const {
    const std::string value = GetString(section, key, "");
    char* end = nullptr;
    const long number = std::strtol(value.c_str(), &end, 10);
    return end == value.c_str() ? defaultValue : static_cast<int>(number);
}

std::vector<std::string> IniFile::GetList(const char* section, const char* key, char separator) const {
    const std::string value = GetString(section, key, "");
    std::vector<std::string> items;
    if (value.empty()) return items;
    for (std::size_t start = 0;;) {
        const std::size_t end = value.find(separator, start);
        std::string_view item = std::string_view(value).substr(start, end == std::string::npos ? end : end - start);
        while (!item.empty() && text::IsSpace(static_cast<unsigned char>(item.front()))) item.remove_prefix(1);
        while (!item.empty() && text::IsSpace(static_cast<unsigned char>(item.back()))) item.remove_suffix(1);
        items.emplace_back(item);
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return items;
}

std::vector<std::uint32_t> IniFile::GetIntArray(const char* section, const char* key) const {
    std::vector<std::uint32_t> values;
    for (const std::string& code : GetList(section, key, kListSeparator))
        values.push_back(static_cast<std::uint32_t>(str::ToInt(code)));
    return values;
}
