// IniFile: .ini settings through GetPrivateProfile* / WritePrivateProfileString.
#include "ini_file.h"
#include "game_strings.h"

#include "console.h"
#include "lib/crt.h"
#include "lib/str.h"
#include "platform/platform.h"

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
    for (std::string item : platform::IniSectionEntries(section, path.c_str())) {
        // One "key=value" entry.
        int equals = str::Find(item, '=');
        if (equals != -1) {
            item = str::Left(item, equals);
            str::TrimLeft(item);
            str::TrimRight(item);
        }
        names.push_back(item);
    }
    return names;
}

std::string IniFile::GetString(const char* section, const char* key, const char* defaultValue) {
    return platform::IniString(section, key, defaultValue, path.c_str());
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
