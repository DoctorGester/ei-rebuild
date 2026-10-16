// Never-set bytes zeroed in the original (harness_leftovers.h; tickets SV7b3, SV7b2, SV7d3, SV7d4, S17a, SV7b7,
// SV7b6a).
#include "harness_leftovers.h"

#include "../../src/game/class_templates.h"
#include "../../src/game/map_object_figures.h"
#include "../../src/game/originals_units.h"
#include "../../src/game/unit.h"
#include "../../src/game/unit_ai.h"
#include "../../src/hook/log.h"
#include "ei/hook.h"
#include "lib/crt.h"

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
constexpr std::uintptr_t kOperatorNew = 0x007005AF;
constexpr std::uintptr_t kSuspectionConstruct = 0x004BA610;   // ScriptSuspectionModeConstruct
constexpr std::uintptr_t kFearConstruct = 0x005D2850;         // FearMode::Construct
constexpr std::uintptr_t kTemplateConstruct = 0x004A6580;     // ClassTemplate::Construct
constexpr std::uintptr_t kObject3DConstruct = 0x00515BD0;     // CObject3D::Construct

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

// A server or map object, or an AI mode the original constructs inline: the block zeroed when allocated.
void* __cdecl ZeroedNew(std::size_t size) {
    void* block = gamecrt::operator_new(size);
    if (block) std::memset(block, 0, size);
    return block;
}

// The Suspection and Fear modes the original constructs with a call: their fields zeroed first (the
// vtable and the count, which the callers set, stay).
using SuspectionConstructFn = SuspectionMode*(__fastcall*)(SuspectionMode*);
SuspectionMode* __fastcall ZeroedSuspectionConstruct(SuspectionMode* mode) {
    mode->ClearFields();
    return reinterpret_cast<SuspectionConstructFn>(kSuspectionConstruct)(mode);
}
using FearConstructFn = FearMode*(__fastcall*)(FearMode*, void*, int);
FearMode* __fastcall ZeroedFearConstruct(FearMode* mode, void*, int flag) {
    mode->ClearFields();
    return reinterpret_cast<FearConstructFn>(kFearConstruct)(mode, nullptr, flag);
}

// A class template: the dword after its nine weights (+0x34, archived with them) zeroed first.
using TemplateConstructFn = ClassTemplate*(__fastcall*)(ClassTemplate*);
ClassTemplate* __fastcall ZeroedTemplateConstruct(ClassTemplate* t) {
    std::memset(t->weightsTail, 0, sizeof(t->weightsTail));
    return reinterpret_cast<TemplateConstructFn>(kTemplateConstruct)(t);
}

// CMapObject::Construct's call of CObject3D::Construct: the view position, which the constructor
// leaves and the draw sorts read for objects listed only for their shadow, starts at zero as in ours
// (S17a; zeroed after the construction).
CObject3D* __fastcall ZeroedViewConstruct(CObject3D* object) {
    CObject3D* result = reinterpret_cast<CObject3D*(__fastcall*)(CObject3D*)>(kObject3DConstruct)(object);
    std::memset(&object->viewPosition, 0, sizeof(object->viewPosition));
    return result;
}

// The families, one EI_LEFTOVERS bit each.
enum Family {
    kStatsCopies, kServerObjects, kMapObjects, kSuspection, kFear, kTemplates, kViewPositions, kParticleSources,
    kLights, kFamilies
};
const char* const kFamilyNotes[kFamilies] = {
    "a unit statistics stack copy zeroed first, SV7b3",
    "a server object allocated zeroed, SV7b2 / SV7d3",
    "a map object allocated zeroed, SV7b2",
    "a Suspection mode's point and level zeroed first, SV7d4",
    "a Fear mode's fields zeroed first, SV7b2",
    "a class template's weights tail zeroed first, SV7b2",
    "a new 3D object's view position zeroed, S17a",
    "a particle source allocated zeroed, SV7b7",
    "a light allocated zeroed, SV7b6a",
};
// The families patched only in runs without hooks (EI_NO_HOOKS=1). Their sites all sit in functions ours
// replaces (NewObjectImpl, CreateObject, AddSuspectionMode, AddFearMode, ApplyBehaviour, SSCCmd::Execute,
// the party manager's builder, CreateClassTemplate, NewParticleSource, the light manager's NewObjectImpl), and
// ours zeroes the same blocks; patched in a hooked
// run they would make InstallHooks leave those rewrites out (a harness patch in the body), so the
// original would run in their place (SSCCmd::Execute without addon's commands on the vanilla install).
constexpr unsigned kNoHooksOnly = 1u << kServerObjects | 1u << kMapObjects | 1u << kSuspection | 1u << kFear |
                                  1u << kTemplates | 1u << kParticleSources | 1u << kLights;

struct Site {
    std::uintptr_t call;     // a CALL rel32
    std::uintptr_t target;   // what it calls
    const void* zeroed;      // the harness function that zeroes the block first
    Family family;
};
const void* const kNew = reinterpret_cast<const void*>(&ZeroedNew);
const void* const kSuspectionZeroed = reinterpret_cast<const void*>(&ZeroedSuspectionConstruct);
const void* const kFearZeroed = reinterpret_cast<const void*>(&ZeroedFearConstruct);
const void* const kTemplateZeroed = reinterpret_cast<const void*>(&ZeroedTemplateConstruct);
const Site kSites[] = {
    {0x0051363E, kVitalsReset, reinterpret_cast<const void*>(&ZeroedVitalsReset), kStatsCopies},   // GiveDefaults 00513630
    {0x00513679, kStatsReset, reinterpret_cast<const void*>(&ZeroedStatsReset), kStatsCopies},
    {0x00513B8C, kVitalsReset, reinterpret_cast<const void*>(&ZeroedVitalsReset), kStatsCopies},   // ApplyPrototype 00513B60
    {0x00513BC9, kStatsReset, reinterpret_cast<const void*>(&ZeroedStatsReset), kStatsCopies},
    {0x00513776, kVitalsReset, reinterpret_cast<const void*>(&ZeroedVitalsReset), kStatsCopies},   // BuildFromPrototype 005136D0
    {0x0051377D, kStatsReset, reinterpret_cast<const void*>(&ZeroedStatsReset), kStatsCopies},
    {0x0052EB81, kVitalsReset, reinterpret_cast<const void*>(&ZeroedVitalsReset), kStatsCopies},   // CUnit::TakePrototype 0052EB70
    {0x0052EB8A, kStatsReset, reinterpret_cast<const void*>(&ZeroedStatsReset), kStatsCopies},
    {0x0050E9C5, kBuildFromPrototype, reinterpret_cast<const void*>(&ZeroedVitalsBuild), kStatsCopies},   // CMapObject::TakePrototype
    {0x00662901, kPartyRecordConstruct, reinterpret_cast<const void*>(&ZeroedRecordConstruct), kStatsCopies},   // CParty::AddMember
    // CServerSpecificManager::NewObjectImpl 004D6970: the player server, the 3D object server,
    // CMapObjectServer, CUnitServer (its waitFrame +0x93C, which ResolveBlocker reads) and the torch spec.
    {0x004D69A2, kOperatorNew, kNew, kServerObjects},
    {0x004D69D7, kOperatorNew, kNew, kServerObjects},
    {0x004D6A13, kOperatorNew, kNew, kServerObjects},
    {0x004D6A4F, kOperatorNew, kNew, kServerObjects},
    {0x004D6A88, kOperatorNew, kNew, kServerObjects},
    // CMapObjectManager::CreateObject 00512730: units (0x32..0x34 share one call), trees, buildings,
    // stones, stuff, the fake unit, torches, magic traps, levers, 0x300D and trap objects.
    {0x00512772, kOperatorNew, kNew, kMapObjects},
    {0x005127AB, kOperatorNew, kNew, kMapObjects},
    {0x005127E4, kOperatorNew, kNew, kMapObjects},
    {0x0051281D, kOperatorNew, kNew, kMapObjects},
    {0x00512856, kOperatorNew, kNew, kMapObjects},
    {0x0051288F, kOperatorNew, kNew, kMapObjects},
    {0x005128DF, kOperatorNew, kNew, kMapObjects},
    {0x00512918, kOperatorNew, kNew, kMapObjects},
    {0x00512951, kOperatorNew, kNew, kMapObjects},
    {0x00512997, kOperatorNew, kNew, kMapObjects},
    {0x005129C9, kOperatorNew, kNew, kMapObjects},
    // Suspection modes: AIBrain::AddSuspectionMode 005D63B0 (inline), ApplyBehaviour 005D6630 and the
    // script's UMAg / UMStandard / UMSuspection (SSCCmd::Execute).
    {0x005D63B5, kOperatorNew, kNew, kSuspection},
    {0x005D6ADB, kSuspectionConstruct, kSuspectionZeroed, kSuspection},
    {0x004B7C38, kSuspectionConstruct, kSuspectionZeroed, kSuspection},
    {0x004B7CE4, kSuspectionConstruct, kSuspectionZeroed, kSuspection},
    {0x004B7D9B, kSuspectionConstruct, kSuspectionZeroed, kSuspection},
    {0x004B7E50, kSuspectionConstruct, kSuspectionZeroed, kSuspection},
    {0x004B7F13, kSuspectionConstruct, kSuspectionZeroed, kSuspection},
    {0x004B80CA, kSuspectionConstruct, kSuspectionZeroed, kSuspection},
    // Fear modes: AIBrain::AddFearMode 005D6480 (inline), ApplyBehaviour, the script's UMAg / UMFear.
    {0x005D6485, kOperatorNew, kNew, kFear},
    {0x005D6B2F, kFearConstruct, kFearZeroed, kFear},
    {0x005D6B86, kFearConstruct, kFearZeroed, kFear},
    {0x005D6BDD, kFearConstruct, kFearZeroed, kFear},
    {0x004B7C04, kFearConstruct, kFearZeroed, kFear},
    {0x004B7CB0, kFearConstruct, kFearZeroed, kFear},
    {0x004B7D6A, kFearConstruct, kFearZeroed, kFear},
    {0x004B7E1F, kFearConstruct, kFearZeroed, kFear},
    {0x004B8068, kFearConstruct, kFearZeroed, kFear},
    // Class templates: the party manager's builder 004A7630 (nine) and CreateClassTemplate 00457850.
    {0x004A7688, kTemplateConstruct, kTemplateZeroed, kTemplates},
    {0x004A7776, kTemplateConstruct, kTemplateZeroed, kTemplates},
    {0x004A7860, kTemplateConstruct, kTemplateZeroed, kTemplates},
    {0x004A794B, kTemplateConstruct, kTemplateZeroed, kTemplates},
    {0x004A7A39, kTemplateConstruct, kTemplateZeroed, kTemplates},
    {0x004A7B2A, kTemplateConstruct, kTemplateZeroed, kTemplates},
    {0x004A7C14, kTemplateConstruct, kTemplateZeroed, kTemplates},
    {0x004A7CFE, kTemplateConstruct, kTemplateZeroed, kTemplates},
    {0x004A7DDA, kTemplateConstruct, kTemplateZeroed, kTemplates},
    {0x00457886, kTemplateConstruct, kTemplateZeroed, kTemplates},
    // CMapObject::Construct 0050E2D0's call of CObject3D::Construct (S17a).
    {0x0050E2F2, kObject3DConstruct, reinterpret_cast<const void*>(&ZeroedViewConstruct), kViewPositions},
    // NewParticleSource 004981B0 (SV7b7): the first Tick reads the emission point (+0x98), which nothing sets
    // before Attach.
    {0x004981CD, kOperatorNew, kNew, kParticleSources},
    // CLightFactory::NewObjectImpl 0068DA40 (SV7b6a): nothing sets a directional light's position (+0x1C), whose z the
    // terrain's sun lighting reads under water (ShadeSun 006C36B0).
    {0x0068DA7C, kOperatorNew, kNew, kLights},
};

bool Redirect(const Site& site) {
    auto* p = reinterpret_cast<unsigned char*>(site.call);
    std::int32_t rel = 0;
    std::memcpy(&rel, p + 1, 4);
    if (p[0] != 0xE8 || site.call + 5 + static_cast<std::uintptr_t>(rel) != site.target) {
        Log("harness: %08X is not a call of %08X; its block is left as memory held it", unsigned(site.call),
            unsigned(site.target));
        return false;
    }
    rel = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(site.zeroed) - (site.call + 5));
    ei::NoteHarnessPatch(site.call, 5, kFamilyNotes[site.family]);
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
        Log("harness: never-set blocks: the rewrites zero them (no original code in the image)");
        return;
    }
    // EI_LEFTOVERS=<hex mask>: only the families whose bit is set (bit i: Family i), to find which one a
    // difference comes from; default all.
    unsigned mask = ~0u;
    char value[16] = "";
    if (GetEnvironmentVariableA("EI_LEFTOVERS", value, sizeof(value))) mask = std::strtoul(value, nullptr, 16);
    char hooks[8] = "";
    GetEnvironmentVariableA("EI_NO_HOOKS", hooks, sizeof(hooks));
    if (hooks[0] != '1') mask &= ~kNoHooksOnly;
    int patched = 0;
    for (int i = 0; i < static_cast<int>(sizeof(kSites) / sizeof(kSites[0])); ++i)
        if ((mask >> kSites[i].family & 1) && Redirect(kSites[i])) ++patched;
    Log("harness: never-set blocks zeroed first (%d of %d original sites%s)", patched,
        static_cast<int>(sizeof(kSites) / sizeof(kSites[0])),
        hooks[0] == '1' ? "" : "; the heap-block families only without hooks, where ours zeroes them");
}

} // namespace ei::verify::leftovers
