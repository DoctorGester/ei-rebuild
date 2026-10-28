// CIniFile: .ini settings through GetPrivateProfile* / WritePrivateProfileString.
#include "ini_file.h"
#include "game_strings.h"

#include "console.h"
#include "ei/hook.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc_coll.h"
#include "game_data.h"
#include "lib/mfc_string.h"
#include "lib/str.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string>

namespace {

const char* const kDoubleFormat = "%g";
const char* const kIntFormat = "%d";
const char* const kUIntFormat = "%u";
const char* const kFloatFormat = "%f";
const char* const kLongFloatFormat = "%lf";
constexpr char kListSeparator = ',';

// MFC CString members not in lib/mfc.h, called on existing strings.
struct TextOps : mfc::CString {
    int Find(char c) const;
    int Find(char c, int start) const;
    void TrimLeft();
    void TrimRight();
    void AppendChar(char c);
};
TextOps& Ops(mfc::CString& s) { return static_cast<TextOps&>(s); }

// MFC's afxcoll array of dwords, through its own code: the constructor, destructor and SetSize.
using MfcDWords = mfc::impl::CUIntArray;

mfc::CString ReadString(const CIniFile* ini, const char* section, const char* key, const char* defaultValue) {
    return const_cast<CIniFile*>(ini)->GetString(section, key, defaultValue);
}

// --- Aligned arrays -------------------------------------------------------------------------------

// The list as integers in a temporary aligned int array (an atoi loop).
// Returns 0 when the value is empty (the temporary then stays empty).
BOOL ReadIntTemp(CIniFile* ini, const char* section, const char* key, DwordArray& temp) {
    console::CStringArray codes;
    if (!ini->GetList(section, key, &codes, kListSeparator)) return 0;
    temp.SetSize(codes.GetSize(), -1);
    for (int i = 0; i < temp.size; ++i) temp.data[i] = static_cast<std::uint32_t>(std::atoi(codes[i]));
    return 1;
}

template <typename T> BOOL ReadAlignedVia(CIniFile* ini, const char* section, const char* key, lib::AlignedArray<T>* out) {
    DwordArray temp{nullptr, 0, 0, 0};
    BOOL ok = ReadIntTemp(ini, section, key, temp);
    if (ok) {
        int count = temp.size;
        out->SetSize(count);
        for (int i = 0; i < count; ++i) out->data[i] = static_cast<T>(temp.data[i]);
    }
    temp.FreeData();
    return ok;
}

// The list as integers in a temporary CDWordArray, then copied as T into `out`.
template <typename T>
BOOL ReadMfcVia(CIniFile* ini, const char* section, const char* key, mfc::CArray<T>* out) {
    MfcDWords temp;
    temp.Construct();
    BOOL ok;
    {
        console::CStringArray codes;
        ok = ini->GetList(section, key, &codes, kListSeparator);
        if (ok) {
            temp.SetSize(codes.GetSize(), -1);
            for (int i = 0; i < temp.size; ++i) temp.data[i] = static_cast<std::uint32_t>(std::atoi(codes[i]));
        }
    }
    if (ok) {
        out->SetSize(temp.size, -1);
        for (int i = 0; i < temp.size; ++i) out->data[i] = static_cast<T>(temp.data[i]);
    }
    temp.Destruct();
    return ok;
}

} // namespace

int TextOps::Find(char c) const { return mfc::CString::Find(c); }
int TextOps::Find(char c, int start) const { return mfc::CString::Find(c, start); }
void TextOps::TrimLeft() { mfc::CString::TrimLeft(); }
void TextOps::TrimRight() { mfc::CString::TrimRight(); }
void TextOps::AppendChar(char a0) { mfc::CString::operator+=(a0); }

// --- Construction ----------------------------------------------------------------------------------

CIniFile* CIniFile::Construct() {
    std::construct_at(&path_);
    return this;
}
EI_REWRITTEN(0x00695B80, CIniFile::Construct);

BOOL CIniFile::SetPath(const char* path) {
    path_ = path;
    return 1;
}
EI_REWRITTEN(0x00695BF0, CIniFile::SetPath);

void CIniFile::Destruct() { std::destroy_at(&path_); }
EI_REWRITTEN(0x00695BE0, CIniFile::Destruct);

// --- Readers ---------------------------------------------------------------------------------------

BOOL CIniFile::GetKeyNames(const char* section, console::CStringArray* out) {
    mfc::CString item;
    char buffer[0x5000];
    gameapi::GetPrivateProfileSectionA(section, buffer, sizeof(buffer), path_.c_str());
    out->SetSize(0, -1);
    if (buffer[0] == '\0') return 0;
    const char* start = buffer;
    for (int i = 0; i < static_cast<int>(sizeof(buffer)); ++i) {
        if (buffer[i] != '\0') continue;
        // One "key=value" entry ends here.
        item = start;
        int equals = Ops(item).Find('=');
        if (equals != -1) {
            item = item.Left(equals);
            Ops(item).TrimLeft();
            Ops(item).TrimRight();
        }
        out->SetAtGrow(out->GetSize(), item);
        start = buffer + i + 1;
        if (*start == '\0') return 1;   // the double terminator
    }
    return 0;
}
EI_REWRITTEN(0x00695D10, CIniFile::GetKeyNames);

mfc::CString CIniFile::GetString(const char* section, const char* key, const char* defaultValue) {
    char buffer[0x400];
    std::memset(buffer, 0, 4);
    gameapi::GetPrivateProfileStringA(section, key, defaultValue, buffer, 0x3FF, path_.c_str());
    return mfc::CString(buffer);
}
EI_REWRITTEN(0x00696030, CIniFile::GetString);

BOOL CIniFile::GetList(const char* section, const char* key, mfc::CStringArray* out, char separator) {
    mfc::CString value = ReadString(this, section, key, game::EmptyText());
    mfc::CString item;
    if (value.GetLength() == 0) return 0;
    Ops(value).AppendChar(separator);
    int start = 0;
    for (int end = Ops(value).Find(separator); end != -1; end = Ops(value).Find(separator, start)) {
        item = Ops(value).Mid(start, end - start);
        Ops(item).TrimLeft();
        Ops(item).TrimRight();
        out->SetAtGrow(out->GetSize(), item);
        start = end + 1;
    }
    return 1;
}
EI_REWRITTEN(0x00696090, CIniFile::GetList);

BOOL CIniFile::GetIntArray(const char* section, const char* key, DwordArray* out) {
    console::CStringArray codes;
    if (!GetList(section, key, &codes, kListSeparator)) return 0;
    int count = codes.GetSize();
    out->SetSize(count);
    for (int i = 0; i < count; ++i) out->data[i] = static_cast<std::uint32_t>(std::atoi(codes[i]));
    return 1;
}
EI_REWRITTEN(0x00696830, CIniFile::GetIntArray);

// --- Writers ---------------------------------------------------------------------------------------

