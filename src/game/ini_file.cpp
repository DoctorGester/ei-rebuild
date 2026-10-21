// CIniFile: .ini settings through GetPrivateProfile* / WritePrivateProfileString (ini_file.h).
#include "ini_file.h"
#include "game_strings.h"

#include "console.h"
#include "ei/hook.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "originals_library.h"
#include "lib/mfc_coll.h"

#include <cstring>
#include <new>

#pragma warning(disable : 4740) // naked forwarders

namespace {

const char* const kDoubleFormat = game::Text(0x0078DA68);  // "%g"
const char* const kIntFormat = game::Text(0x0078EEEC);     // "%d"
const char* const kUIntFormat = game::Text(0x0079614C);    // "%u"
const char* const kFloatFormat = game::Text(0x007842D4);   // "%f"
const char* const kLongFloatFormat = game::Text(0x00796150); // "%lf"
constexpr char kListSeparator = ',';

// The game's CRT conversions.

// MFC CString members not in lib/mfc.h, called on existing strings.
struct TextOps : mfc::CString {
    int Find(char c) const;                     // 00700E91 Find(TCHAR)
    int Find(char c, int start) const;          // 00700E9F Find(TCHAR, int)
    void TrimLeft();                            // 006FE216
    void TrimRight();                           // 006FE11F
    void AppendChar(char c);                    // 00700D73 operator+=(TCHAR)
};
TextOps& Ops(mfc::CString& s) { return static_cast<TextOps&>(s); }
// 006FD5D0: operator+(TCHAR, const CString&).

// MFC's afxcoll arrays (mfc::CArray layout), through their own code: CUIntArray's constructor,
// destructor and SetSize (006FF933 / 006FF966 / 006FF993), and the byte / word / uint SetSize.
using MfcDWords = mfc::impl::CUIntArray;
void SetSizeBytes(mfc::CArray<std::uint8_t>* a, int n) { a->SetSize(n, -1); }      // 006FF64A
void SetSizeWords(mfc::CArray<std::uint16_t>* a, int n) { a->SetSize(n, -1); }     // 006FEEFA
void SetSizeUInts(mfc::CArray<std::uint32_t>* a, int n) { a->SetSize(n, -1); }     // 006FF29A

// 004513A0: SetSize of an aligned int array (CArray<int> with the aligned allocator).

// WriteString takes its CString by value (the callee destroys it); passed here as its data pointer.

// A copy of `value` handed to WriteString (which destroys it).
BOOL WriteCopy(CIniFile* ini, const char* section, const char* key, const mfc::CString& value) {
    alignas(4) char copy[sizeof(mfc::CString)];
    new (copy) mfc::CString(value);
    return ini->WriteString(section, key, *reinterpret_cast<char**>(copy));
}

// The value of `key` as a CString (00696030).
mfc::CString ReadString(const CIniFile* ini, const char* section, const char* key, const char* defaultValue) {
    return const_cast<CIniFile*>(ini)->GetString(section, key, defaultValue);
}

// --- Aligned arrays -------------------------------------------------------------------------------

// The byte, short and int arrays are engine aligned arrays (lib::AlignedArray); their inlined
// SetSize allocates through 00695B00 / 00695B20 / 00695B40 / 00453A10.

// The list as integers in a temporary aligned int array (the original's 004513A0 + atoi loop).
// Returns 0 when the value is empty (the temporary then stays empty).
BOOL ReadIntTemp(CIniFile* ini, const char* section, const char* key, DwordArray& temp) {
    console::CStringArray codes;
    if (!ini->GetList(section, key, &codes, kListSeparator)) return 0;
    temp.SetSize(codes.GetSize(), -1);
    for (int i = 0; i < temp.size; ++i) temp.data[i] = static_cast<std::uint32_t>(libcrt::Atoi(codes[i]));
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
// `setSize`, one of the MFC SetSize forwarders).
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
            for (int i = 0; i < temp.size; ++i) temp.data[i] = static_cast<std::uint32_t>(libcrt::Atoi(codes[i]));
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

int TextOps::Find(char c) const { return reinterpret_cast<const mfc::CString*>(this)->Find(c); }
int TextOps::Find(char c, int start) const { return reinterpret_cast<const mfc::CString*>(this)->Find(c, start); }
void TextOps::TrimLeft() { reinterpret_cast<mfc::CString*>(this)->TrimLeft(); }
void TextOps::TrimRight() { reinterpret_cast<mfc::CString*>(this)->TrimRight(); }
void TextOps::AppendChar(char a0) { *reinterpret_cast<mfc::CString*>(this) += a0; }

// --- Construction ----------------------------------------------------------------------------------

CIniFile* CIniFile::Construct() {
    path_ = *reinterpret_cast<char* const*>(0x00796E48);   // afxEmptyString
    return this;
}
EI_REPLACE_METHOD(0x00695B80, CIniFile::Construct);

CIniFile* CIniFile::ConstructWithPath(const char* path) {
    path_ = *reinterpret_cast<char* const*>(0x00796E48);
    reinterpret_cast<mfc::CString&>(path_) = path;
    return this;
}
EI_REPLACE_METHOD(0x00695B90, CIniFile::ConstructWithPath);

BOOL CIniFile::SetPath(const char* path) {
    reinterpret_cast<mfc::CString&>(path_) = path;
    return 1;
}
EI_REPLACE_METHOD(0x00695BF0, CIniFile::SetPath);

void CIniFile::Destruct() { reinterpret_cast<mfc::CString&>(path_).~CString(); }
EI_REPLACE_METHOD(0x00695BE0, CIniFile::Destruct);

// --- Readers ---------------------------------------------------------------------------------------

BOOL CIniFile::GetKeyNames(const char* section, console::CStringArray* out) {
    mfc::CString item;
    char buffer[0x5000];
    gameapi::GetPrivateProfileSectionA(section, buffer, sizeof(buffer), path_);
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
EI_REPLACE_METHOD(0x00695D10, CIniFile::GetKeyNames);

unsigned CIniFile::GetInt(const char* section, const char* key, int defaultValue) {
    return gameapi::GetPrivateProfileIntA(section, key, defaultValue, path_);
}
EI_REPLACE_METHOD(0x00695F00, CIniFile::GetInt);

unsigned CIniFile::GetInt2(const char* section, const char* key, int defaultValue) {
    return gameapi::GetPrivateProfileIntA(section, key, defaultValue, path_);
}
EI_REPLACE_METHOD(0x00695F20, CIniFile::GetInt2);

double CIniFile::GetFloat(const char* section, const char* key, float defaultValue) {
    return GetDouble(section, key, defaultValue);
}
EI_REPLACE_METHOD(0x00695F60, CIniFile::GetFloat);

double CIniFile::GetDouble(const char* section, const char* key, double defaultValue) {
    mfc::CString text;
    text.Format(kDoubleFormat, defaultValue);
    mfc::CString value = ReadString(this, section, key, text);
    volatile double result = GameAtof(value);   // stored as a double, as the original does
    return result;
}
EI_REPLACE_METHOD(0x00695F80, CIniFile::GetDouble);

mfc::CString CIniFile::GetString(const char* section, const char* key, const char* defaultValue) {
    char buffer[0x400];
    std::memset(buffer, 0, 4);
    gameapi::GetPrivateProfileStringA(section, key, defaultValue, buffer, 0x3FF, path_);
    return mfc::CString(buffer);
}
EI_REPLACE_METHOD(0x00696030, CIniFile::GetString);

BOOL CIniFile::GetList(const char* section, const char* key, console::CStringArray* out, char separator) {
    mfc::CString value = ReadString(this, section, key, game::EmptyText());
    mfc::CString item;
    if (console::Length(value) == 0) return 0;
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
EI_REPLACE_METHOD(0x00696090, CIniFile::GetList);

BOOL CIniFile::GetIntArrayCopy(const char* section, const char* key, DwordArray* out) {
    return ReadAlignedVia<std::uint32_t>(this, section, key, out);
}
EI_REPLACE_METHOD(0x006961D0, CIniFile::GetIntArrayCopy);

BOOL CIniFile::GetByteArray(const char* section, const char* key, lib::AlignedArray<std::uint8_t>* out) {
    return ReadAlignedVia(this, section, key, out);
}
EI_REPLACE_METHOD(0x006963F0, CIniFile::GetByteArray);

BOOL CIniFile::GetShortArray(const char* section, const char* key, lib::AlignedArray<std::uint16_t>* out) {
    return ReadAlignedVia(this, section, key, out);
}
EI_REPLACE_METHOD(0x00696610, CIniFile::GetShortArray);

BOOL CIniFile::GetIntArray(const char* section, const char* key, DwordArray* out) {
    console::CStringArray codes;
    if (!GetList(section, key, &codes, kListSeparator)) return 0;
    int count = codes.GetSize();
    out->SetSize(count);
    for (int i = 0; i < count; ++i) out->data[i] = static_cast<std::uint32_t>(libcrt::Atoi(codes[i]));
    return 1;
}
EI_REPLACE_METHOD(0x00696830, CIniFile::GetIntArray);

BOOL CIniFile::GetMfcByteArray(const char* section, const char* key, mfc::CArray<std::uint8_t>* out) {
    return ReadMfcVia(this, section, key, out, &SetSizeBytes);
}
EI_REPLACE_METHOD(0x006969F0, CIniFile::GetMfcByteArray);

BOOL CIniFile::GetMfcWordArray(const char* section, const char* key, mfc::CArray<std::uint16_t>* out) {
    return ReadMfcVia(this, section, key, out, &SetSizeWords);
}
EI_REPLACE_METHOD(0x00696B20, CIniFile::GetMfcWordArray);

BOOL CIniFile::GetMfcUIntArray(const char* section, const char* key, mfc::CArray<std::uint32_t>* out) {
    return ReadMfcVia(this, section, key, out, &SetSizeUInts);
}
EI_REPLACE_METHOD(0x00696C50, CIniFile::GetMfcUIntArray);

BOOL CIniFile::GetMfcDWordArray(const char* section, const char* key, mfc::CArray<std::uint32_t>* out) {
    console::CStringArray codes;
    if (!GetList(section, key, &codes, kListSeparator)) return 0;
    reinterpret_cast<MfcDWords*>(out)->SetSize(codes.GetSize(), -1);
    for (int i = 0; i < codes.GetSize(); ++i)
        out->data[i] = static_cast<std::uint32_t>(libcrt::Atoi(codes[i]));
    return 1;
}
EI_REPLACE_METHOD(0x00696D80, CIniFile::GetMfcDWordArray);

// --- Writers ---------------------------------------------------------------------------------------

BOOL CIniFile::WriteString(const char* section, const char* key, mfc::CString value) {
    return gameapi::WritePrivateProfileStringA(section, key, value, path_);
}
EI_REPLACE_METHOD(0x00696E60, CIniFile::WriteString);

BOOL CIniFile::WriteIntForward(const char* section, const char* key, int value) {
    return WriteInt(section, key, value);
}
EI_REPLACE_METHOD(0x00696F30, CIniFile::WriteIntForward);

BOOL CIniFile::WriteInt(const char* section, const char* key, int value) {
    mfc::CString text;
    text.Format(kIntFormat, value);
    return WriteCopy(this, section, key, text);
}
EI_REPLACE_METHOD(0x00696F50, CIniFile::WriteInt);

BOOL CIniFile::WriteUInt(const char* section, const char* key, unsigned value) {
    mfc::CString text;
    text.Format(kUIntFormat, value);
    return WriteCopy(this, section, key, text);
}
EI_REPLACE_METHOD(0x00696FE0, CIniFile::WriteUInt);

BOOL CIniFile::WriteFloat(const char* section, const char* key, float value) {
    mfc::CString text;
    text.Format(kFloatFormat, static_cast<double>(value));
    return WriteCopy(this, section, key, text);
}
EI_REPLACE_METHOD(0x00697070, CIniFile::WriteFloat);

BOOL CIniFile::WriteDouble(const char* section, const char* key, double value) {
    mfc::CString text;
    text.Format(kLongFloatFormat, value);
    return WriteCopy(this, section, key, text);
}
EI_REPLACE_METHOD(0x00697100, CIniFile::WriteDouble);

BOOL CIniFile::WriteList(const char* section, const char* key, const console::CStringArray* items, char separator) {
    mfc::CString text;
    if (items->GetSize() == 0) return 0;
    text = (*items)[0];
    for (int i = 1; i < items->GetSize(); ++i) {
        mfc::CString part = mfc::operator+(separator, (*items)[i]);   // 006FD5D0
        text += part;
    }
    return WriteCopy(this, section, key, text);
}
EI_REPLACE_METHOD(0x00697190, CIniFile::WriteList);
