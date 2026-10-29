// IniFile: .ini settings through GetPrivateProfile* / WritePrivateProfileString.
#include "ini_file.h"
#include "game_strings.h"

#include "console.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/str.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr char kListSeparator = ',';

std::string ReadString(const IniFile* ini, const char* section, const char* key, const char* defaultValue) {
    return const_cast<IniFile*>(ini)->GetString(section, key, defaultValue);
}

} // namespace

void IniFile::SetPath(std::string_view newPath) {
    path = newPath;
}

// --- Readers ---------------------------------------------------------------------------------------

std::vector<std::string> IniFile::GetKeyNames(const char* section) {
    std::vector<std::string> names;
    std::string item;
    char buffer[0x5000];
    gameapi::GetPrivateProfileSectionA(section, buffer, sizeof(buffer), path.c_str());
    if (buffer[0] == '\0') return names;
    const char* start = buffer;
    for (int i = 0; i < static_cast<int>(sizeof(buffer)); ++i) {
        if (buffer[i] != '\0') continue;
        // One "key=value" entry ends here.
        item = start;
        int equals = str::Find(item, '=');
        if (equals != -1) {
            item = str::Left(item, equals);
            str::TrimLeft(item);
            str::TrimRight(item);
        }
        names.push_back(item);
        start = buffer + i + 1;
        if (*start == '\0') break;   // the double terminator
    }
    return names;
}

std::string IniFile::GetString(const char* section, const char* key, const char* defaultValue) {
    char buffer[0x400];
    std::memset(buffer, 0, 4);
    gameapi::GetPrivateProfileStringA(section, key, defaultValue, buffer, 0x3FF, path.c_str());
    return std::string(buffer);
}

std::vector<std::string> IniFile::GetList(const char* section, const char* key, char separator) {
    std::string value = ReadString(this, section, key, game::kEmptyText);
    std::vector<std::string> items;
    std::string item;
    if (value.empty()) return items;
    value += separator;
    int start = 0;
    for (int end = str::Find(value, separator); end != -1; end = str::Find(value, separator, start)) {
        item = str::Mid(value, start, end - start);
        str::TrimLeft(item);
        str::TrimRight(item);
        items.push_back(item);
        start = end + 1;
    }
    return items;
}

std::vector<std::uint32_t> IniFile::GetIntArray(const char* section, const char* key) {
    std::vector<std::uint32_t> values;
    for (const std::string& code : GetList(section, key, kListSeparator)) values.push_back(static_cast<std::uint32_t>(str::ToInt(code)));
    return values;
}
