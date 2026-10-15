// Checks for the game CRT's locale set-up (src/lib/crt_locale.cpp, S4c).
// - _ismbcdigit / _ismbcspace: against the originals through their trampolines; the start-up check passes
//   every byte and some double-byte characters through their addresses, under the multibyte code page the
//   game has and under Japanese 932 (set and put back by the original _setmbcp).
// - _setmbcp and setlocale (the image's; the hook build keeps the exe's): at start-up, from the CRT state
//   the exe starts with (the "C" locale, its ANSI multibyte code page), each case runs the original, then
//   ours from the same state, and compares the state each leaves (the bytes of the multibyte tables, code
//   page and ids; the locale names, handles, LC_IDs, code pages, getqloc's cache, __mb_cur_max and the
//   contents of the class tables). The state is put back after each, and the heap blocks either made are
//   freed.
#include "verify.h"

#include "../../src/hook/log.h"
#include "../../src/lib/crt.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

using ei::verify::Check;

// --- _ismbcdigit / _ismbcspace. -----------------------------------------------------------------------------

__declspec(naked) int __cdecl IsmbcdigitAt(unsigned) EI_ORIGINAL(0x006ECE1A)
__declspec(naked) int __cdecl IsmbcspaceAt(unsigned) EI_ORIGINAL(0x006ECEA9)
__declspec(naked) int __cdecl OriginalSetmbcp(int) EI_ORIGINAL(0x006E9E30)
__declspec(naked) char* __cdecl OriginalSetlocale(int, const char*) EI_ORIGINAL(0x006EA234)

// 006ECE1A / 006ECEA9: PUSH EBP; MOV EBP,ESP; PUSH ECX; MOV ECX,[EBP+8] (7 bytes).
int(__cdecl* s_ismbcdigit)(unsigned);
int __cdecl IsmbcdigitVerify(unsigned c) {
    const int expected = s_ismbcdigit(c);
    const int actual = libcrt::Ismbcdigit(c);
    Check("_ismbcdigit", expected, actual, "%04X: %d, ours %d", c, expected, actual);
    return actual;
}
EI_VERIFY(0x006ECE1A, IsmbcdigitVerify, 7, s_ismbcdigit);

int(__cdecl* s_ismbcspace)(unsigned);
int __cdecl IsmbcspaceVerify(unsigned c) {
    const int expected = s_ismbcspace(c);
    const int actual = libcrt::Ismbcspace(c);
    Check("_ismbcspace", expected, actual, "%04X: %d, ours %d", c, expected, actual);
    return actual;
}
EI_VERIFY(0x006ECEA9, IsmbcspaceVerify, 7, s_ismbcspace);

void ClassBattery() {
    static const unsigned kDoubleByte[] = {0x100, 0x3000, 0x8140, 0x824F, 0x8258, 0x8260, 0x82A0, 0x889F,
                                           0x9FFC, 0xA1A1, 0xA3B0, 0xE040, 0xFFFF, 0x12345};
    for (unsigned c = 0; c < 0x100; ++c) {
        IsmbcdigitAt(c);
        IsmbcspaceAt(c);
    }
    for (unsigned c : kDoubleByte) {
        IsmbcdigitAt(c);
        IsmbcspaceAt(c);
    }
}

// --- The CRT state, saved and put back. ---------------------------------------------------------------------

struct Region {
    std::uintptr_t at;
    std::size_t size;
};

// _setmbcp's: _mbctype, _mbcasemap, __mbcodepage, __mbulinfo, __ismbcodepage, __mblcid, fSystemSet.
const Region kMultibyteRegions[] = {{0x007CC180, 0x101}, {0x007CC080, 0x100}, {0x007CC058, 4}, {0x007CC060, 12},
                                    {0x007CC06C, 4},     {0x007CC284, 4},     {0x007CAB20, 4}};
// setlocale's: __lc_category, _pctype / _pwctype, __mb_cur_max, __lc_handle, __lc_codepage and
// __lc_collate_cp, the class table blocks, __lc_id, getqloc's cached names, LC_ID and code page.
const Region kLocaleRegions[] = {{0x00797888, 72}, {0x00797DD8, 8},  {0x00797FE4, 4},     {0x007CABE8, 24},
                                 {0x007CAC00, 8},  {0x007CAC28, 8},  {0x007CAC30, 36},    {0x0079777C, 0x10C},
                                 {0x007CAB24, 12}};

template <std::size_t N>
std::vector<unsigned char> Save(const Region (&regions)[N]) {
    std::vector<unsigned char> bytes;
    for (const Region& r : regions) {
        const auto* p = reinterpret_cast<const unsigned char*>(r.at);
        bytes.insert(bytes.end(), p, p + r.size);
    }
    return bytes;
}

template <std::size_t N>
void Restore(const Region (&regions)[N], const std::vector<unsigned char>& bytes) {
    std::size_t offset = 0;
    for (const Region& r : regions) {
        std::memcpy(reinterpret_cast<void*>(r.at), bytes.data() + offset, r.size);
        offset += r.size;
    }
}

// The first byte where two saves differ, by address (0 when none).
template <std::size_t N>
std::uintptr_t FirstDifference(const Region (&regions)[N], const std::vector<unsigned char>& a,
                               const std::vector<unsigned char>& b) {
    std::size_t offset = 0;
    for (const Region& r : regions) {
        for (std::size_t i = 0; i < r.size; ++i)
            if (a[offset + i] != b[offset + i]) return r.at + i;
        offset += r.size;
    }
    return 0;
}

// --- _setmbcp. --------------------------------------------------------------------------------------------

unsigned& MbCodePage() { return *reinterpret_cast<unsigned*>(0x007CC058); }

void SetmbcpBattery() {
    static const int kCodePages[] = {-2, -3, -4, 0, 437, 850, 866, 874, 932, 936, 949, 950, 1250, 1251,
                                     1252, 1253, 1255, 1256, 1361, 65001, 12345, -1};
    const std::vector<unsigned char> start = Save(kMultibyteRegions);
    for (int fromZero = 0; fromZero < 2; ++fromZero) {
        for (int codePage : kCodePages) {
            Restore(kMultibyteRegions, start);
            if (fromZero) MbCodePage() = 0;
            const int expected = OriginalSetmbcp(codePage);
            const std::vector<unsigned char> original = Save(kMultibyteRegions);
            Restore(kMultibyteRegions, start);
            if (fromZero) MbCodePage() = 0;
            const int actual = libcrt::Setmbcp(codePage);
            const std::vector<unsigned char> ours = Save(kMultibyteRegions);
            Check("_setmbcp", expected, actual, "%d (from code page %s): %d, ours %d", codePage,
                  fromZero ? "0" : "as started", expected, actual);
            const std::uintptr_t at = FirstDifference(kMultibyteRegions, original, ours);
            Check("_setmbcp state", std::uintptr_t(0), at, "%d (from code page %s): first difference at %08X",
                  codePage, fromZero ? "0" : "as started", static_cast<unsigned>(at));
        }
    }
    Restore(kMultibyteRegions, start);
}

// --- setlocale. -------------------------------------------------------------------------------------------

struct LocaleCategory {
    const char* name;
    char* locale;
    void* init;
};
LocaleCategory* Categories() { return reinterpret_cast<LocaleCategory*>(0x00797888); }
const unsigned short* CtypeTable() { return *reinterpret_cast<const unsigned short* const*>(0x00797DD8); }
const unsigned short* WideCtypeTable() { return *reinterpret_cast<const unsigned short* const*>(0x00797DDC); }
unsigned short* CtypeBlock() { return *reinterpret_cast<unsigned short* const*>(0x007CAC28); }
unsigned short* WideCtypeBlock() { return *reinterpret_cast<unsigned short* const*>(0x007CAC2C); }
const char* StaticC() { return reinterpret_cast<const char*>(0x00797778); }

// What a setlocale leaves: the result and the state, pointers by what they point at.
struct LocaleState {
    std::string result;
    std::string names[6];
    std::vector<unsigned char> bytes;   // kLocaleRegions without the pointers (zeroed)
    std::vector<unsigned short> ctype;  // _pctype[-1..255], _pwctype[-1..255]
    bool blocksBehindTables = false;    // the heap blocks are the tables' (or both null with the static table)
};

std::string Text(const char* p) { return p ? std::string("'") + p + "'" : std::string("null"); }

LocaleState Capture(const char* result) {
    LocaleState state;
    state.result = Text(result);
    for (int i = 0; i < 6; ++i) state.names[i] = Text(Categories()[i].locale);
    state.bytes = Save(kLocaleRegions);
    // Zero the pointers: the category names (offset 0, 72 bytes: each entry's +4), the tables (72, 8) and
    // the blocks (72 + 8 + 4 + 24 + 8 = 116, 8).
    for (int i = 0; i < 6; ++i) std::memset(&state.bytes[i * 12 + 4], 0, 4);
    std::memset(&state.bytes[72], 0, 8);
    std::memset(&state.bytes[116], 0, 8);
    state.ctype.assign(CtypeTable() - 1, CtypeTable() + 256);
    state.ctype.insert(state.ctype.end(), WideCtypeTable() - 1, WideCtypeTable() + 256);
    state.blocksBehindTables = CtypeBlock() ? CtypeBlock() + 1 == CtypeTable() && WideCtypeBlock() + 1 == WideCtypeTable()
                                            : !WideCtypeBlock() && CtypeTable() == WideCtypeTable();
    return state;
}

// Frees what a run made: the category names and class tables that are not the start's.
void FreeMade(const std::vector<unsigned char>& start) {
    for (int i = 0; i < 6; ++i) {
        char* name = Categories()[i].locale;
        char* before;
        std::memcpy(&before, &start[i * 12 + 4], 4);
        if (name && name != before && name != StaticC()) gamecrt::free(name);
    }
    unsigned short* blocks[2];
    std::memcpy(blocks, &start[116], 8);
    if (CtypeBlock() && CtypeBlock() != blocks[0]) gamecrt::free(CtypeBlock());
    if (WideCtypeBlock() && WideCtypeBlock() != blocks[1]) gamecrt::free(WideCtypeBlock());
}

int s_localeCases;

void CompareLocale(const char* steps, const LocaleState& original, const LocaleState& ours) {
    ++s_localeCases;
    Check("setlocale", true, original.result == ours.result, "%s: %s, ours %s", steps, original.result.c_str(), ours.result.c_str());
    for (int i = 0; i < 6; ++i)
        Check("setlocale name", true, original.names[i] == ours.names[i], "%s: category %d %s, ours %s", steps, i,
              original.names[i].c_str(), ours.names[i].c_str());
    std::size_t at = 0;
    while (at < original.bytes.size() && original.bytes[at] == ours.bytes[at]) ++at;
    Check("setlocale state", original.bytes.size(), at, "%s: first difference at byte %u of the saved regions", steps,
          static_cast<unsigned>(at));
    std::size_t entry = 0;
    while (entry < original.ctype.size() && original.ctype[entry] == ours.ctype[entry]) ++entry;
    Check("setlocale class tables", original.ctype.size(), entry, "%s: first difference at entry %u (%04X, ours %04X)",
          steps, static_cast<unsigned>(entry), entry < original.ctype.size() ? original.ctype[entry] : 0,
          entry < ours.ctype.size() ? ours.ctype[entry] : 0);
    Check("setlocale class blocks", original.blocksBehindTables, ours.blocksBehindTables, "%s", steps);
}

// One case: each step (a category and a name, null for a query) run by the original, then by ours from the
// same state; compared after each step.
struct Step {
    int category;
    const char* name;
};

void LocaleCase(std::initializer_list<Step> steps) {
    const std::vector<unsigned char> start = Save(kLocaleRegions);
    std::vector<LocaleState> original;
    for (const Step& step : steps) original.push_back(Capture(OriginalSetlocale(step.category, step.name)));
    FreeMade(start);
    Restore(kLocaleRegions, start);
    std::string described;
    std::size_t i = 0;
    for (const Step& step : steps) {
        const LocaleState ours = Capture(libcrt::Setlocale(step.category, step.name));
        described += (described.empty() ? "" : ", ") + std::to_string(step.category) + " " + Text(step.name);
        CompareLocale(described.c_str(), original[i++], ours);
    }
    FreeMade(start);
    Restore(kLocaleRegions, start);
}

void SetlocaleBattery() {
    if (*reinterpret_cast<const unsigned*>(0x007CABF0) != 0 || CtypeBlock() || WideCtypeBlock()) {
        ei::Log("crt locale: LC_CTYPE is already set: setlocale not compared");
        return;
    }
    constexpr int kCtype = 2;
    static const char* const kNames[] = {
        "english", "English", "ENGLISH", "american", "american english", "english-us", "eng", "ENU", "enu",
        "english_united kingdom", "english_uk", "english_usa", "English_United States.1252", "english.1252",
        "english_us.437", "english_us.OCP", "english_us.ACP", "russian", "Russian_Russia.1251", "rus_rus",
        "german", "deu", "german_austria", "french_canada", "fra_can", "spanish", "chinese", "chs", "japanese",
        "Japanese_Japan.932", "korean_korea", "portuguese", "norwegian", "dutch_belgium", "swiss", "_usa",
        ".1251", ".OCP", ".", "", "C", "c", "xyz", "english_xyz", "xyz_usa", "english_usa.xyz", "english,1",
        "english_usa.1252,x", "LC_CTYPE=english"};
    for (const char* name : kNames) LocaleCase({{kCtype, name}});
    LocaleCase({{kCtype, nullptr}});
    LocaleCase({{kCtype, "english"}, {kCtype, nullptr}, {kCtype, "english"}, {kCtype, "russian"}, {kCtype, "C"},
                {kCtype, nullptr}});
    LocaleCase({{kCtype, "Japanese_Japan.932"}, {kCtype, "english"}, {kCtype, "xyz"}, {kCtype, "C"}});
    LocaleCase({{kCtype, ""}, {kCtype, "English_United States.1252"}, {kCtype, ""}});
    LocaleCase({{-1, "english"}, {6, "english"}, {1, nullptr}, {5, nullptr}});
}

void CrtLocaleStartupCheck() {
    ClassBattery();
    // Under a double-byte code page too (the original _setmbcp, then the state put back).
    const std::vector<unsigned char> multibyte = Save(kMultibyteRegions);
    OriginalSetmbcp(932);
    ClassBattery();
    Restore(kMultibyteRegions, multibyte);
    SetmbcpBattery();
    s_localeCases = 0;
    SetlocaleBattery();
    ei::Log("crt locale: %d setlocale step(s) compared", s_localeCases);
}
EI_STARTUP_CHECK(CrtLocaleStartupCheck);

} // namespace
