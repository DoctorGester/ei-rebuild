// The client effects in the original as in ours (harness_client_effects.h; ticket R34a).
#include "harness_client_effects.h"

#include "exe_objects.h"
#include "harness_original.h"

#include "../../src/hook/log.h"
#include "ei/hook.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

namespace ei::verify::client_effects {

namespace {

using namespace ei::verify::exe;   // game.exe's generator and sectors: the patches run in the original's code only

constexpr std::uintptr_t kNext = 0x004667F0;                            // IsaacRandom::Next (plain RET)
constexpr std::uintptr_t kInit = 0x0049F150;                            // IsaacRandom::Init (plain RET)
constexpr std::uintptr_t kInitCall = 0x0055F165;                        // CWorld::Create's CALL of Init
constexpr std::uintptr_t kRand = 0x006E77F7;                            // the CRT's rand()
constexpr std::uintptr_t kEnsure = 0x006C3DD0;                          // SectorData::Ensure (wanted, terrain; RET 8)
constexpr std::uintptr_t kEnsureCalls[] = {0x006C8234, 0x006C84BC};     // Terrain::SectorAt's
constexpr std::uintptr_t kSetSunRelight = 0x006C908F;                   // SetSun: MOV EAX,[EBX+0x21C] (the interval)
constexpr std::uintptr_t kSetSunReturn = 0x006C90C1;                    // SetSun's epilogue (POP EDI ...)
constexpr std::uint32_t kStageBuilt = 2;                                // SectorStage
constexpr int kStageLit = 4;

// A function of the original, [start, end) up to the next function, and how many generator operands or rand()
// calls it has (tools: capstone's operands and a byte scan agree on every range; a range that does not hold as
// many is left alone).
struct Range {
    std::uintptr_t start, end;
    int count;
    const char* name;
};

// The client effects that drew from the ISAAC generator: their operands that address it.
const Range kIsaacFunctions[] = {
    {0x00466190, 0x00466270, 10, "NoiseKey::Randomize"},
    {0x004662C0, 0x004667F0, 12, "LightningSegment::SampleNoise"},
    {0x00466820, 0x00466CE0, 20, "LightningSegment::Init"},
    {0x00467A20, 0x00467AD0, 5, "CLightning: the draw's offset step"},
    {0x00467AD0, 0x00467E10, 2, "CLightning::DrawImpl"},
    {0x00467E10, 0x004680D0, 13, "CLightning::SetEndpoints"},
    {0x004680D0, 0x00468680, 5, "CLightning::Start"},
    {0x00485540, 0x00488940, 23, "CParticleSource::CreateForType"},
    {0x004889D0, 0x00488DB0, 24, "CParticleSource::EmitMushroom"},
    {0x00488F90, 0x00489190, 14, "CParticleSource::EmitNuke"},
    {0x00489320, 0x00489A50, 40, "CParticleSource::EmitCastRing"},
    {0x00489BE0, 0x00489EF0, 15, "CParticleSource::EmitBanshee"},
    {0x00489F70, 0x0048A310, 32, "CParticleSource::EmitTornado"},
    {0x0048A8F0, 0x0048ADA0, 48, "CParticleSource::EmitGeyser"},
    {0x0048ADA0, 0x0048AEC0, 5, "CParticleSource::UpdateGeyser"},
    {0x0048AEC0, 0x0048B080, 23, "CParticleSource::TickGeyser"},
    {0x0048B080, 0x0048B570, 21, "CParticleSource::EmitPoisonFog"},
    {0x0048B630, 0x0048BBE0, 25, "CParticleSource::EmitHealing"},
    {0x0048BD50, 0x0048C010, 24, "CParticleSource::EmitVulkan"},
    {0x0048C090, 0x0048C590, 12, "CParticleSource::EmitBlast"},
    {0x0048C800, 0x0048CAD0, 15, "CParticleSource::EmitFireBall"},
    {0x0048CBA0, 0x0048CED0, 21, "CParticleSource::EmitArrowSmoke"},
    {0x0048CED0, 0x0048D050, 5, "CParticleSource::UpdateArrowSmoke"},
    {0x0048D050, 0x0048D330, 8, "CParticleSource::EmitAcidRay"},
    {0x0048D420, 0x0048D7F0, 12, "CParticleSource::EmitStench"},
    {0x0048D8D0, 0x0048DE30, 8, "CParticleSource::EmitLink"},
    {0x0048E070, 0x0048E7C0, 29, "CParticleSource::EmitOrbit"},
    {0x0048E9E0, 0x0048EC20, 20, "CParticleSource::EmitSmoke"},
    {0x0048EC20, 0x0048ED60, 5, "CParticleSource::UpdateSmoke"},
    {0x0048ED60, 0x0048EE20, 5, "CParticleSource::EmitTendril"},
    {0x0048EEA0, 0x0048F300, 13, "CParticleSource::EmitShower"},
    {0x0048F3B0, 0x0048F670, 28, "CParticleSource::EmitFalling"},
    {0x0048F6F0, 0x0048F7F0, 10, "CParticleSource::TickFalling"},
    {0x0048F7F0, 0x0048FAB0, 16, "CParticleSource::EmitClayRing"},
    {0x0048FDB0, 0x00490060, 27, "CParticleSource::EmitTeleport"},
    {0x00490130, 0x00490990, 29, "CParticleSource::EmitModifier"},
    {0x00490C10, 0x00491230, 30, "CParticleSource::EmitCastSwirl"},
    {0x00491230, 0x00491390, 5, "CParticleSource::UpdateCastSwirl"},
    {0x00491420, 0x00491830, 24, "CParticleSource::EmitColumn"},
    {0x004918E0, 0x00492030, 26, "CParticleSource::EmitCasting"},
    {0x00492030, 0x004921D0, 5, "CParticleSource::UpdateCasting"},
    {0x004921D0, 0x004923B0, 15, "CParticleSource::TickCasting"},
    {0x004923B0, 0x00492B80, 37, "CParticleSource::EmitLightning"},
    {0x00492D30, 0x00492F40, 19, "CParticleSource::EmitZoneExit"},
    {0x00493560, 0x00493770, 15, "CParticleSource::EmitBag"},
    {0x004938C0, 0x00493D10, 20, "CParticleSource::TickBag"},
    {0x00493D50, 0x00493DE0, 5, "CParticleSource::UpdatePortalStar"},
    {0x00493DE0, 0x00494170, 10, "CParticleSource::EmitCylinder"},
    {0x00494470, 0x00494770, 27, "CParticleSource::EmitPortal"},
    {0x00494770, 0x00494930, 5, "CParticleSource::UpdatePortal"},
    {0x00494A10, 0x00494CD0, 19, "CParticleSource::EmitSparks"},
    {0x00494CD0, 0x00494E00, 5, "CParticleSource::UpdateSparks"},
    {0x00494F30, 0x004950B0, 5, "CParticleSource::UpdateVisionStar"},
    {0x00495120, 0x00495320, 5, "CParticleSource::UpdateRegeneration"},
    {0x00495320, 0x004955D0, 17, "CParticleSource::EmitSilence"},
    {0x004955D0, 0x004957E0, 5, "CParticleSource::UpdateSilence"},
    {0x004957E0, 0x00495950, 15, "CParticleSource::EmitFeebleMind"},
    {0x00495A30, 0x00495C80, 12, "CParticleSource::EmitFeetCloud"},
    {0x00495FB0, 0x004961B0, 8, "CParticleSource::EmitStun"},
    {0x004961B0, 0x00496270, 5, "CParticleSource::UpdateStun"},
    {0x00496270, 0x004964B0, 8, "CParticleSource::EmitCurseStars"},
    {0x004964B0, 0x004965E0, 5, "CParticleSource::UpdateCurseStars"},
    {0x004965E0, 0x00496840, 8, "CParticleSource::EmitStartTrans"},
    {0x00496840, 0x00496990, 5, "CParticleSource::UpdateStartTrans"},
    {0x00496EB0, 0x004976B0, 1, "CParticleSource::Tick"},
    {0x0049A090, 0x0049A220, 13, "CCloudNoise::MakeLayerImpl"},
    {0x0049A220, 0x0049AAF0, 13, "CCloudNoise::GenerateImpl"},
    {0x004E1C60, 0x004E1EB0, 15, "CVisibilityFog::Reveal"},
    {0x00510890, 0x00510B80, 1, "CMapObjectClientSpecific::Update"},
    {0x00535B30, 0x00535CB0, 5, "CUnitClientSpecific::AdvanceTime"},
    {0x005ECA10, 0x005ED710, 1, "CItemScreen::DrawItem"},
    {0x00607A60, 0x00607BB0, 10, "CBriefingScreen::PlaySpeakerAnimation"},
    {0x006326B0, 0x00632910, 5, "CMainMenu::Create"},
    {0x00632B10, 0x00632D70, 21, "CMainMenu::Update"},
    {0x006366F0, 0x006369D0, 5, "CBattleScreen::OnUnitSpeech"},
    {0x00636F10, 0x00637190, 1, "CBattleScreen::OrderAt"},
    {0x00637190, 0x006373A0, 1, "CBattleScreen::AttackUnit"},
    {0x006373A0, 0x00637500, 5, "CBattleScreen::OrderDeadUnit"},
    {0x00637500, 0x00637750, 5, "CBattleScreen::UseOnUnit"},
    {0x00637750, 0x006379E0, 5, "CBattleScreen::UseAtPoint"},
    {0x00638EF0, 0x00639D60, 10, "CBattleScreen::SelectUnit"},
    {0x006407E0, 0x00641080, 5, "CHudPortraits::Rebuild and Wobble"},
    {0x00641080, 0x00641CE0, 20, "CHudPortraits::Update"},
    {0x006B5350, 0x006B56C0, 17, "CMusicSystem::UpdateMusic"},
    {0x006B8250, 0x006B8600, 5, "CSoundManager::Play2D"},
    {0x006B8600, 0x006B8960, 5, "CSoundManager::Play3D"},
    {0x006B9B10, 0x006BA140, 19, "CSoundManager::Tick"},
    {0x006BA140, 0x006BA2D0, 5, "SoundSequence::PlayNext"},
    {0x006C1C00, 0x006C30E0, 1, "SectorData::Build"},
    {0x006C3E80, 0x006C42A0, 5, "SectorData::Animate"},
    {0x006CB390, 0x006CBC00, 26, "Terrain::PlayAmbience"},
};

// The client effects that drew rand(): their CALLs of it.
const Range kRandFunctions[] = {
    {0x004E3980, 0x004E3B80, 1, "WeatherNoiseCurve::Build"},
    {0x005341A0, 0x005344A0, 1, "CUnitClientSpecific::UpdateClient"},
    {0x00534590, 0x00534760, 2, "CUnitClientSpecific::FootstepSound"},
    {0x00534760, 0x00534B60, 3, "CUnitClientSpecific::ActionSound"},
    {0x00534B60, 0x00535220, 1, "CUnitClientSpecific::PlayStepSounds"},
    {0x005373D0, 0x00537500, 1, "CUnitClientSpecific::DeathEffect"},
    {0x00537500, 0x00537C00, 3, "CUnitClientSpecific::HitEffect"},
    {0x00604C50, 0x00605DE0, 1, "CBriefingScreen::NextPhrase"},
    {0x00619150, 0x006193C0, 1, "MpHeroPlayVoice"},
    {0x006C57E0, 0x006C5AA0, 3, "Terrain::BuildWavePhases"},
    {0x006CF660, 0x006CF900, 2, "BloodPrint::Construct"},
    {0x006CFEA0, 0x006D00D0, 1, "GroundPrint::ConstructScorch"},
    {0x006D8430, 0x006D8780, 2, "SayAck"},
    {0x006D8780, 0x006D88E0, 1, "PickSpeech"},
    {0x006D88E0, 0x006D8A40, 1, "PickEnvironmentSpeech"},
};

IsaacRandom g_client = {};   // the client generator (zero until CWorld::Create seeds it, as ours)

std::uint32_t Word(std::uintptr_t at) {
    std::uint32_t value;
    std::memcpy(&value, reinterpret_cast<const void*>(at), sizeof(value));
    return value;
}

// Writes `bytes` over the code at `at`; `what` (not null) notes the patch (the client effects' ranges note theirs
// once each: the table of patches is small).
void Write(std::uintptr_t at, const void* bytes, std::size_t size, const char* what) {
    if (what) ei::NoteHarnessPatch(at, size, what);
    auto* p = reinterpret_cast<unsigned char*>(at);
    DWORD old;
    VirtualProtect(p, size, PAGE_EXECUTE_READWRITE, &old);
    std::memcpy(p, bytes, size);
    VirtualProtect(p, size, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, size);
}

// The CALL rel32 at `at` calls `target`.
bool Calls(std::uintptr_t at, std::uintptr_t target) {
    return *reinterpret_cast<const unsigned char*>(at) == 0xE8 && at + 5 + static_cast<std::int32_t>(Word(at + 1)) == target;
}

void PointCall(std::uintptr_t at, const void* to, const char* what) {
    const auto rel = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(to) - (at + 5));
    Write(at + 1, &rel, sizeof(rel), what);
}

// rand() for the client effects: a draw of the client generator in rand()'s range (ours: ClientRand).
int __cdecl ClientRandForOriginal() {
    return static_cast<int>(OriginalCode<std::uint32_t(__thiscall*)(IsaacRandom*)>(kNext)(&g_client) & 0x7FFF);
}

// CWorld::Create's Init of the game's generator (ECX), then once the client generator's from its memory words
// (ours: g_clientRandom.InitFrom(g_random)).
void __fastcall InitForOriginal(IsaacRandom* random) {
    using InitFn = void(__thiscall*)(IsaacRandom*);
    OriginalCode<InitFn>(kInit)(random);
    if (g_client.seeded_ != 0) return;
    std::memcpy(g_client.results_, random->memory_, sizeof(g_client.results_));
    OriginalCode<InitFn>(kInit)(&g_client);
}

// Terrain::SectorAt's Ensure (ECX the sector): a built sector is lit at once (ours writes the fade at Build).
void __fastcall EnsureForOriginal(SectorData* sector, void*, int wanted, void* terrain) {
    using EnsureFn = int(__thiscall*)(SectorData*, int, void*);
    OriginalCode<EnsureFn>(kEnsure)(sector, wanted, terrain);
    if (sector->built & kStageBuilt) OriginalCode<EnsureFn>(kEnsure)(sector, kStageLit, terrain);
}

// Where the game's generator lies (exe::Random, 007A9D40): an operand in [GeneratorStart(), GeneratorEnd()) addresses it.
std::uintptr_t GeneratorStart() { return reinterpret_cast<std::uintptr_t>(&Random()); }
std::uintptr_t GeneratorEnd() { return GeneratorStart() + sizeof(IsaacRandom); }

// The generator operands in [start, end): `count` of them pointed at the client generator's field. False (nothing
// written) when the range holds another number.
bool RetargetOperands(const Range& range) {
    const std::uintptr_t generator = GeneratorStart(), generatorEnd = GeneratorEnd();
    int found = 0;
    for (std::uintptr_t at = range.start; at + 4 <= range.end; ++at) {
        const std::uint32_t value = Word(at);
        if (value >= generator && value < generatorEnd) ++found;
    }
    if (found != range.count) {
        Log("harness: client effects: %08X (%s) has %d generator operands, not %d; left alone", unsigned(range.start),
            range.name, found, range.count);
        return false;
    }
    ei::NoteHarnessPatch(range.start, range.end - range.start, "a client effect's generator operands (R34a)");
    const auto client = reinterpret_cast<std::uintptr_t>(&g_client);
    for (std::uintptr_t at = range.start; at + 4 <= range.end; ++at) {
        const std::uint32_t value = Word(at);
        if (value < generator || value >= generatorEnd) continue;
        const auto moved = static_cast<std::uint32_t>(client + (value - generator));
        Write(at, &moved, sizeof(moved), nullptr);
        at += 3;
    }
    return true;
}

// The CALLs of rand() in [start, end) pointed at ClientRandForOriginal.
bool RetargetRandCalls(const Range& range) {
    int found = 0;
    for (std::uintptr_t at = range.start; at + 5 <= range.end; ++at)
        if (Calls(at, kRand)) ++found;
    if (found != range.count) {
        Log("harness: client effects: %08X (%s) has %d CALLs of rand(), not %d; left alone", unsigned(range.start),
            range.name, found, range.count);
        return false;
    }
    ei::NoteHarnessPatch(range.start, range.end - range.start, "a client effect's rand() calls (R34a)");
    for (std::uintptr_t at = range.start; at + 5 <= range.end; ++at)
        if (Calls(at, kRand)) PointCall(at, reinterpret_cast<const void*>(&ClientRandForOriginal), nullptr);
    return true;
}

} // namespace

void Install() {
    if (ei::Standalone()) return;
    int patched = 0;
    // SetSun: MOV EAX,[EBX+0x21C] (8B 83 1C 02 00 00) becomes a JMP to the epilogue.
    static const unsigned char kInterval[] = {0x8B, 0x83, 0x1C, 0x02, 0x00, 0x00};
    if (std::memcmp(reinterpret_cast<const void*>(kSetSunRelight), kInterval, sizeof(kInterval)) == 0) {
        unsigned char jump[sizeof(kInterval)] = {0xE9, 0, 0, 0, 0, 0x90};
        const auto rel = static_cast<std::int32_t>(kSetSunReturn - (kSetSunRelight + 5));
        std::memcpy(jump + 1, &rel, sizeof(rel));
        Write(kSetSunRelight, jump, sizeof(jump), "SetSun keeps the sectors lit (R34a)");
        ++patched;
    } else {
        Log("harness: client effects: SetSun's relighting at %08X is not as expected; left alone", unsigned(kSetSunRelight));
    }
    for (std::uintptr_t at : kEnsureCalls) {
        if (!Calls(at, kEnsure)) {
            Log("harness: client effects: %08X is not a CALL of Ensure; left alone", unsigned(at));
            continue;
        }
        PointCall(at, reinterpret_cast<const void*>(&EnsureForOriginal), "a sector lit when built (R34a)");
        ++patched;
    }
    Log("harness: client effects: the terrain's sectors lit from Build on (%d of 3 original sites)", patched);
}

void InstallGenerator() {
    if (ei::Standalone()) return;
    int isaac = 0, crt = 0;
    for (const Range& range : kIsaacFunctions)
        if (RetargetOperands(range)) ++isaac;
    for (const Range& range : kRandFunctions)
        if (RetargetRandCalls(range)) ++crt;
    bool seeded = Calls(kInitCall, kInit);
    if (seeded) PointCall(kInitCall, reinterpret_cast<const void*>(&InitForOriginal), "the client generator seeded (R34a)");
    else Log("harness: client effects: %08X is not a CALL of IsaacRandom::Init; the client generator stays unseeded", unsigned(kInitCall));
    Log("harness: client effects: their own generator in %d of %d ISAAC functions and %d of %d rand() functions%s", isaac,
        int(sizeof(kIsaacFunctions) / sizeof(kIsaacFunctions[0])), crt, int(sizeof(kRandFunctions) / sizeof(kRandFunctions[0])),
        seeded ? "" : " (unseeded)");
}

bool IsClientGenerator(const void* isaac) { return isaac == &g_client; }

} // namespace ei::verify::client_effects
