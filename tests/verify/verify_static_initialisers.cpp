// Checks for static initialisers ported under A-E2: the debug panel's constructor (00444740,
// src/game/string_map.cpp), the options object's (00458870, src/game/options_construct.cpp),
// CSysInfo's translation unit (004DDCB0-004DE87C, src/game/sys_info.cpp) and the weather's noise
// curves (004E3980, src/game/weather_noise.cpp); and under A-Z1: the constructor of the spells'
// footprint cell set (0067E4E0, src/game/ai_footprint.cpp).
//
// The originals run among the game's static initialisers, before the hooks are installed, so no
// wrapper ever sees them. What they built is still untouched at the entry of WinMain: each check
// below runs once at that point (EI_STARTUP_CHECK), builds the same thing with our code into
// storage of its own and compares the two. Every game start reaches them.
//
//   - the debug panel: the vtable and an empty map (a header node linked to itself). The map's
//     compare byte is stack garbage in the original and is not compared;
//   - the options: all 0x26C bytes, the eighteen strings by content;
//   - CSysInfo: the engine object's fields and flags, the five texts by content, the whole version
//     and system records, the memory record's size and totals. Not compared, because they change
//     between two calls: the memory load, the available and page file amounts. The processor
//     globals (007CAED0) our collector writes again are put back as the original left them (the
//     APIC id in CPUID's EBX depends on the core the thread runs on). The four constants are
//     computed again over the originals' values and compared by bits; the two initialisers that
//     only register exit handlers (004DDD4E, 004DE87C) are not run: only the guard bit is read;
//   - the noise curves: all five, every byte. They are made of rand's numbers: the check seeds the
//     C runtime's generator with 1 (its state before the first draw of the thread), builds five
//     curves, and puts the state back. This compares equal only while nothing draws from rand
//     before the curves' initialiser; the check also reports whether the 640 draws account for the
//     whole state at the entry of WinMain;
//   - the spells' footprint cell set (007C8478, built by 0067E4C0; nothing has cast a wall or a
//     cloud yet): the array's four fields and the hashed byte, the only bytes the constructor
//     writes. Ours runs on storage filled with a pattern, which every other byte must keep; the
//     original's other bytes (the buckets) must still be the image's zeros.
#include "verify.h"

#include "../../src/game/ai_map.h"
#include "../../src/game/options.h"
#include "../../src/game/sys_info.h"
#include "../../src/game/weather_noise.h"
#include "../../src/lib/crt.h"
#include "../../src/lib/stlp.h"

#include <cstdint>
#include <cstring>
#include <new>
#include <string>

namespace strmap {
void* __fastcall StringTableConstruct(void* self);   // string_map.cpp
}

namespace {

// ---- The debug panel -------------------------------------------------------------------------------

struct DebugPanelView {
    void* vtable;
    stlp::map<stlp::string, stlp::string> entries;
    std::uint8_t rest[0x44 - 0x10];
};
static_assert(sizeof(DebugPanelView) == 0x44);

bool EmptyPanel(const DebugPanelView* panel) {
    const auto* header = panel->entries.end();
    return panel->vtable == reinterpret_cast<void*>(0x0073BFA4) && panel->entries.size() == 0 && header &&
           (header->color & 0xFF) == 0 && !header->parent && header->left == header && header->right == header;
}

void DebugPanelStartupCheck() {
    const auto* theirs = reinterpret_cast<const DebugPanelView*>(0x0079B930);
    alignas(4) static std::uint8_t storage[sizeof(DebugPanelView)];
    auto* ours = reinterpret_cast<DebugPanelView*>(storage);
    const void* result = strmap::StringTableConstruct(ours);
    ei::verify::Check("static initialiser: debug panel 00444740", true, result == ours && EmptyPanel(theirs) && EmptyPanel(ours),
                      "theirs: vtable %p, %u entries; ours: vtable %p, %u entries", theirs->vtable,
                      static_cast<unsigned>(theirs->entries.size()), ours->vtable, static_cast<unsigned>(ours->entries.size()));
    ours->entries.Destroy();
}
EI_STARTUP_CHECK(DebugPanelStartupCheck);

// ---- The options -----------------------------------------------------------------------------------

bool IsOptionString(std::size_t offset) {
    for (std::size_t s : options::kStringFields)
        if (s == offset) return true;
    return false;
}

void OptionsStartupCheck() {
    using options::GameOptions;
    alignas(4) static std::uint8_t storage[sizeof(GameOptions)];
    GameOptions* ours = new (storage) GameOptions;
    const auto* theirs = reinterpret_cast<const std::uint8_t*>(options::TheOptions());
    int wrong = 0;
    std::size_t first = 0;
    for (std::size_t o = 0; o < sizeof(GameOptions); o += 4) {
        bool same;
        if (IsOptionString(o))
            same = std::strcmp(*reinterpret_cast<const char* const*>(theirs + o), *reinterpret_cast<const char* const*>(storage + o)) == 0;
        else
            same = std::memcmp(theirs + o, storage + o, 4) == 0;
        if (same) continue;
        if (!wrong) first = o;
        ++wrong;
    }
    std::uint32_t theirBits = 0, ourBits = 0;
    std::memcpy(&theirBits, theirs + first, 4);
    std::memcpy(&ourBits, storage + first, 4);
    // An object that is all zeros would also be "equal" to nothing we build: the resolution is 800.
    ei::verify::Check("static initialiser: options 00458870", true, wrong == 0 && ours->resolutionX_ == 800,
                      "%d fields differ, the first at +0x%X (%08X / ours %08X)", wrong, static_cast<unsigned>(first), theirBits, ourBits);
    ours->~GameOptions();
}
EI_STARTUP_CHECK(OptionsStartupCheck);

// ---- CSysInfo --------------------------------------------------------------------------------------

std::uint32_t Bits(float value) {
    std::uint32_t bits;
    std::memcpy(&bits, &value, 4);
    return bits;
}

void SysInfoStartupCheck() {
    const CSysInfo* theirs = TheSysInfo();
    alignas(4) static std::uint8_t storage[sizeof(CSysInfo)];
    auto* ours = reinterpret_cast<CSysInfo*>(storage);
    const ProcessorInfo processor = *TheProcessorInfo();
    const CSysInfo* result = ours->Construct();
    *TheProcessorInfo() = processor;

    const bool object = result == ours && std::memcmp(theirs, ours, sizeof(void*)) == 0 && theirs->refCount_ == ours->refCount_ &&
                        theirs->flags_ == ours->flags_ && theirs->id_ == ours->id_ && theirs->type_ == ours->type_ &&
                        theirs->owner_ == ours->owner_ && theirs->unknown_18 == ours->unknown_18;
    const bool texts = std::strcmp(theirs->computerName_, ours->computerName_) == 0 &&
                       std::strcmp(theirs->userName_, ours->userName_) == 0 &&
                       std::strcmp(theirs->systemName_, ours->systemName_) == 0 &&
                       std::strcmp(theirs->systemVersion_, ours->systemVersion_) == 0 &&
                       std::strcmp(theirs->servicePack_, ours->servicePack_) == 0;
    const bool version = std::memcmp(&theirs->version_, &ours->version_, sizeof(ours->version_)) == 0;
    const bool system = std::memcmp(&theirs->system_, &ours->system_, sizeof(ours->system_)) == 0;
    const bool memory = theirs->memory_.dwLength == ours->memory_.dwLength && theirs->memory_.dwTotalPhys == ours->memory_.dwTotalPhys &&
                        theirs->memory_.dwTotalVirtual == ours->memory_.dwTotalVirtual;
    // The original's texts must hold something for the comparison to mean anything.
    const bool filled = *static_cast<const char*>(theirs->systemName_) != 0 && theirs->version_.dwOSVersionInfoSize == sizeof(OSVERSIONINFOA);
    ei::verify::Check("static initialiser: CSysInfo 004DDD6F / 004DDF28", true, object && texts && version && system && memory && filled,
                      "object %d (flags %X / %X) texts %d ('%s' '%s' / '%s' '%s') version %d system %d memory %d filled %d", object,
                      theirs->flags_, ours->flags_, texts, static_cast<const char*>(theirs->systemName_),
                      static_cast<const char*>(theirs->systemVersion_), static_cast<const char*>(ours->systemName_),
                      static_cast<const char*>(ours->systemVersion_), version, system, memory, filled);
    SysInfoDestruct(ours);
}
EI_STARTUP_CHECK(SysInfoStartupCheck);

void SysInfoConstantsStartupCheck() {
    static constexpr std::uintptr_t kConstants[] = {0x007AB8F0, 0x007ABA00, 0x007ABA04, 0x007ABA08};
    void(__cdecl* const initialisers[])() = {SysInfoInitTwoPi, SysInfoInitHalfPi, SysInfoInitQuarterPi, SysInfoInitInversePi};
    std::uint32_t theirs[4], ours[4];
    bool same = true;
    for (int i = 0; i < 4; ++i) {
        float* constant = reinterpret_cast<float*>(kConstants[i]);
        theirs[i] = Bits(*constant);
        *constant = 0.0f;
        initialisers[i]();
        ours[i] = Bits(*constant);
        std::memcpy(constant, &theirs[i], 4);
        same = same && theirs[i] == ours[i] && theirs[i] != 0;
    }
    const bool guard = (*reinterpret_cast<const std::uint8_t*>(0x007CAEE4) & 1) != 0;
    ei::verify::Check("static initialiser: CSysInfo's constants 004DDCBA..004DDD19, guard 004DE855", true, same && guard,
                      "%08X %08X %08X %08X / ours %08X %08X %08X %08X, guard %d", theirs[0], theirs[1], theirs[2], theirs[3], ours[0],
                      ours[1], ours[2], ours[3], guard);
}
EI_STARTUP_CHECK(SysInfoConstantsStartupCheck);

// ---- The weather's noise curves --------------------------------------------------------------------

// The C runtime's per-thread data (_getptd); rand's state is the dword at +0x14.
__declspec(naked) std::uint8_t* __cdecl GameThreadData() EI_ORIGINAL(0x006F00D1)

void WeatherNoiseStartupCheck() {
    static WeatherNoiseCurve ours[kWeatherNoiseCurves];
    std::uint32_t* state = reinterpret_cast<std::uint32_t*>(GameThreadData() + 0x14);
    const std::uint32_t atStart = *state;
    *state = 1;
    BuildWeatherNoiseCurves(ours);
    const std::uint32_t afterCurves = *state;
    *state = atStart;

    int wrong = 0, first = -1;
    for (int i = 0; i < kWeatherNoiseCurves; ++i) {
        if (std::memcmp(WeatherNoiseCurveAt(i), &ours[i], sizeof(WeatherNoiseCurve)) == 0) continue;
        ++wrong;
        if (first < 0) first = i;
    }
    const int at = first < 0 ? 0 : first;
    const WeatherNoiseCurve* theirs = WeatherNoiseCurveAt(at);
    // Curves of zeros would compare equal to nothing we build; the first sample is never zero here.
    ei::verify::Check("static initialiser: weather noise curves 004E3980", true, wrong == 0 && Bits(theirs->samples[0]) != 0,
                      "%d of 5 curves differ, the first %d (sample 0 %08X / ours %08X, segment 0 cubic %08X / %08X); rand state at "
                      "WinMain %08X, after our 640 draws from seed 1 %08X",
                      wrong, first, Bits(theirs->samples[0]), Bits(ours[at].samples[0]), Bits(theirs->segments[0].cubic),
                      Bits(ours[at].segments[0].cubic), atStart, afterCurves);
}
EI_STARTUP_CHECK(WeatherNoiseStartupCheck);

// ---- The spells' footprint cell set ----------------------------------------------------------------

void FootprintSetStartupCheck() {
    constexpr std::uint8_t kPattern = 0xA5;
    const auto* theirs = reinterpret_cast<const FootprintCellSet*>(0x007C8478);
    alignas(4) static std::uint8_t storage[sizeof(FootprintCellSet)];
    std::memset(storage, kPattern, sizeof(storage));
    auto* ours = reinterpret_cast<FootprintCellSet*>(storage);
    const FootprintCellSet* result = ours->Construct();

    const bool written = theirs->cells.data == ours->cells.data && theirs->cells.size == ours->cells.size && theirs->cells.capacity == ours->cells.capacity &&
                         theirs->cells.growBy == ours->cells.growBy && theirs->hashed == ours->hashed;
    // Everything else: the pattern in ours, zeros in theirs.
    bool untouched = true;
    const auto* theirBytes = reinterpret_cast<const std::uint8_t*>(theirs);
    for (std::size_t o = offsetof(FootprintCellSet, buckets); o < sizeof(FootprintCellSet); ++o) {
        if (o == offsetof(FootprintCellSet, hashed)) continue;
        untouched = untouched && storage[o] == kPattern && theirBytes[o] == 0;
    }
    // An object of zeros would not have the hashed byte set.
    ei::verify::Check("static initialiser: footprint cell set 0067E4E0", true,
                      result == ours && written && untouched && theirs->hashed && !theirs->cells.data,
                      "theirs: cells %p count %d capacity %d growBy %d hashed %d; ours: cells %p count %d capacity %d growBy %d "
                      "hashed %d; other bytes untouched %d",
                      static_cast<const void*>(theirs->cells.data), theirs->cells.size, theirs->cells.capacity, theirs->cells.growBy, theirs->hashed,
                      static_cast<const void*>(ours->cells.data), ours->cells.size, ours->cells.capacity, ours->cells.growBy, ours->hashed, untouched);
}
EI_STARTUP_CHECK(FootprintSetStartupCheck);

} // namespace
