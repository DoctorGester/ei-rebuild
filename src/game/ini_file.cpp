// CIniFile: .ini settings through GetPrivateProfile* / WritePrivateProfileString.
#include "ini_file.h"
#include "game_strings.h"

#include "console.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/str.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>

namespace {

constexpr char kListSeparator = ',';

std::string ReadString(const CIniFile* ini, const char* section, const char* key, const char* defaultValue) {
    return const_cast<CIniFile*>(ini)->GetString(section, key, defaultValue);
}

} // namespace

// --- Construction ----------------------------------------------------------------------------------

CIniFile* CIniFile::Construct() {
    std::construct_at(&path_);
    return this;
}

BOOL CIniFile::SetPath(const char* path) {
    path_ = path;
    return 1;
}

void CIniFile::Destruct() { std::destroy_at(&path_); }

// --- Readers ---------------------------------------------------------------------------------------

BOOL CIniFile::GetKeyNames(const char* section, std::vector<std::string>* out) {
    std::string item;
    char buffer[0x5000];
    gameapi::GetPrivateProfileSectionA(section, buffer, sizeof(buffer), path_.c_str());
    out->clear();
    if (buffer[0] == '\0') return 0;
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
        out->push_back(item);
        start = buffer + i + 1;
        if (*start == '\0') return 1;   // the double terminator
    }
    return 0;
}

std::string CIniFile::GetString(const char* section, const char* key, const char* defaultValue) {
    char buffer[0x400];
    std::memset(buffer, 0, 4);
    gameapi::GetPrivateProfileStringA(section, key, defaultValue, buffer, 0x3FF, path_.c_str());
    return std::string(buffer);
}

BOOL CIniFile::GetList(const char* section, const char* key, std::vector<std::string>* out, char separator) {
    std::string value = ReadString(this, section, key, game::kEmptyText);
    std::string item;
    if (value.empty()) return 0;
    value += separator;
    int start = 0;
    for (int end = str::Find(value, separator); end != -1; end = str::Find(value, separator, start)) {
        item = str::Mid(value, start, end - start);
        str::TrimLeft(item);
        str::TrimRight(item);
        out->push_back(item);
        start = end + 1;
    }
    return 1;
}

BOOL CIniFile::GetIntArray(const char* section, const char* key, DwordArray* out) {
    std::vector<std::string> codes;
    if (!GetList(section, key, &codes, kListSeparator)) return 0;
    int count = static_cast<int>(codes.size());
    out->resize(count);
    for (int i = 0; i < count; ++i) (*out)[i] = static_cast<std::uint32_t>(str::ToInt(codes[i]));
    return 1;
}
