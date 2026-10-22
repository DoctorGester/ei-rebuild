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
#include "crt_locale_data.h"

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
// The state setlocale writes (__lc_handle, __lc_id, the code pages, getqloc's cache): crt_locale_data.h.

// The class tables (_ctype, _pctype / _pwctype), the heap blocks behind them and __mb_cur_max:
// crt_locale_data.h's.

// The multibyte code page's tables and state (_mbctype, _mbcasemap, ...): crt_locale_data.h.
constexpr unsigned kMbFlagsSize = 257;

// __rgcode_page_info: the four DBCS code pages and Johab, each with its lead / trail byte ranges per class
// (pairs, ended by a zero), and __rgctypeflag the class flags.
struct CodePageInfo {
    unsigned codePage;
    unsigned ulInfo[3];
    unsigned char ranges[4][8];
};
static_assert(sizeof(CodePageInfo) == 0x30);
constexpr int kCodePageInfos = 5;
constexpr CodePageInfo kCodePageTable[5] = /*00797688*/ {
    {932, {0x82798260, 0x21, 0x0},
     {{0xA6, 0xDF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {0xA1, 0xA5, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
      {0x81, 0x9F, 0xE0, 0xFC, 0x00, 0x00, 0x00, 0x00}, {0x40, 0x7E, 0x80, 0xFC, 0x00, 0x00, 0x00, 0x00}}},
    {936, {0xA3DAA3C1, 0x20, 0x0},
     {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
      {0x81, 0xFE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x40, 0xFE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}}},
    {949, {0xA3DAA3C1, 0x20, 0x0},
     {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
      {0x81, 0xFE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x41, 0xFE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}}},
    {950, {0xA2E4A2CF, 0xA2E5001A, 0x5BA2E8},
     {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
      {0x81, 0xFE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x40, 0x7E, 0xA1, 0xFE, 0x00, 0x00, 0x00, 0x00}}},
    {1361, {0xDA5EDA51, 0xDA5F0020, 0x32DA6A},
     {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
      {0x81, 0xD3, 0xD8, 0xDE, 0xE0, 0xF9, 0x00, 0x00}, {0x31, 0x7E, 0x81, 0xFE, 0x00, 0x00, 0x00, 0x00}}}};
constexpr std::uint8_t kClassFlags[4] = /*00797680*/ {0x1, 0x2, 0x4, 0x8};

constexpr unsigned char kMbLead = 0x4;    // _M1
constexpr unsigned char kMbTrail = 0x8;   // _M2
constexpr unsigned char kSbUpper = 0x10;  // _SBUP
constexpr unsigned char kSbLower = 0x20;  // _SBLOW

// The language ids that are not their country's default, and the language and country abbreviation
// tables (__rg_language, __rg_country; sorted for stricmp).
struct NameAbbreviation {
    const char* name;
    char abbreviation[4];
};
constexpr std::uint16_t kNonDefaultCountryIds[10] = /*00798914*/ {0xC0C, 0xC1A, 0x1007, 0x436, 0x80C,
                                                                    0x42D, 0x403, 0x100C, 0x810, 0x81D};
constexpr NameAbbreviation kCountries[23] = /*00798928*/ {
    {"america", "USA"},
    {"britain", "GBR"},
    {"china", "CHN"},
    {"czech", "CZE"},
    {"england", "GBR"},
    {"great britain", "GBR"},
    {"holland", "NLD"},
    {"hong-kong", "HKG"},
    {"new-zealand", "NZL"},
    {"nz", "NZL"},
    {"pr china", "CHN"},
    {"pr-china", "CHN"},
    {"puerto-rico", "PRI"},
    {"slovak", "SVK"},
    {"south africa", "ZAF"},
    {"south korea", "KOR"},
    {"south-africa", "ZAF"},
    {"south-korea", "KOR"},
    {"trinidad & tobago", "TTO"},
    {"uk", "GBR"},
    {"united-kingdom", "GBR"},
    {"united-states", "USA"},
    {"us", "USA"}
};
constexpr NameAbbreviation kLanguages[65] = /*007989E0*/ {
    {"american", "ENU"},
    {"american english", "ENU"},
    {"american-english", "ENU"},
    {"australian", "ENA"},
    {"belgian", "NLB"},
    {"canadian", "ENC"},
    {"chh", "ZHH"},
    {"chi", "ZHI"},
    {"chinese", "CHS"},
    {"chinese-hongkong", "ZHH"},
    {"chinese-simplified", "CHS"},
    {"chinese-singapore", "ZHI"},
    {"chinese-traditional", "CHT"},
    {"dutch-belgian", "NLB"},
    {"english-american", "ENU"},
    {"english-aus", "ENA"},
    {"english-belize", "ENL"},
    {"english-can", "ENC"},
    {"english-caribbean", "ENB"},
    {"english-ire", "ENI"},
    {"english-jamaica", "ENJ"},
    {"english-nz", "ENZ"},
    {"english-south africa", "ENS"},
    {"english-trinidad y tobago", "ENT"},
    {"english-uk", "ENG"},
    {"english-us", "ENU"},
    {"english-usa", "ENU"},
    {"french-belgian", "FRB"},
    {"french-canadian", "FRC"},
    {"french-luxembourg", "FRL"},
    {"french-swiss", "FRS"},
    {"german-austrian", "DEA"},
    {"german-lichtenstein", "DEC"},
    {"german-luxembourg", "DEL"},
    {"german-swiss", "DES"},
    {"irish-english", "ENI"},
    {"italian-swiss", "ITS"},
    {"norwegian", "NOR"},
    {"norwegian-bokmal", "NOR"},
    {"norwegian-nynorsk", "NON"},
    {"portuguese-brazilian", "PTB"},
    {"spanish-argentina", "ESS"},
    {"spanish-bolivia", "ESB"},
    {"spanish-chile", "ESL"},
    {"spanish-colombia", "ESO"},
    {"spanish-costa rica", "ESC"},
    {"spanish-dominican republic", "ESD"},
    {"spanish-ecuador", "ESF"},
    {"spanish-el salvador", "ESE"},
    {"spanish-guatemala", "ESG"},
    {"spanish-honduras", "ESH"},
    {"spanish-mexican", "ESM"},
    {"spanish-modern", "ESN"},
    {"spanish-nicaragua", "ESI"},
    {"spanish-panama", "ESA"},
    {"spanish-paraguay", "ESZ"},
    {"spanish-peru", "ESR"},
    {"spanish-puerto rico", "ESU"},
    {"spanish-uruguay", "ESY"},
    {"spanish-venezuela", "ESV"},
    {"swedish-finland", "SVF"},
    {"swiss", "DES"},
    {"uk", "ENG"},
    {"us", "ENU"},
    {"usa", "ENU"}
};
constexpr int kLastLanguage = 0x40;
constexpr int kLastCountry = 0x16;
static_assert(sizeof(kLanguages) / sizeof(kLanguages[0]) == kLastLanguage + 1);
static_assert(sizeof(kCountries) / sizeof(kCountries[0]) == kLastCountry + 1);
constexpr int kNonDefaultCountries = 10;

// strcpy / strcat as VC6's (no bound).
void CopyText(char* to, const char* from) { std::memcpy(to, from, std::strlen(from) + 1); }
void AppendText(char* to, const char* from) { CopyText(to + std::strlen(to), from); }

// --- The NT paths of __crtGetStringTypeA / W and __crtLCMapStringA. --------------------------------------

// 006F3CC5: the bytes to UTF-16 in the code page (the locale's when 0), then GetStringTypeW.
BOOL StringTypeA(DWORD type, const char* text, int count, WORD* types, unsigned codePage, bool rejectInvalid) {
    if (codePage == 0) codePage = g_lcCodePage;
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
    if (codePage == 0) codePage = g_lcCodePage;
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
        if (static_cast<unsigned short>(lcid) == kNonDefaultCountryIds[i]) return false;
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
    if (*state.language) TranslateName(kLanguages, kLastLanguage, &state.language);
    state.country = names.country;
    if (*state.country) TranslateName(kCountries, kLastCountry, &state.country);
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
    if (std::strcmp(g_cacheOut, name) != 0 && std::strcmp(g_cacheIn, name) != 0) {
        LocaleNames names;
        if (ParseLocaleName(names, name) != 0 || !QualifyNames(names, g_cachedId)) return nullptr;
        g_cachedCodePage = g_cachedId.codePage;
        JoinNames(g_cacheOut, names);
        if (*name == '\0') name = g_cacheOut;
        CopyText(g_cacheIn, name);
    }
    if (id) *id = g_cachedId;
    if (codePage) *codePage = g_cachedCodePage;
    CopyText(out, g_cacheOut);
    return g_cacheOut;
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
    const unsigned oldHandle = g_lcHandle[category];
    const unsigned oldCodePage = g_lcCodePage;
    CopyText(copy, qualified);
    entry.locale = copy;
    g_lcHandle[category] = id.language;
    g_lcId[category] = id;
    if (category == kCtype) g_lcCodePage = codePage;
    if (category == kCollate) g_lcCollateCodePage = codePage;
    const int failed = category == kCtype ? InitCtype() : entry.init();
    if (failed) {
        entry.locale = oldLocale;
        gamecrt::free(copy);
        g_lcHandle[category] = oldHandle;
        g_lcCodePage = oldCodePage;
        return nullptr;
    }
    if (oldLocale != StaticC()) gamecrt::free(oldLocale);
    return entry.locale;
}

// --- _setmbcp. ---------------------------------------------------------------------------------------

// getSystemCP (006E9FDD): _MB_CP_OEM, _MB_CP_ANSI and _MB_CP_LOCALE to a code page.
unsigned SystemCodePage(int codePage) {
    g_mbSystemSet = 1;
    if (codePage == -2) return gameapi::GetOEMCP();
    if (codePage == -3) return gameapi::GetACP();
    if (codePage == -4) return g_lcCodePage;
    g_mbSystemSet = 0;
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
    std::memset(g_mbCtype, 0, kMbFlagsSize);
    g_mbCodePage = 0;
    g_isMbCodePage = 0;
    g_mbLcid = 0;
    g_mbUlInfo[0] = g_mbUlInfo[1] = g_mbUlInfo[2] = 0;
}

// setSBUpLow (006EA083): the single-byte case flags and _mbcasemap (each byte's other case), from the
// code page's classes and case maps; ASCII without the code page.
void SetSingleByteCase() {
    CPINFO info;
    if (gameapi::GetCPInfo(g_mbCodePage, &info) == TRUE) {
        char bytes[256];
        for (unsigned i = 0; i < 256; ++i) bytes[i] = static_cast<char>(i);
        bytes[0] = ' ';
        for (const BYTE* lead = info.LeadByte; lead[0] != 0; lead += 2)
            for (unsigned c = lead[0]; c <= lead[1]; ++c) bytes[c] = ' ';
        WORD types[256] = {};
        char lower[256] = {};
        char upper[256] = {};
        StringTypeA(CT_CTYPE1, bytes, 256, types, g_mbCodePage, false);
        MapStringA(g_mbLcid, LCMAP_LOWERCASE, bytes, 256, lower, 256, g_mbCodePage);
        MapStringA(g_mbLcid, LCMAP_UPPERCASE, bytes, 256, upper, 256, g_mbCodePage);
        for (unsigned i = 0; i < 256; ++i) {
            if (types[i] & C1_UPPER) {
                g_mbCtype[i + 1] |= kSbUpper;
                g_mbCaseMap[i] = static_cast<unsigned char>(lower[i]);
            } else if (types[i] & C1_LOWER) {
                g_mbCtype[i + 1] |= kSbLower;
                g_mbCaseMap[i] = static_cast<unsigned char>(upper[i]);
            } else {
                g_mbCaseMap[i] = 0;
            }
        }
        return;
    }
    for (unsigned i = 0; i < 256; ++i) {
        if (i >= 'A' && i <= 'Z') {
            g_mbCtype[i + 1] |= kSbUpper;
            g_mbCaseMap[i] = static_cast<unsigned char>(i + 0x20);
        } else if (i >= 'a' && i <= 'z') {
            g_mbCtype[i + 1] |= kSbLower;
            g_mbCaseMap[i] = static_cast<unsigned char>(i - 0x20);
        } else {
            g_mbCaseMap[i] = 0;
        }
    }
}

} // namespace

// --- __init_ctype (006F47FB). ----------------------------------------------------------------------------

int InitCtype() {
    if (g_lcHandle[kCtype] == 0) {
        g_pctype = kCtypeC + 1;
        g_pwctype = kCtypeC + 1;
        gamecrt::free(g_ctypeBlock);
        gamecrt::free(g_wideCtypeBlock);
        g_ctypeBlock = nullptr;
        g_wideCtypeBlock = nullptr;
        return 0;
    }
    // Not reached by setlocale (the code page is set with the handle): __getlocaleinfo's LC_INT_TYPE.
    if (g_lcCodePage == 0) {
        char number[128];
        if (!gameapi::GetLocaleInfoA(g_lcId[kCtype].language, LOCALE_IDEFAULTANSICODEPAGE, number, sizeof number))
            return 1;
        g_lcCodePage = static_cast<unsigned>(Atol(number));
    }
    auto* classes = static_cast<unsigned short*>(gamecrt::malloc(0x202));
    auto* wideClasses = static_cast<unsigned short*>(gamecrt::malloc(0x202));
    auto* bytes = static_cast<unsigned char*>(gamecrt::malloc(0x101));
    auto* wide = static_cast<wchar_t*>(gamecrt::malloc(0x202));
    bool built = false;
    CPINFO info;
    if (classes && wideClasses && bytes && wide) {
        for (int i = 0; i < 256; ++i) bytes[i] = static_cast<unsigned char>(i);
        if (gameapi::GetCPInfo(g_lcCodePage, &info) && info.MaxCharSize < 3) {
            g_mbCurMax = static_cast<unsigned short>(info.MaxCharSize);
            if (g_mbCurMax > 1)
                for (const BYTE* lead = info.LeadByte; lead[0] != 0 && lead[1] != 0; lead += 2)
                    for (unsigned c = lead[0]; c <= lead[1]; ++c) bytes[c] = 0;
            if (StringTypeA(CT_CTYPE1, reinterpret_cast<const char*>(bytes), 256, classes + 1, 0, false)) {
                classes[0] = 0;
                for (int i = 0; i < 256; ++i) wide[i] = static_cast<wchar_t>(i);
                if (gameapi::GetStringTypeW(CT_CTYPE1, wide, 256, wideClasses + 1)) {
                    wideClasses[0] = 0;
                    if (g_mbCurMax > 1)
                        for (const BYTE* lead = info.LeadByte; lead[0] != 0 && lead[1] != 0; lead += 2)
                            for (unsigned c = lead[0]; c <= lead[1]; ++c) classes[c + 1] = 0x8000;   // _LEADBYTE
                    g_pctype = classes + 1;
                    g_pwctype = wideClasses + 1;
                    if (g_ctypeBlock) gamecrt::free(g_ctypeBlock);
                    g_ctypeBlock = classes;
                    if (g_wideCtypeBlock) gamecrt::free(g_wideCtypeBlock);
                    g_wideCtypeBlock = wideClasses;
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
    if (page == g_mbCodePage) return 0;
    if (page == 0) {
        SetSingleByte();
        SetSingleByteCase();
        return 0;
    }
    for (int i = 0; i < kCodePageInfos; ++i) {
        const CodePageInfo& known = kCodePageTable[i];
        if (known.codePage != page) continue;
        std::memset(g_mbCtype, 0, kMbFlagsSize);
        // The ranges are read as VC6 reads them: pairs until a zero (a full row runs into the next).
        for (int kind = 0; kind < 4; ++kind)
            for (const unsigned char* range = known.ranges[kind]; range[0] != 0 && range[1] != 0; range += 2)
                for (unsigned c = range[0]; c <= range[1]; ++c) g_mbCtype[c + 1] |= kClassFlags[kind];
        g_isMbCodePage = 1;
        g_mbCodePage = page;
        g_mbLcid = CodePageLcid(page);
        g_mbUlInfo[0] = known.ulInfo[0];
        g_mbUlInfo[1] = known.ulInfo[1];
        g_mbUlInfo[2] = known.ulInfo[2];
        SetSingleByteCase();
        return 0;
    }
    CPINFO info;
    if (gameapi::GetCPInfo(page, &info) == TRUE) {
        g_mbLcid = 0;
        std::memset(g_mbCtype, 0, kMbFlagsSize);
        g_mbCodePage = page;
        if (info.MaxCharSize < 2) {
            g_isMbCodePage = 0;
        } else {
            for (const BYTE* lead = info.LeadByte; lead[0] != 0 && lead[1] != 0; lead += 2)
                for (unsigned c = lead[0]; c <= lead[1]; ++c) g_mbCtype[c + 1] |= kMbLead;
            for (unsigned c = 1; c < 0xFF; ++c) g_mbCtype[c + 1] |= kMbTrail;
            g_mbLcid = CodePageLcid(page);
            g_isMbCodePage = 1;
        }
        g_mbUlInfo[0] = g_mbUlInfo[1] = g_mbUlInfo[2] = 0;
        SetSingleByteCase();
        return 0;
    }
    if (!g_mbSystemSet) return -1;
    SetSingleByte();
    SetSingleByteCase();
    return 0;
}
EI_REWRITTEN(0x006E9E30, libcrt::Setmbcp);

void InitMultibyteTable() {
    if (g_mbTableReady == 0) {
        Setmbcp(-3);
        g_mbTableReady = 1;
    }
}

// --- _ismbcdigit (006ECE1A), _ismbcspace (006ECEA9). ------------------------------------------------------

namespace {

// A byte by its _pctype entry (_isctype's answer too, under a multibyte code page); a double-byte
// character (lead byte high) by GetStringTypeW in the multibyte code page, when it has one.
// EI_UTF8 (text_case.h): a byte past ASCII is part of a UTF-8 sequence and has no class.
int IsMultibyteClass(unsigned c, WORD mask) {
    if constexpr (EI_UTF8 != 0) {
        if (c >= 0x80) return 0;
    }
    if (c < 0x100) return g_pctype[c] & mask & 0xFF;
    if (!g_isMbCodePage) return 0;
    const char bytes[2] = {static_cast<char>(c >> 8), static_cast<char>(c)};
    WORD types[2] = {};
    if (StringTypeA(CT_CTYPE1, bytes, 2, types, g_mbCodePage, true) && types[1] == 0 && (types[0] & mask)) return 1;
    return 0;
}

} // namespace

int __cdecl Ismbcdigit(unsigned c) { return IsMultibyteClass(c, C1_DIGIT); }
EI_REWRITTEN(0x006ECE1A, libcrt::Ismbcdigit);

int __cdecl Ismbcspace(unsigned c) { return IsMultibyteClass(c, C1_SPACE); }
EI_REWRITTEN(0x006ECEA9, libcrt::Ismbcspace);

} // namespace libcrt
