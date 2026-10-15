// Stack copies of unit statistics zeroed in the original (harness_leftovers.h; ticket SV7b3), and
// the view position of a new 3D object (S17a).
#include "harness_leftovers.h"

#include "../../src/game/map_object_figures.h"
#include "../../src/game/originals_units.h"
#include "../../src/game/unit.h"
#include "../../src/hook/log.h"
#include "ei/hook.h"

#include <windows.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace ei::verify::leftovers {

namespace {

constexpr std::uintptr_t kVitalsReset = 0x00523370;
constexpr std::uintptr_t kStatsReset = 0x005233E0;
constexpr std::uintptr_t kBuildFromPrototype = 0x005136D0;
constexpr std::uintptr_t kPartyRecordConstruct = 0x0065F5F0;
constexpr std::uintptr_t kObject3DConstruct = 0x00515BD0;

void* __fastcall ZeroedVitalsReset(UnitVitals* vitals) {
    std::memset(vitals, 0, sizeof(UnitVitals));
    return UnitVitalsReset(vitals);
}

int __fastcall ZeroedStatsReset(UnitStats* stats) {
    std::memset(stats, 0, sizeof(UnitStats));
    return UnitStatsResetOriginal(stats);
}

// CMapObject::TakePrototype's copy: the builder gets no statistics, so with a prototype it returns
// without touching the vitals, which are then packed.
int __fastcall ZeroedVitalsBuild(UnitStatsBuilder* builder, void*, UnitVitals* vitals, UnitStats* stats,
                                 const char* name) {
    if (vitals) std::memset(vitals, 0, sizeof(UnitVitals));
    return UnitStatsBuildFromPrototype(builder, nullptr, vitals, stats, name);
}

// CParty::AddMember's stack record, whose statistics BuildNpcStats fills (its reset leaves some).
PartyUnitRecord* __fastcall ZeroedRecordConstruct(PartyUnitRecord* record) {
    std::memset(record, 0, sizeof(PartyUnitRecord));
    return PartyUnitRecordConstructOriginal(record);
}

// CMapObject::Construct's call of CObject3D::Construct: the view position, which the constructor
// leaves and the draw sorts read for objects listed only for their shadow, starts at zero as in ours.
CObject3D* __fastcall ZeroedViewConstruct(CObject3D* object) {
    CObject3D* result = reinterpret_cast<CObject3D*(__fastcall*)(CObject3D*)>(kObject3DConstruct)(object);
    std::memset(&object->viewPosition, 0, sizeof(object->viewPosition));
    return result;
}

struct Site {
    std::uintptr_t call;     // a CALL rel32
    std::uintptr_t target;   // what it calls
    const void* zeroed;      // the harness function that zeroes the block
};
constexpr const char* kStatsNote = "a unit statistics stack copy zeroed first, SV7b3";
const Site kSites[] = {
    {0x0051363E, kVitalsReset, reinterpret_cast<const void*>(&ZeroedVitalsReset)},   // GiveDefaults 00513630
    {0x00513679, kStatsReset, reinterpret_cast<const void*>(&ZeroedStatsReset)},
    {0x00513B8C, kVitalsReset, reinterpret_cast<const void*>(&ZeroedVitalsReset)},   // ApplyPrototype 00513B60
    {0x00513BC9, kStatsReset, reinterpret_cast<const void*>(&ZeroedStatsReset)},
    {0x00513776, kVitalsReset, reinterpret_cast<const void*>(&ZeroedVitalsReset)},   // BuildFromPrototype 005136D0
    {0x0051377D, kStatsReset, reinterpret_cast<const void*>(&ZeroedStatsReset)},
    {0x0052EB81, kVitalsReset, reinterpret_cast<const void*>(&ZeroedVitalsReset)},   // CUnit::TakePrototype 0052EB70
    {0x0052EB8A, kStatsReset, reinterpret_cast<const void*>(&ZeroedStatsReset)},
    {0x0050E9C5, kBuildFromPrototype, reinterpret_cast<const void*>(&ZeroedVitalsBuild)},   // CMapObject::TakePrototype
    {0x00662901, kPartyRecordConstruct, reinterpret_cast<const void*>(&ZeroedRecordConstruct)},   // CParty::AddMember
    {0x0050E2F2, kObject3DConstruct, reinterpret_cast<const void*>(&ZeroedViewConstruct)},   // CMapObject::Construct
};
constexpr int kViewSite = 10;

bool Redirect(const Site& site, const char* note) {
    auto* p = reinterpret_cast<unsigned char*>(site.call);
    std::int32_t rel = 0;
    std::memcpy(&rel, p + 1, 4);
    if (p[0] != 0xE8 || site.call + 5 + static_cast<std::uintptr_t>(rel) != site.target) {
        Log("harness: %08X is not a call of %08X; left alone (%s)", unsigned(site.call), unsigned(site.target), note);
        return false;
    }
    rel = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(site.zeroed) - (site.call + 5));
    ei::NoteHarnessPatch(site.call, 5, note);
    DWORD old;
    VirtualProtect(p + 1, 4, PAGE_EXECUTE_READWRITE, &old);
    std::memcpy(p + 1, &rel, 4);
    VirtualProtect(p + 1, 4, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
    return true;
}

} // namespace

void Install() {
    if (ei::Standalone()) {
        Log("harness: unit statistics stack copies and new objects' view positions: the rewrites zero them (no "
            "original code in the image)");
        return;
    }
    // EI_LEFTOVERS=<hex mask>: only the sites whose bit is set (bit i: kSites[i]), to find which copy a
    // difference comes from; default all.
    unsigned mask = ~0u;
    char value[16] = "";
    if (GetEnvironmentVariableA("EI_LEFTOVERS", value, sizeof(value))) mask = std::strtoul(value, nullptr, 16);
    int patched = 0;
    for (int i = 0; i < kViewSite; ++i)
        if ((mask >> i & 1) && Redirect(kSites[i], kStatsNote)) ++patched;
    Log("harness: unit statistics stack copies zeroed first (%d of %d original sites)", patched, kViewSite);
    if ((mask >> kViewSite & 1) && Redirect(kSites[kViewSite], "a new 3D object's view position zeroed, S17a"))
        Log("harness: new 3D objects' view positions start at zero (CMapObject::Construct 0050E2F2)");
}

} // namespace ei::verify::leftovers
