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

// MFC's afxcoll arrays (mfc::CArray layout), through their own code: CUIntArray's constructor,
// destructor and SetSize, and the byte / word / uint SetSize.
using MfcDWords = mfc::impl::CUIntArray;
void SetSizeBytes(mfc::CArray<std::uint8_t>* a, int n) { a->SetSize(n, -1); }
void SetSizeWords(mfc::CArray<std::uint16_t>* a, int n) { a->SetSize(n, -1); }
void SetSizeUInts(mfc::CArray<std::uint32_t>* a, int n) { a->SetSize(n, -1); }

// A copy of `value` handed to WriteString (which destroys it).
BOOL WriteCopy(CIniFile* ini, const char* section, const char* key, const std::string& value) {
    alignas(4) char copy[sizeof(mfc::CString)];
    new (copy) mfc::CString(value);
    return ini->WriteString(section, key, *reinterpret_cast<char**>(copy));
}

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

// The list as integers in a temporary CDWordArray, then copied as T into `out` (resized by
// `setSize`, one of the MFC SetSize instantiations).
template <typename T>
BOOL ReadMfcVia(CIniFile* ini, const char* section, const char* key, mfc::CArray<T>* out,
                void (*setSize)(mfc::CArray<T>*, int)) {
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
        setSize(out, temp.size);
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

CIniFile* CIniFile::ConstructWithPath(const char* path) {
    std::construct_at(&path_);
    path_ = path;
    return this;
}
EI_REWRITTEN(0x00695B90, CIniFile::ConstructWithPath);

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

unsigned CIniFile::GetInt(const char* section, const char* key, int defaultValue) {
    return gameapi::GetPrivateProfileIntA(section, key, defaultValue, path_.c_str());
}
EI_REWRITTEN(0x00695F00, CIniFile::GetInt);

unsigned CIniFile::GetInt2(const char* section, const char* key, int defaultValue) {
    return gameapi::GetPrivateProfileIntA(section, key, defaultValue, path_.c_str());
}
EI_REWRITTEN(0x00695F20, CIniFile::GetInt2);

double CIniFile::GetFloat(const char* section, const char* key, float defaultValue) {
    return GetDouble(section, key, defaultValue);
}
EI_REWRITTEN(0x00695F60, CIniFile::GetFloat);

double CIniFile::GetDouble(const char* section, const char* key, double defaultValue) {
    std::string text;
    text = str::Format(kDoubleFormat, defaultValue);
    std::string value = ReadString(this, section, key, text.c_str()).GetString();
    volatile double result = std::atof(value.c_str());   // stored as a double
    return result;
}
EI_REWRITTEN(0x00695F80, CIniFile::GetDouble);

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

BOOL CIniFile::GetIntArrayCopy(const char* section, const char* key, DwordArray* out) {
    return ReadAlignedVia<std::uint32_t>(this, section, key, out);
}
EI_REWRITTEN(0x006961D0, CIniFile::GetIntArrayCopy);

BOOL CIniFile::GetByteArray(const char* section, const char* key, lib::AlignedArray<std::uint8_t>* out) {
    return ReadAlignedVia(this, section, key, out);
}
EI_REWRITTEN(0x006963F0, CIniFile::GetByteArray);

BOOL CIniFile::GetShortArray(const char* section, const char* key, lib::AlignedArray<std::uint16_t>* out) {
    return ReadAlignedVia(this, section, key, out);
}
EI_REWRITTEN(0x00696610, CIniFile::GetShortArray);

BOOL CIniFile::GetIntArray(const char* section, const char* key, DwordArray* out) {
    console::CStringArray codes;
    if (!GetList(section, key, &codes, kListSeparator)) return 0;
    int count = codes.GetSize();
    out->SetSize(count);
    for (int i = 0; i < count; ++i) out->data[i] = static_cast<std::uint32_t>(std::atoi(codes[i]));
    return 1;
}
EI_REWRITTEN(0x00696830, CIniFile::GetIntArray);

BOOL CIniFile::GetMfcByteArray(const char* section, const char* key, mfc::CArray<std::uint8_t>* out) {
    return ReadMfcVia(this, section, key, out, &SetSizeBytes);
}
EI_REWRITTEN(0x006969F0, CIniFile::GetMfcByteArray);

BOOL CIniFile::GetMfcWordArray(const char* section, const char* key, mfc::CArray<std::uint16_t>* out) {
    return ReadMfcVia(this, section, key, out, &SetSizeWords);
}
EI_REWRITTEN(0x00696B20, CIniFile::GetMfcWordArray);

BOOL CIniFile::GetMfcUIntArray(const char* section, const char* key, mfc::CArray<std::uint32_t>* out) {
    return ReadMfcVia(this, section, key, out, &SetSizeUInts);
}
EI_REWRITTEN(0x00696C50, CIniFile::GetMfcUIntArray);

BOOL CIniFile::GetMfcDWordArray(const char* section, const char* key, mfc::CArray<std::uint32_t>* out) {
    console::CStringArray codes;
    if (!GetList(section, key, &codes, kListSeparator)) return 0;
    reinterpret_cast<MfcDWords*>(out)->SetSize(codes.GetSize(), -1);
    for (int i = 0; i < codes.GetSize(); ++i)
        out->data[i] = static_cast<std::uint32_t>(std::atoi(codes[i]));
    return 1;
}
EI_REWRITTEN(0x00696D80, CIniFile::GetMfcDWordArray);

// --- Writers ---------------------------------------------------------------------------------------

BOOL CIniFile::WriteString(const char* section, const char* key, mfc::CString value) {
    return gameapi::WritePrivateProfileStringA(section, key, value, path_.c_str());
}
EI_REWRITTEN(0x00696E60, CIniFile::WriteString);

BOOL CIniFile::WriteIntForward(const char* section, const char* key, int value) {
    return WriteInt(section, key, value);
}
EI_REWRITTEN(0x00696F30, CIniFile::WriteIntForward);

BOOL CIniFile::WriteInt(const char* section, const char* key, int value) {
    std::string text;
    text = str::Format(kIntFormat, value);
    return WriteCopy(this, section, key, text);
}
EI_REWRITTEN(0x00696F50, CIniFile::WriteInt);

BOOL CIniFile::WriteUInt(const char* section, const char* key, unsigned value) {
    std::string text;
    text = str::Format(kUIntFormat, value);
    return WriteCopy(this, section, key, text);
}
EI_REWRITTEN(0x00696FE0, CIniFile::WriteUInt);

BOOL CIniFile::WriteFloat(const char* section, const char* key, float value) {
    std::string text;
    text = str::Format(kFloatFormat, static_cast<double>(value));
    return WriteCopy(this, section, key, text);
}
EI_REWRITTEN(0x00697070, CIniFile::WriteFloat);

BOOL CIniFile::WriteDouble(const char* section, const char* key, double value) {
    std::string text;
    text = str::Format(kLongFloatFormat, value);
    return WriteCopy(this, section, key, text);
}
EI_REWRITTEN(0x00697100, CIniFile::WriteDouble);

BOOL CIniFile::WriteList(const char* section, const char* key, const console::CStringArray* items, char separator) {
    std::string text;
    if (items->GetSize() == 0) return 0;
    text = (*items)[0];
    for (int i = 1; i < items->GetSize(); ++i) {
        std::string part = (mfc::operator+(separator, (*items)[i])).GetString();
        text += part;
    }
    return WriteCopy(this, section, key, text);
}
EI_REWRITTEN(0x00697190, CIniFile::WriteList);
