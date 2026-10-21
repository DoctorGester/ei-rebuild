// The game CRT's locale set-up and its two multibyte class tests (S4c, docs/notes/standalone.md).
//
// The game sets one locale: the text manager's Create (004DED20) calls setlocale(LC_CTYPE, the "Font
// CharSet" option, "english" by default) and then _setmbcp(_MB_CP_LOCALE). In the hook build the exe's own
// setlocale / _setmbcp do it (the CRT state they write is the exe's, read by the exe's remaining library
// code), so those two are registered for the standalone image only, with the CRT start-up's
// __initmbctable (InitMultibyteTable, called by the image's start). VC6's code on Windows NT: names are
// resolved by getqloc.c's search over EnumSystemLocalesA with the exe's abbreviation tables, the class
// tables built by __init_ctype through GetStringTypeW, the multibyte tables by _setmbcp. The CRT's locks
// and __setlc_active are left out (one thread sets the locale), and so is setlocale(LC_ALL, ...), which
// the game never calls.
//
// _ismbcdigit / _ismbcspace keep no state: both builds. verify_crt_locale.cpp compared all of
// it with the originals (until S21d).
#include "crt.h"

#include "ei/hook.h"
#include "gameapi.h"
#include "game/game_data.h"

#include <cstring>

namespace libcrt {

namespace {

// --- The locale data in the exe. ---------------------------------------------------------------------

constexpr int kAll = 0;       // LC_ALL
constexpr int kCollate = 1;   // LC_COLLATE
constexpr int kCtype = 2;     // LC_CTYPE
constexpr int kLastCategory = 5;

// __lc_category (00797888): each category's name, its locale name (the static "C" 00797778 or a heap
// copy) and the function that rebuilds its tables (0 on success).
struct LocaleCategory {
    const char* name;
    char* locale;
    int(__cdecl* init)();
};
LocaleCategory* Categories() { return ei::GamePointer<LocaleCategory>(0x00797888); }
const char* StaticC() { return ei::GamePointer<const char>(0x00797778); }
// __lc_handle (007CABE8): each category's language id (0 for "C"); __lc_id (007CAC30): its LC_ID.
unsigned* Handles() { return ei::GamePointer<unsigned>(0x007CABE8); }
LocaleId* Ids() { return ei::GamePointer<LocaleId>(0x007CAC30); }
// __lc_codepage (007CAC00) and __lc_collate_cp (007CAC04).
unsigned& CtypeCodePage() { return ei::GameData<unsigned>(0x007CAC00); }
unsigned& CollateCodePage() { return ei::GameData<unsigned>(0x007CAC04); }
// getqloc's cache: the last name resolved, qualified (00797800) and as asked for (0079777C), its LC_ID
// (007CAB24) and code page (007CAB2C).
char* QualifiedCache() { return ei::GamePointer<char>(0x00797800); }
char* RequestedCache() { return ei::GamePointer<char>(0x0079777C); }
LocaleId& CachedId() { return ei::GameData<LocaleId>(0x007CAB24); }
unsigned& CachedCodePage() { return ei::GameData<unsigned>(0x007CAB2C); }

// The class tables: _pctype / _pwctype (00797DD8 / 00797DDC; the C locale's table + 1 entry is 00797DE2),
// the heap blocks behind them (007CAC28 / 007CAC2C, null for "C") and __mb_cur_max (00797FE4).
const unsigned short*& CtypeTable() { return ei::GameData<const unsigned short*>(0x00797DD8); }
const unsigned short*& WideCtypeTable() { return ei::GameData<const unsigned short*>(0x00797DDC); }
const unsigned short* StaticCtype() { return ei::GamePointer<const unsigned short>(0x00797DE2); }
unsigned short*& CtypeBlock() { return ei::GameData<unsigned short*>(0x007CAC28); }
unsigned short*& WideCtypeBlock() { return ei::GameData<unsigned short*>(0x007CAC2C); }
int& MbCurMax() { return ei::GameData<int>(0x00797FE4); }

// The multibyte code page: _mbctype (007CC180, 257 bytes, a byte's flags at + 1), _mbcasemap (007CC080),
// __mbcodepage (007CC058), __ismbcodepage (007CC06C), __mblcid (007CC284), __mbulinfo (007CC060, three
// dwords), fSystemSet (007CAB20: the code page came from the system) and __initmbctable's flag (007CC28C).
unsigned char* MbFlags() { return ei::GamePointer<unsigned char>(0x007CC180); }
unsigned char* MbCaseMap() { return ei::GamePointer<unsigned char>(0x007CC080); }
unsigned& MbCodePage() { return ei::GameData<unsigned>(0x007CC058); }
int& IsMbCodePage() { return ei::GameData<int>(0x007CC06C); }
unsigned& MbLcid() { return ei::GameData<unsigned>(0x007CC284); }
unsigned* MbUlInfo() { return ei::GamePointer<unsigned>(0x007CC060); }
int& SystemSet() { return ei::GameData<int>(0x007CAB20); }
int& MbTableReady() { return ei::GameData<int>(0x007CC28C); }
constexpr unsigned kMbFlagsSize = 257;

// __rgcode_page_info (00797688): the four DBCS code pages and Johab, each with its lead / trail byte
// ranges per class (pairs, ended by a zero) and __rgctypeflag (00797680) the class flags.
struct CodePageInfo {
    unsigned codePage;
    unsigned ulInfo[3];
    unsigned char ranges[4][8];
};
static_assert(sizeof(CodePageInfo) == 0x30);
const CodePageInfo* CodePageInfos() { return ei::GamePointer<const CodePageInfo>(0x00797688); }
constexpr int kCodePageInfos = 5;
const unsigned char* ClassFlags() { return ei::GamePointer<const unsigned char>(0x00797680); }

constexpr unsigned char kMbLead = 0x4;    // _M1
constexpr unsigned char kMbTrail = 0x8;   // _M2
constexpr unsigned char kSbUpper = 0x10;  // _SBUP
constexpr unsigned char kSbLower = 0x20;  // _SBLOW

// The language and country abbreviation tables (__rg_language 007989E0, 65 entries; __rg_country
// 00798928, 23 entries; sorted for stricmp) and the language ids that are not their country's default
// (00798914, 10).
struct NameAbbreviation {
    const char* name;
    char abbreviation[4];
};
const NameAbbreviation* LanguageTable() { return ei::GamePointer<const NameAbbreviation>(0x007989E0); }
const NameAbbreviation* CountryTable() { return ei::GamePointer<const NameAbbreviation>(0x00798928); }
constexpr int kLastLanguage = 0x40;
constexpr int kLastCountry = 0x16;
const unsigned short* NonDefaultCountries() { return ei::GamePointer<const unsigned short>(0x00798914); }
constexpr int kNonDefaultCountries = 10;

// strcpy / strcat as VC6's (no bound).
void CopyText(char* to, const char* from) { std::memcpy(to, from, std::strlen(from) + 1); }
void AppendText(char* to, const char* from) { CopyText(to + std::strlen(to), from); }

// --- The NT paths of __crtGetStringTypeA / W and __crtLCMapStringA. --------------------------------------

// 006F3CC5: the bytes to UTF-16 in the code page (the locale's when 0), then GetStringTypeW.
BOOL StringTypeA(DWORD type, const char* text, int count, WORD* types, unsigned codePage, bool rejectInvalid) {
    if (codePage == 0) codePage = CtypeCodePage();
    const int wideLength = gameapi::MultiByteToWideChar(
        codePage, MB_PRECOMPOSED | (rejectInvalid ? MB_ERR_INVALID_CHARS : 0), text, count, nullptr, 0);
    wchar_t wide[512] = {};
    if (wideLength == 0 || wideLength > 512) return FALSE;
    const int converted = gameapi::MultiByteToWideChar(codePage, MB_PRECOMPOSED, text, count, wide, wideLength);
    if (converted == 0) return FALSE;
    return gameapi::GetStringTypeW(type, wide, converted, types);
}

// 006F1FDE (no LCMAP_SORTKEY): the bytes up to `count` or a zero, to UTF-16, mapped, and back; the bytes
// written, or 0.
int MapStringA(LCID locale, DWORD flags, const char* text, int count, char* to, int room, unsigned codePage) {
    int length = 0;
    while (length < count && text[length] != '\0') ++length;
    if (codePage == 0) codePage = CtypeCodePage();
    const int wideLength = gameapi::MultiByteToWideChar(codePage, MB_PRECOMPOSED, text, length, nullptr, 0);
    wchar_t wide[512];
    wchar_t mapped[1024];
    if (wideLength == 0 || wideLength > 512 ||
        !gameapi::MultiByteToWideChar(codePage, MB_PRECOMPOSED, text, length, wide, wideLength))
        return 0;
    const int mappedLength = gameapi::LCMapStringW(locale, flags, wide, wideLength, nullptr, 0);
    if (mappedLength == 0 || mappedLength > 1024 ||
        !gameapi::LCMapStringW(locale, flags, wide, wideLength, mapped, mappedLength))
        return 0;
    return gameapi::WideCharToMultiByte(codePage, WC_COMPOSITECHECK | WC_SEPCHARS, mapped, mappedLength, to, room,
                                        nullptr, nullptr);
}

// --- getqloc.c: a locale name to its LC_ID, code page and full name. ----------------------------------

// The name's parts (__lc_strtolc's LC_STRINGS: language, country, code page). The code page has 8 bytes
// in VC6, copied without a check (a longer one overruns the stack); here it has 127 and is cut there.
struct LocaleNames {
    char language[64];
    char country[64];
    char codePage[8];
    char codePageOverrun[120];
};

// __lc_strtolc (006EA78E): "language[_country[.code page]]" (a ',' ends it) or ".code page"; "_country..."
// is refused, so is "language.code page" (a '.' straight after the language); 0, or -1 when the name does
// not parse. An empty name leaves every part empty.
std::size_t CodePageLength(std::size_t length) {
    return length < sizeof(LocaleNames::codePage) + sizeof(LocaleNames::codePageOverrun) ? length : 127;
}

int ParseLocaleName(LocaleNames& names, const char* name) {
    std::memset(&names, 0, sizeof names);
    if (*name == '\0') return 0;
    if (name[0] == '.' && name[1] != '\0') {
        std::memcpy(names.codePage, name + 1, CodePageLength(std::strlen(name + 1)));
        return 0;
    }
    for (int part = 0;; ++part) {
        const std::size_t count = std::strcspn(name, "_.,");
        if (count == 0) return -1;
        const char next = name[count];
        char* to;
        if (part == 0) {
            if (count >= 64 || next == '.') return -1;
            to = names.language;
        } else if (part == 1) {
            if (count >= 64 || next == '_') return -1;
            to = names.country;
        } else if (part == 2) {
            if (next != '\0' && next != ',') return -1;
            to = names.codePage;
        } else {
            return -1;
        }
        std::memcpy(to, name, part == 2 ? CodePageLength(count) : count);   // strncpy into the zeroed part
        if (next == ',' || next == '\0') return 0;
        name += count + 1;
    }
}

// The search's state (getqloc.c's statics; the exe's at 007CAC54-007CAC70 are left alone).
struct LocaleSearch {
    const char* language;
    const char* country;
    bool abbreviatedLanguage;
    bool abbreviatedCountry;
    int primaryLength;
    unsigned state;
    LCID languageId;
    LCID countryId;
};
LocaleSearch* search;

constexpr unsigned kFoundCountry = 0x1;      // __LCID_DEFAULT
constexpr unsigned kFoundPrimary = 0x2;      // __LCID_PRIMARY
constexpr unsigned kFoundFull = 0x4;         // __LCID_FULL
constexpr unsigned kLanguageId = 0x100;     // __LCID_LANGUAGE: the language id found
constexpr unsigned kLanguageExists = 0x200; // __LCID_EXISTS: the language is installed

// 006F5317: the hexadecimal string EnumSystemLocalesA passes.
LCID LcidFromHex(const char* text) {
    LCID lcid = 0;
    for (char c; (c = *text++) != '\0';) {
        if (c >= 'a' && c <= 'f') c -= 0x27;
        else if (c >= 'A' && c <= 'F') c -= 7;
        lcid = lcid * 16 + c - '0';
    }
    return lcid;
}

// 006F5350: the letters the name starts with.
int PrimaryLength(const char* text) {
    int length = 0;
    while ((text[length] >= 'A' && text[length] <= 'Z') || (text[length] >= 'a' && text[length] <= 'z')) ++length;
    return length;
}

// 006F4C47: the name looked up in a table, replaced by its abbreviation when found.
void TranslateName(const NameAbbreviation* table, int high, const char** name) {
    int low = 0;
    int compare = 1;
    while (compare != 0 && low <= high) {
        const int middle = (low + high) / 2;
        compare = Stricmp(*name, table[middle].name);
        if (compare == 0) *name = table[middle].abbreviation;
        else if (compare < 0) high = middle - 1;
        else low = middle + 1;
    }
}

// 006F517A: false for a language id that is not its country's default.
bool IsDefaultCountry(LCID lcid) {
    for (int i = 0; i < kNonDefaultCountries; ++i)
        if (static_cast<unsigned short>(lcid) == NonDefaultCountries()[i]) return false;
    return true;
}

// 006F5199: whether the language's default sublanguage is this one (when the name gave only the primary
// language, `testPrimary`).
bool IsDefaultLanguage(LCID lcid, bool testPrimary) {
    char text[120];
    if (!gameapi::GetLocaleInfoA((lcid & 0x3FF) | 0x400, LOCALE_ILANGUAGE, text, sizeof text)) return false;
    if (lcid != LcidFromHex(text) && testPrimary &&
        PrimaryLength(search->language) == static_cast<int>(std::strlen(search->language)))
        return false;
    return true;
}

LCTYPE LanguageType() { return search->abbreviatedLanguage ? LOCALE_SABBREVLANGNAME : LOCALE_SENGLANGUAGE; }
LCTYPE CountryType() { return search->abbreviatedCountry ? LOCALE_SABBREVCTRYNAME : LOCALE_SENGCOUNTRY; }
BOOL Continue() { return (search->state & kFoundFull) == 0; }

// 006F4D26: a locale against both the language and the country.
BOOL CALLBACK LanguageCountryProc(LPSTR text) {
    const LCID lcid = LcidFromHex(text);
    char name[120];
    if (!gameapi::GetLocaleInfoA(lcid, CountryType(), name, sizeof name)) {
        search->state = 0;
        return TRUE;
    }
    if (Stricmp(search->country, name) == 0) {
        if (!gameapi::GetLocaleInfoA(lcid, LanguageType(), name, sizeof name)) {
            search->state = 0;
            return TRUE;
        }
        if (Stricmp(search->language, name) == 0) {
            search->state |= kLanguageId | kLanguageExists | kFoundFull;
            search->countryId = lcid;
            search->languageId = lcid;
        } else if (!(search->state & kFoundPrimary)) {
            if (search->primaryLength != 0 && Strnicmp(search->language, name, search->primaryLength) == 0) {
                search->state |= kFoundPrimary;
                search->countryId = lcid;
                if (static_cast<int>(std::strlen(search->language)) == search->primaryLength) search->languageId = lcid;
            } else if (!(search->state & kFoundCountry) && IsDefaultCountry(lcid)) {
                search->state |= kFoundCountry;
                search->countryId = lcid;
            }
        }
    }
    if ((search->state & (kLanguageId | kLanguageExists)) == (kLanguageId | kLanguageExists)) return Continue();
    if (!gameapi::GetLocaleInfoA(lcid, LanguageType(), name, sizeof name)) {
        search->state = 0;
        return TRUE;
    }
    if (Stricmp(search->language, name) == 0) {
        search->state |= kLanguageExists;
        if (!search->abbreviatedLanguage && search->primaryLength != 0 &&
            static_cast<int>(std::strlen(search->language)) == search->primaryLength &&
            !IsDefaultLanguage(lcid, true))
            return Continue();
    } else if (search->abbreviatedLanguage || search->primaryLength == 0 ||
               Strnicmp(search->language, name, search->primaryLength) != 0 || !IsDefaultLanguage(lcid, false)) {
        return Continue();
    }
    search->state |= kLanguageId;
    if (search->languageId == 0) search->languageId = lcid;
    return Continue();
}

// 006F4F80: a locale against the language alone.
BOOL CALLBACK LanguageProc(LPSTR text) {
    const LCID lcid = LcidFromHex(text);
    char name[120];
    if (!gameapi::GetLocaleInfoA(lcid, LanguageType(), name, sizeof name)) {
        search->state = 0;
        return TRUE;
    }
    if (Stricmp(search->language, name) == 0) {
        if (!search->abbreviatedLanguage && !IsDefaultLanguage(lcid, true)) return Continue();
    } else if (search->abbreviatedLanguage || search->primaryLength == 0 ||
               Strnicmp(search->language, name, search->primaryLength) != 0 || !IsDefaultLanguage(lcid, false)) {
        return Continue();
    }
    search->state |= kFoundFull;
    search->languageId = lcid;
    search->countryId = lcid;
    return Continue();
}

// 006F5074: a locale against the country alone.
BOOL CALLBACK CountryProc(LPSTR text) {
    const LCID lcid = LcidFromHex(text);
    char name[120];
    if (!gameapi::GetLocaleInfoA(lcid, CountryType(), name, sizeof name)) {
        search->state = 0;
        return TRUE;
    }
    if (Stricmp(search->country, name) == 0 && IsDefaultCountry(lcid)) {
        search->state |= kFoundFull;
        search->languageId = lcid;
        search->countryId = lcid;
    }
    return Continue();
}

int StartLength(const char* language) { return search->abbreviatedLanguage ? 2 : PrimaryLength(language); }

// 006F4C9F, 006F4F2A, 006F503D, 006F50FA.
void FindLanguageAndCountry() {
    search->abbreviatedLanguage = std::strlen(search->language) == 3;
    search->abbreviatedCountry = std::strlen(search->country) == 3;
    search->languageId = 0;
    search->primaryLength = StartLength(search->language);
    gameapi::EnumSystemLocalesA(LanguageCountryProc, LCID_INSTALLED);
    if (!(search->state & kLanguageId) || !(search->state & kLanguageExists) ||
        !(search->state & (kFoundCountry | kFoundPrimary | kFoundFull)))
        search->state = 0;
}

void FindLanguage() {
    search->abbreviatedLanguage = std::strlen(search->language) == 3;
    search->primaryLength = StartLength(search->language);
    gameapi::EnumSystemLocalesA(LanguageProc, LCID_INSTALLED);
    if (!(search->state & kFoundFull)) search->state = 0;
}

void FindCountry() {
    search->abbreviatedCountry = std::strlen(search->country) == 3;
    gameapi::EnumSystemLocalesA(CountryProc, LCID_INSTALLED);
    if (!(search->state & kFoundFull)) search->state = 0;
}

void FindDefault() {
    search->state |= kLanguageId | kFoundFull;
    search->countryId = gameapi::GetUserDefaultLCID();
    search->languageId = search->countryId;
}

// 006F5114: "", "ACP" (the country's ANSI code page), "OCP" (its OEM one) or a number.
unsigned ProcessCodePage(const char* text) {
    char number[8];
    LCTYPE type;
    if (text == nullptr || *text == '\0' || std::strcmp(text, "ACP") == 0) type = LOCALE_IDEFAULTANSICODEPAGE;
    else if (std::strcmp(text, "OCP") == 0) type = LOCALE_IDEFAULTCODEPAGE;
    else return static_cast<unsigned>(Atol(text));
    if (!gameapi::GetLocaleInfoA(search->countryId, type, number, sizeof number)) return 0;
    return static_cast<unsigned>(Atol(number));
}

// __get_qualified_locale (006F4ACA): the parts to a language and country id and a code page (`id`), and
// the full English names and the code page's number into `names`; false when none is found, or the code
// page or locale is not valid.
bool QualifyNames(LocaleNames& names, LocaleId& id) {
    LocaleSearch state{};
    search = &state;
    state.language = names.language;
    if (*state.language) TranslateName(LanguageTable(), kLastLanguage, &state.language);
    state.country = names.country;
    if (*state.country) TranslateName(CountryTable(), kLastCountry, &state.country);
    if (*state.language) {
        if (*state.country) FindLanguageAndCountry();
        else FindLanguage();
    } else if (*state.country) {
        FindCountry();
    } else {
        FindDefault();
    }
    unsigned codePage;
    if (state.state == 0 || (codePage = ProcessCodePage(names.codePage)) == 0 ||
        !gameapi::IsValidCodePage(codePage & 0xFFFF) || !gameapi::IsValidLocale(state.languageId, LCID_INSTALLED))
        return false;
    id.language = static_cast<unsigned short>(state.languageId);
    id.country = static_cast<unsigned short>(state.countryId);
    id.codePage = static_cast<unsigned short>(codePage);
    if (!gameapi::GetLocaleInfoA(state.languageId, LOCALE_SENGLANGUAGE, names.language, sizeof names.language) ||
        !gameapi::GetLocaleInfoA(state.countryId, LOCALE_SENGCOUNTRY, names.country, sizeof names.country))
        return false;
    Itoa(static_cast<int>(codePage), names.codePage, 10);
    return true;
}

// __lc_lctostr (006EA85A): "language[_country][.code page]".
void JoinNames(char* to, const LocaleNames& names) {
    CopyText(to, names.language);
    if (names.country[0] != '\0') {
        AppendText(to, "_");
        AppendText(to, names.country);
    }
    if (names.codePage[0] != '\0') {
        AppendText(to, ".");
        AppendText(to, names.codePage);
    }
}

// _expandlocale (006EA64E): the full name into `out` (and the cache), with its LC_ID and code page;
// null when the name is not a locale. "C" is itself; the last name resolved (qualified or as asked for)
// comes from the cache.
const char* ExpandLocale(const char* name, char* out, LocaleId* id, unsigned* codePage) {
    if (name == nullptr) return nullptr;
    if (name[0] == 'C' && name[1] == '\0') {
        out[0] = 'C';
        out[1] = '\0';
        if (id) *id = {};
        if (codePage) *codePage = 0;
        return out;
    }
    if (std::strcmp(QualifiedCache(), name) != 0 && std::strcmp(RequestedCache(), name) != 0) {
        LocaleNames names;
        if (ParseLocaleName(names, name) != 0 || !QualifyNames(names, CachedId())) return nullptr;
        CachedCodePage() = CachedId().codePage;
        JoinNames(QualifiedCache(), names);
        if (*name == '\0') name = QualifiedCache();
        CopyText(RequestedCache(), name);
    }
    if (id) *id = CachedId();
    if (codePage) *codePage = CachedCodePage();
    CopyText(out, QualifiedCache());
    return QualifiedCache();
}

// _setlocale_set_cat (006EA47A): the category's name, handle, LC_ID (and code page) set and its tables
// rebuilt; the new name, or null (the old name, handle and __lc_codepage put back; the LC_ID is not, as
// in VC6). LC_CTYPE's tables are rebuilt by InitCtype, the other categories' by the exe's functions.
char* SetCategory(int category, const char* name) {
    char qualified[132];
    LocaleId id;
    unsigned codePage;
    if (!ExpandLocale(name, qualified, &id, &codePage)) return nullptr;
    char* copy = static_cast<char*>(gamecrt::malloc(std::strlen(qualified) + 1));
    if (!copy) return nullptr;
    LocaleCategory& entry = Categories()[category];
    char* const oldLocale = entry.locale;
    const unsigned oldHandle = Handles()[category];
    const unsigned oldCodePage = CtypeCodePage();
    CopyText(copy, qualified);
    entry.locale = copy;
    Handles()[category] = id.language;
    Ids()[category] = id;
    if (category == kCtype) CtypeCodePage() = codePage;
    if (category == kCollate) CollateCodePage() = codePage;
    const int failed = category == kCtype ? InitCtype() : entry.init();
    if (failed) {
        entry.locale = oldLocale;
        gamecrt::free(copy);
        Handles()[category] = oldHandle;
        CtypeCodePage() = oldCodePage;
        return nullptr;
    }
    if (oldLocale != StaticC()) gamecrt::free(oldLocale);
    return entry.locale;
}

// --- _setmbcp. ---------------------------------------------------------------------------------------

// getSystemCP (006E9FDD): _MB_CP_OEM, _MB_CP_ANSI and _MB_CP_LOCALE to a code page.
unsigned SystemCodePage(int codePage) {
    SystemSet() = 1;
    if (codePage == -2) return gameapi::GetOEMCP();
    if (codePage == -3) return gameapi::GetACP();
    if (codePage == -4) return CtypeCodePage();
    SystemSet() = 0;
    return static_cast<unsigned>(codePage);
}

// CPtoLCID (006EA027).
unsigned CodePageLcid(unsigned codePage) {
    switch (codePage) {
    case 932: return 0x411;
    case 936: return 0x804;
    case 949: return 0x412;
    case 950: return 0x404;
    default: return 0;
    }
}

// setSBCS (006EA05A).
void SetSingleByte() {
    std::memset(MbFlags(), 0, kMbFlagsSize);
    MbCodePage() = 0;
    IsMbCodePage() = 0;
    MbLcid() = 0;
    MbUlInfo()[0] = MbUlInfo()[1] = MbUlInfo()[2] = 0;
}

// setSBUpLow (006EA083): the single-byte case flags and _mbcasemap (each byte's other case), from the
// code page's classes and case maps; ASCII without the code page.
void SetSingleByteCase() {
    CPINFO info;
    if (gameapi::GetCPInfo(MbCodePage(), &info) == TRUE) {
        char bytes[256];
        for (unsigned i = 0; i < 256; ++i) bytes[i] = static_cast<char>(i);
        bytes[0] = ' ';
        for (const BYTE* lead = info.LeadByte; lead[0] != 0; lead += 2)
            for (unsigned c = lead[0]; c <= lead[1]; ++c) bytes[c] = ' ';
        WORD types[256] = {};
        char lower[256] = {};
        char upper[256] = {};
        StringTypeA(CT_CTYPE1, bytes, 256, types, MbCodePage(), false);
        MapStringA(MbLcid(), LCMAP_LOWERCASE, bytes, 256, lower, 256, MbCodePage());
        MapStringA(MbLcid(), LCMAP_UPPERCASE, bytes, 256, upper, 256, MbCodePage());
        for (unsigned i = 0; i < 256; ++i) {
            if (types[i] & C1_UPPER) {
                MbFlags()[i + 1] |= kSbUpper;
                MbCaseMap()[i] = static_cast<unsigned char>(lower[i]);
            } else if (types[i] & C1_LOWER) {
                MbFlags()[i + 1] |= kSbLower;
                MbCaseMap()[i] = static_cast<unsigned char>(upper[i]);
            } else {
                MbCaseMap()[i] = 0;
            }
        }
        return;
    }
    for (unsigned i = 0; i < 256; ++i) {
        if (i >= 'A' && i <= 'Z') {
            MbFlags()[i + 1] |= kSbUpper;
            MbCaseMap()[i] = static_cast<unsigned char>(i + 0x20);
        } else if (i >= 'a' && i <= 'z') {
            MbFlags()[i + 1] |= kSbLower;
            MbCaseMap()[i] = static_cast<unsigned char>(i - 0x20);
        } else {
            MbCaseMap()[i] = 0;
        }
    }
}

} // namespace

// --- __init_ctype (006F47FB). ----------------------------------------------------------------------------

int InitCtype() {
    if (Handles()[kCtype] == 0) {
        CtypeTable() = StaticCtype();
        WideCtypeTable() = StaticCtype();
        gamecrt::free(CtypeBlock());
        gamecrt::free(WideCtypeBlock());
        CtypeBlock() = nullptr;
        WideCtypeBlock() = nullptr;
        return 0;
    }
    // Not reached by setlocale (the code page is set with the handle): __getlocaleinfo's LC_INT_TYPE.
    if (CtypeCodePage() == 0) {
        char number[128];
        if (!gameapi::GetLocaleInfoA(Ids()[kCtype].language, LOCALE_IDEFAULTANSICODEPAGE, number, sizeof number))
            return 1;
        CtypeCodePage() = static_cast<unsigned>(Atol(number));
    }
    auto* classes = static_cast<unsigned short*>(gamecrt::malloc(0x202));
    auto* wideClasses = static_cast<unsigned short*>(gamecrt::malloc(0x202));
    auto* bytes = static_cast<unsigned char*>(gamecrt::malloc(0x101));
    auto* wide = static_cast<wchar_t*>(gamecrt::malloc(0x202));
    bool built = false;
    CPINFO info;
    if (classes && wideClasses && bytes && wide) {
        for (int i = 0; i < 256; ++i) bytes[i] = static_cast<unsigned char>(i);
        if (gameapi::GetCPInfo(CtypeCodePage(), &info) && info.MaxCharSize < 3) {
            MbCurMax() = static_cast<unsigned short>(info.MaxCharSize);
            if (MbCurMax() > 1)
                for (const BYTE* lead = info.LeadByte; lead[0] != 0 && lead[1] != 0; lead += 2)
                    for (unsigned c = lead[0]; c <= lead[1]; ++c) bytes[c] = 0;
            if (StringTypeA(CT_CTYPE1, reinterpret_cast<const char*>(bytes), 256, classes + 1, 0, false)) {
                classes[0] = 0;
                for (int i = 0; i < 256; ++i) wide[i] = static_cast<wchar_t>(i);
                if (gameapi::GetStringTypeW(CT_CTYPE1, wide, 256, wideClasses + 1)) {
                    wideClasses[0] = 0;
                    if (MbCurMax() > 1)
                        for (const BYTE* lead = info.LeadByte; lead[0] != 0 && lead[1] != 0; lead += 2)
                            for (unsigned c = lead[0]; c <= lead[1]; ++c) classes[c + 1] = 0x8000;   // _LEADBYTE
                    CtypeTable() = classes + 1;
                    WideCtypeTable() = wideClasses + 1;
                    if (CtypeBlock()) gamecrt::free(CtypeBlock());
                    CtypeBlock() = classes;
                    if (WideCtypeBlock()) gamecrt::free(WideCtypeBlock());
                    WideCtypeBlock() = wideClasses;
                    built = true;
                }
            }
        }
    }
    if (!built) {
        gamecrt::free(classes);
        gamecrt::free(wideClasses);
    }
    gamecrt::free(bytes);
    gamecrt::free(wide);
    return built ? 0 : 1;
}

// --- setlocale (006EA234), _setmbcp (006E9E30), __initmbctable (006EA218). ---------------------------

char* __cdecl Setlocale(int category, const char* locale) {
    if (category < kAll || category > kLastCategory || category == kAll) return nullptr;
    if (locale == nullptr) return Categories()[category].locale;
    return SetCategory(category, locale);
}
EI_REWRITTEN(0x006EA234, libcrt::Setlocale);

int __cdecl Setmbcp(int codePage) {
    const unsigned page = SystemCodePage(codePage);
    if (page == MbCodePage()) return 0;
    if (page == 0) {
        SetSingleByte();
        SetSingleByteCase();
        return 0;
    }
    for (int i = 0; i < kCodePageInfos; ++i) {
        const CodePageInfo& known = CodePageInfos()[i];
        if (known.codePage != page) continue;
        std::memset(MbFlags(), 0, kMbFlagsSize);
        // The ranges are read as VC6 reads them: pairs until a zero (a full row runs into the next).
        for (int kind = 0; kind < 4; ++kind)
            for (const unsigned char* range = known.ranges[kind]; range[0] != 0 && range[1] != 0; range += 2)
                for (unsigned c = range[0]; c <= range[1]; ++c) MbFlags()[c + 1] |= ClassFlags()[kind];
        IsMbCodePage() = 1;
        MbCodePage() = page;
        MbLcid() = CodePageLcid(page);
        MbUlInfo()[0] = known.ulInfo[0];
        MbUlInfo()[1] = known.ulInfo[1];
        MbUlInfo()[2] = known.ulInfo[2];
        SetSingleByteCase();
        return 0;
    }
    CPINFO info;
    if (gameapi::GetCPInfo(page, &info) == TRUE) {
        MbLcid() = 0;
        std::memset(MbFlags(), 0, kMbFlagsSize);
        MbCodePage() = page;
        if (info.MaxCharSize < 2) {
            IsMbCodePage() = 0;
        } else {
            for (const BYTE* lead = info.LeadByte; lead[0] != 0 && lead[1] != 0; lead += 2)
                for (unsigned c = lead[0]; c <= lead[1]; ++c) MbFlags()[c + 1] |= kMbLead;
            for (unsigned c = 1; c < 0xFF; ++c) MbFlags()[c + 1] |= kMbTrail;
            MbLcid() = CodePageLcid(page);
            IsMbCodePage() = 1;
        }
        MbUlInfo()[0] = MbUlInfo()[1] = MbUlInfo()[2] = 0;
        SetSingleByteCase();
        return 0;
    }
    if (!SystemSet()) return -1;
    SetSingleByte();
    SetSingleByteCase();
    return 0;
}
EI_REWRITTEN(0x006E9E30, libcrt::Setmbcp);

void InitMultibyteTable() {
    if (MbTableReady() == 0) {
        Setmbcp(-3);
        MbTableReady() = 1;
    }
}

// --- _ismbcdigit (006ECE1A), _ismbcspace (006ECEA9). ------------------------------------------------------

namespace {

// A byte by its _pctype entry (_isctype's answer too, under a multibyte code page); a double-byte
// character (lead byte high) by GetStringTypeW in the multibyte code page, when it has one.
int IsMultibyteClass(unsigned c, WORD mask) {
    if (c < 0x100) return CtypeTable()[c] & mask & 0xFF;
    if (!IsMbCodePage()) return 0;
    const char bytes[2] = {static_cast<char>(c >> 8), static_cast<char>(c)};
    WORD types[2] = {};
    if (StringTypeA(CT_CTYPE1, bytes, 2, types, MbCodePage(), true) && types[1] == 0 && (types[0] & mask)) return 1;
    return 0;
}

} // namespace

int __cdecl Ismbcdigit(unsigned c) { return IsMultibyteClass(c, C1_DIGIT); }
EI_REWRITTEN(0x006ECE1A, libcrt::Ismbcdigit);

int __cdecl Ismbcspace(unsigned c) { return IsMultibyteClass(c, C1_SPACE); }
EI_REWRITTEN(0x006ECEA9, libcrt::Ismbcspace);

} // namespace libcrt
