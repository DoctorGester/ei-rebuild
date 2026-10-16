// CFileRegistry: lookups in the game's binary registry files (see registry.h for the format).
#include "registry.h"
#include "game_strings.h"

#include "console.h"
#include "ei/hook.h"
#include "lib/crt.h"
#include "originals_library.h"

#include <cstring>

#pragma warning(disable : 4740) // naked forwarders

namespace {

const char* const kNotRegistry = game::Text(0x0079600C);   // "CFileRegistry::IsValid().\nThat is not registry file.\n"
const char* const kWrongType = game::Text(0x00796044);     // "CFileRegistry::FindData().\nWrong type of entry.\n"

// 006FD4DE CString::CString(LPCTSTR, int) on raw storage.
void __fastcall ConstructString(void* a0, void*, const char* a2, int a3) { new (a0) mfc::CString(a2, a3); }
// _ftol (006E853C) of a float: FLD, then the game's _ftol (truncates).
int FloatToInt(const float* value) { return static_cast<int>(gamecrt::Ftol(*value)); }

std::uint16_t U16(const std::uint8_t* p) {
    std::uint16_t v;
    std::memcpy(&v, p, 2);
    return v;
}
std::uint32_t U32(const std::uint8_t* p) {
    std::uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

// The home slot of `name` in a table of `slots` entries.
std::uint32_t HomeSlot(const char* name, std::uint16_t slots) {
    std::uint32_t sum = 0;
    for (const char* p = name; *p; ++p) sum += static_cast<std::uint32_t>(gamecrt::tolower(*p));
    return sum % slots;
}

// A CString made by CString(LPCTSTR, int) (the original builds it in place).
struct RawString {
    char* data;
    mfc::CString& Get() { return *reinterpret_cast<mfc::CString*>(this); }
};
RawString MakeString(const char* text, int length) {
    RawString s;
    ConstructString(&s, nullptr, text, length);
    return s;
}

// GetIntArray / GetFloatArray: the values are copied as dwords.
BOOL GetDwordArray(CFileRegistry* registry, const char* section, const char* key, unsigned type, DwordArray* out) {
    int isScalar;
    const std::uint8_t* value = registry->FindData(section, key, type, &isScalar);
    if (!value) {
        out->RemoveAll();
        return 0;
    }
    int count;
    if (isScalar) {
        count = 1;
    } else {
        count = U16(value);
        value += 2;
        if (count == 0) {
            out->RemoveAll();
            return 1;
        }
    }
    out->SetSize(count);
    for (int i = 0; i < count; ++i) out->data[i] = U32(value + 4 * i);
    return 1;
}

} // namespace

// --- Lifetime ------------------------------------------------------------------------------------

CFileRegistry* CFileRegistry::Construct() {
    file_.Construct();
    attached_ = 0;
    data_ = nullptr;
    return this;
}
EI_REPLACE_METHOD(0x00694520, CFileRegistry::Construct);

void CFileRegistry::Destroy() {
    if (!attached_) file_.Close();   // 00675720
    attached_ = 0;
    data_ = nullptr;
    file_.Close();                   // 006756D0, the mapped file's destructor
}
EI_REPLACE_METHOD(0x00694540, CFileRegistry::Destroy);

BOOL CFileRegistry::Open(const char* path) {
    if (!file_.Open(path, 0)) return 0;
    attached_ = 0;
    data_ = static_cast<const std::uint8_t*>(file_.view_);
    if (U32(data_) == kRegistryMagic) return 1;
    game::DebugLog(kNotRegistry);
    if (!attached_) file_.Close();
    attached_ = 0;
    data_ = nullptr;
    return 0;
}
EI_REPLACE_METHOD(0x006945A0, CFileRegistry::Open);

BOOL CFileRegistry::Attach(const void* image) {
    attached_ = 1;
    data_ = static_cast<const std::uint8_t*>(image);
    if (U32(data_) == kRegistryMagic) return 1;
    game::DebugLog(kNotRegistry);
    if (!attached_) file_.Close();
    attached_ = 0;
    data_ = nullptr;
    return 0;
}
EI_REPLACE_METHOD(0x00694610, CFileRegistry::Attach);

BOOL CFileRegistry::IsValid() {
    if (U32(data_) == kRegistryMagic) return 1;
    game::DebugLog(kNotRegistry);
    Close();
    return 0;
}
EI_REPLACE_METHOD(0x00694660, CFileRegistry::IsValid);

void CFileRegistry::Close() {
    if (!attached_) file_.Close();
    attached_ = 0;
    data_ = nullptr;
}
EI_REPLACE_METHOD(0x006946B0, CFileRegistry::Close);

// --- Lookups -------------------------------------------------------------------------------------

const std::uint8_t* CFileRegistry::FindData(const char* section, const char* key, unsigned type, int* isScalar) {
    const std::uint8_t* root = data_;
    if (!root) return nullptr;

    // The section: chained 6-byte slots after the u16 count at +4.
    std::uint32_t slot = HomeSlot(section, U16(root + 4));
    std::uint32_t sectionLength = static_cast<std::uint32_t>(std::strlen(section));
    const std::uint8_t* record;
    for (;;) {
        const std::uint8_t* entry = root + 6 + 6 * (slot & 0xFFFF);
        record = data_ + U32(entry + 2);
        if (U16(record + 2) == static_cast<std::uint16_t>(sectionLength) &&
            libcrt::Memicmp(reinterpret_cast<const char*>(record + 4), section, sectionLength & 0xFFFF) == 0)
            break;
        slot = U16(entry);
        if (slot == 0xFFFF) return nullptr;
    }
    if (!key) return record;

    // The key: the section's own table follows its name.
    const std::uint8_t* keyTable = record + 4 + (sectionLength & 0xFFFF);
    slot = HomeSlot(key, U16(record));
    std::uint32_t keyLength = static_cast<std::uint32_t>(std::strlen(key));
    const std::uint8_t* value;
    for (;;) {
        const std::uint8_t* entry = keyTable + 6 * (slot & 0xFFFF);
        value = record + U32(entry + 2);
        if (U16(value + 1) == static_cast<std::uint16_t>(keyLength) &&
            libcrt::Memicmp(reinterpret_cast<const char*>(value + 3), key, keyLength & 0xFFFF) == 0)
            break;
        slot = U16(entry);
        if (slot == 0xFFFF) return nullptr;
    }

    std::uint8_t wanted = static_cast<std::uint8_t>(type);
    if (isScalar) *isScalar = (wanted & 0x80) ? 0 : 1;
    if (value[0] != wanted) {
        if (value[0] != (wanted & 0x7F)) {
            game::DebugLog(kWrongType);
            return nullptr;
        }
        if (isScalar) *isScalar = 1;
    }
    return value + 3 + (keyLength & 0xFFFF);
}
EI_REPLACE_METHOD(0x006946D0, CFileRegistry::FindData);

BOOL CFileRegistry::GetSectionNames(console::CStringArray* out) {
    const std::uint8_t* root = data_;
    int count = U16(root + 4);
    out->SetSize(count, -1);
    for (int i = 0; i < count; ++i) {
        const std::uint8_t* record = data_ + U32(root + 8 + 6 * i);
        RawString name = MakeString(reinterpret_cast<const char*>(record + 4), U16(record + 2));
        (*out)[i] = name.Get();
        name.Get().~CString();
    }
    return 1;
}
EI_REPLACE_METHOD(0x00694880, CFileRegistry::GetSectionNames);

int CFileRegistry::GetInt(const char* section, const char* key, int defaultValue) {
    if (const std::uint8_t* value = FindData(section, key, kRegInt, nullptr))
        return static_cast<int>(U32(value));
    if (const std::uint8_t* value = FindData(section, key, kRegFloat, nullptr))
        return FloatToInt(reinterpret_cast<const float*>(value));
    return defaultValue;
}
EI_REPLACE_METHOD(0x00694B90, CFileRegistry::GetInt);

int CFileRegistry::GetInt2(const char* section, const char* key, int defaultValue) {
    return GetInt(section, key, defaultValue);
}
EI_REPLACE_METHOD(0x00694BE0, CFileRegistry::GetInt2);

double CFileRegistry::GetFloat(const char* section, const char* key, float defaultValue) {
    if (const std::uint8_t* value = FindData(section, key, kRegFloat, nullptr))
        return *reinterpret_cast<const float*>(value);
    if (const std::uint8_t* value = FindData(section, key, kRegInt, nullptr))
        return static_cast<int>(U32(value));
    return defaultValue;
}
EI_REPLACE_METHOD(0x00694C80, CFileRegistry::GetFloat);

mfc::CString CFileRegistry::GetString(const char* section, const char* key, mfc::CString defaultValue) {
    if (const std::uint8_t* value = FindData(section, key, kRegString, nullptr)) {
        RawString text = MakeString(reinterpret_cast<const char*>(value + 2), U16(value));
        mfc::CString result(text.Get());
        text.Get().~CString();
        return result;
    }
    return defaultValue;
}
EI_REPLACE_METHOD(0x00694D20, CFileRegistry::GetString);

void CFileRegistry::GetStringBuffer(const char* section, const char* key, char* buffer, int* length,
                                    const char* defaultValue) {
    if (const std::uint8_t* value = FindData(section, key, kRegString, nullptr)) {
        std::uint16_t count = U16(value);
        if (static_cast<int>(count) >= *length) count = static_cast<std::uint16_t>(*length - 1);
        gamecrt::strncpy(buffer, reinterpret_cast<const char*>(value + 2), count);
        buffer[count] = '\0';
        *length = count;
        return;
    }
    gamecrt::strncpy(buffer, defaultValue, static_cast<std::size_t>(*length));
    *length = static_cast<int>(std::strlen(buffer));
}
EI_REPLACE_METHOD(0x00694DC0, CFileRegistry::GetStringBuffer);

BOOL CFileRegistry::GetIntArray(const char* section, const char* key, DwordArray* out) {
    return GetDwordArray(this, section, key, kRegArray | kRegInt, out);
}
EI_REPLACE_METHOD(0x00695410, CFileRegistry::GetIntArray);

BOOL CFileRegistry::GetFloatArray(const char* section, const char* key, DwordArray* out) {
    return GetDwordArray(this, section, key, kRegArray | kRegFloat, out);
}
EI_REPLACE_METHOD(0x00695600, CFileRegistry::GetFloatArray);

BOOL CFileRegistry::GetStringArray(const char* section, const char* key, console::CStringArray* out) {
    int isScalar;
    const std::uint8_t* value = FindData(section, key, kRegArray | kRegString, &isScalar);
    if (!value) {
        out->SetSize(0, -1);
        return 0;
    }
    int count;
    if (isScalar) {
        count = 1;
    } else {
        count = U16(value);
        value += 2;
    }
    out->SetSize(count, -1);
    for (int i = 0; i < count; ++i) {
        std::uint16_t length = U16(value);
        RawString text = MakeString(reinterpret_cast<const char*>(value + 2), length);
        (*out)[i] = text.Get();
        text.Get().~CString();
        value += length + 2;
    }
    return 1;
}
EI_REPLACE_METHOD(0x006958E0, CFileRegistry::GetStringArray);
