// A creature's starting vitals and statistics from its prototype records, the prototype applied
// to a new map object, and the map object factory. See unit_create.h and docs/notes/units.md
// ("Fourth round").
#include "unit_create.h"

#include "item.h"
#include "map_object.h"
#include "object_tables.h"
#include "unit.h"

#include "lib/crt.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "ei/x87.h"

#include <bit>
#include <cstring>

// Values the original keeps on the FPU stack are double locals; values it stores are floats.
#pragma float_control(precise, on)

namespace {
template <typename T>
T& At(void* base, int offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(base) + offset);
}
template <typename T>
T Read(const char* record, int offset) {
    return Field<T>(record, offset);   // faults on a null record like the original
}

// Prototype records (0x120 bytes, 007AF358): fields used here.
namespace proto {
constexpr int kModelIndex = 0x08;       // index of the model record
constexpr int kScale = 0x14;            // three floats
constexpr int kHitPoints = 0x48;
constexpr int kMana = 0x4C;
constexpr int kProtectionScale = 0x58;  // multiplies the model's protection values
constexpr int kStats8C = 0x5C;          // -> stats +0x8C
constexpr int kSpeed = 0x60;            // -> stats speedBase
constexpr int kCombat = 0x64;           // five floats -> stats +0x70..+0x80
constexpr int kNaturalWeapon = 0x78;    // -> stats +0xAC
constexpr int kDamage = 0x80;           // -> stats +0x84
constexpr int kDamageSpread = 0x84;     // -> stats +0x88
constexpr int kSkillLevel = 0x88;       // every skill byte starts here (rounded)
constexpr int kAttribute01 = 0x8C;      // attributes 0 and 1
constexpr int kAttribute2 = 0x90;       // attribute 2
constexpr int kStats48 = 0x94;          // -> stats +0x48
constexpr int kValuesB0 = 0x98;         // six floats
constexpr int kValuesE0 = 0xB0;         // six floats
constexpr int kSkills3to5 = 0xF4;       // three floats -> skill bytes 3..5 (truncated)
} // namespace proto

// Model records (0x168 bytes, 007AF398).
namespace model {
constexpr int kHitPointRegen = 0x08;
constexpr int kManaRegen = 0x0C;
constexpr int kStats60 = 0x14;          // -> stats +0x60
constexpr int kAngle = 0x18;            // degrees; cos(angle / 2) -> stats +0x44
constexpr int kModeSpeeds = 0x1C;       // four dwords -> stats +0x34
constexpr int kStats64 = 0x44;
constexpr int kSizeClass = 0x48;        // short -> stats +0x68
constexpr int kSizeClassSmall = 0x4C;   // short -> stats +0x6C and +0x6E
constexpr int kDamageWeights = 0x50;    // seven dwords -> stats +0x90
constexpr int kProtection = 0x6C;       // seven floats
constexpr int kSlots = 0x8C;            // six {level record index, share, factor} of 0x10
constexpr int kHeight = 0x104;          // the object stands this much lower (00513B60)
} // namespace model

// NPC records (007AF378).
namespace npc {
constexpr int kExperience = 0x08;
constexpr int kStats = 0x0C;            // three floats: stat10, stat18, stat20
constexpr int kAttributesAndSkills = 0x18;   // 3 attribute and 8 skill bytes
constexpr int kSkillNames = 0x24;       // vector of skill names (begin, end)
} // namespace npc

constexpr int kSkillLevelRecordSize = 0x44;   // 007AF3B8
constexpr std::uintptr_t kSkillDatabase = 0x007AF268;

constexpr float kThird = std::bit_cast<float>(0x3EAAAAABu);     // 0073BAA8
constexpr float kHalf = 0.5f;                                    // 0073B86C
constexpr std::uint32_t kStat25 = 0x41C80000;                    // 25.0
constexpr std::uint32_t kRate15 = 0x41700000;                    // 15.0
constexpr std::uint32_t kDefaultStat = 0x42480000;               // 50.0
constexpr std::uint32_t kDefaultModeSpeed = 0x3E088889;          // 0.13333334
constexpr std::uint32_t kDefaultSpeed = 0x3E4CCCCD;              // 0.2
constexpr std::uint32_t kOne = 0x3F800000;
constexpr std::uint32_t kHundred = 0x42C80000;
constexpr std::uint32_t kThousand = 0x447A0000;
constexpr std::uint32_t kTen = 0x41200000;
constexpr int kSlotWorking = 3;

// pi and 1/180 (0073DCA8 / 0073BA30) and 90 (0073BAB8): volatile so the products are computed
// at run time with the FPU's precision, as the original does.
volatile double g_pi = 3.141592653589793;
volatile double g_perDegree = std::bit_cast<double>(0x3F76C16C16C16C17ull);
volatile double g_ninety = 90.0;

void SetBits(void* base, int offset, std::uint32_t bits) { At<std::uint32_t>(base, offset) = bits; }
void CopyDword(void* to, int toOffset, const char* from, int fromOffset) {
    At<std::uint32_t>(to, toOffset) = Read<std::uint32_t>(from, fromOffset);
}

// FLD [src]; FST [a]; FSTP [b]: a float copied through the FPU to two places.
__declspec(naked) void __fastcall FpuCopy2(float* /*a*/, float* /*b*/, const float* /*src*/) {
    __asm {
        mov eax, [esp + 4]
        fld dword ptr [eax]
        fst dword ptr [ecx]
        fstp dword ptr [edx]
        ret 4
    }
}

// The game's _ftol (006E853C) of a float: truncation, low byte kept.
__declspec(naked) int __cdecl GameFtol(double) {
    __asm {
        fld qword ptr [esp + 4]
        mov eax, 0x006E853C
        call eax
        ret
    }
}

std::uint8_t RoundToByte(const char* record, int offset) {
    return static_cast<std::uint8_t>(x87::fistp(Read<float>(record, offset)));
}

// Not rewritten / rewritten elsewhere, through their game addresses:
// 00523370 UnitVitals::Reset, 005233E0 UnitStats::Reset, 00523960 AddScaledExperience,
// 00525AF0 RefillSlots, all in unit_vitals.cpp / unit_stats_pack.cpp.
__declspec(naked) void* __fastcall ResetVitalsAt(UnitVitals*) EI_ORIGINAL(0x00523370)
__declspec(naked) int __fastcall ResetStatsAt(UnitStats*) EI_ORIGINAL(0x005233E0)
__declspec(naked) void __fastcall AddScaledExperienceAt(UnitStats*, void*, float) EI_ORIGINAL(0x00523960)
__declspec(naked) void* __fastcall RefillSlotsAt(UnitStats*) EI_ORIGINAL(0x00525AF0)
// 0051FE00. Skill database: the skill named `name` by _mbsicmp, or null. Not rewritten.
__declspec(naked) void* __fastcall FindSkill(void*, void*, const void*) EI_ORIGINAL(0x0051FE00)
// 00526450. Statistics: learns `skill` of the skill database. Not rewritten.
__declspec(naked) void __fastcall LearnSkillAt(UnitStats*, void*, void*, void*) EI_ORIGINAL(0x00526450)
// 00524CD0. Statistics: recomputes the current values at `scale`. Not rewritten.
__declspec(naked) void __fastcall RecomputeAt(UnitStats*, void*, std::uint32_t) EI_ORIGINAL(0x00524CD0)
// This file's functions through their addresses, so their verify wrappers stay in the path.
__declspec(naked) char* __fastcall FindPrototypeAt(UnitPrototypeTable*, void*, const char*) EI_ORIGINAL(0x005144D0)
__declspec(naked) char* __fastcall FindPrototypeAt2(UnitPrototypeTable*, void*, const char*) EI_ORIGINAL(0x00514930)
__declspec(naked) char* __fastcall FindNpcAt(NpcPrototypeTable*, void*, const char*) EI_ORIGINAL(0x005145D0)
__declspec(naked) int __fastcall SetDefaultsAt(UnitStatsBuilder*, void*, UnitVitals*, UnitStats*) EI_ORIGINAL(0x005134F0)
__declspec(naked) int __fastcall BuildFromPrototypeAt(UnitStatsBuilder*, void*, UnitVitals*, UnitStats*, const char*)
    EI_ORIGINAL(0x005136D0)
// 0050F050 CMapObject::SetServerData, 00517700 CObject3D::SetFigureScale, 005308A0
// CUnit::SetPrototype23C, rewritten elsewhere.
__declspec(naked) std::uintptr_t __fastcall SetServerDataAt(CEngineObject*, void*, const void*) EI_ORIGINAL(0x0050F050)
__declspec(naked) std::uintptr_t __fastcall SetFigureScaleAt(CEngineObject*, void*, const float*) EI_ORIGINAL(0x00517700)
__declspec(naked) std::uint32_t __fastcall SetPrototype23CAt(CEngineObject*, void*, const char*) EI_ORIGINAL(0x005308A0)

// Object slots used by ApplyPrototype.
constexpr int kSlotResetScale = 0x78 / 4;       // 30
constexpr int kSlotScaleEffect = 0x84 / 4;      // 33
constexpr int kSlotServerStats = 0x8C / 4;      // 35
constexpr int kSlotServerData = 0x90 / 4;       // 36

const char* ModelOf(const char* prototype) {
    return unitdb::Models().At(Read<std::uint32_t>(prototype, proto::kModelIndex), unitdb::kModelSize);
}

// --- The statistics both builders fill the same way ---------------------------------------

// cos(angle / 2) with the angle in degrees, stored to a float.
float HalfAngleCosine(const char* model) {
    double half = static_cast<double>(Read<float>(model, model::kAngle)) * kHalf;
    double perDegree = g_pi * g_perDegree;
    return static_cast<float>(x87::fcos(half * perDegree));
}

float RightAngleCosine() {
    double perDegree = g_pi * g_perDegree;
    return static_cast<float>(x87::fcos(perDegree * g_ninety));
}

// Attribute and skill bytes: from the NPC record, or rounded from the prototype.
void SetAttributesAndSkills(UnitStats* stats, const char* prototype, const char* npcRecord) {
    if (npcRecord) {
        std::memcpy(stats->attributes, npcRecord + npc::kAttributesAndSkills, 3 + 8);
        return;
    }
    for (int i = 0; i < 3; ++i) stats->attributes[i] = RoundToByte(prototype, proto::kSkillLevel);
    for (int i = 0; i < 8; ++i) stats->skills[i] = RoundToByte(prototype, proto::kSkillLevel);
    stats->attributes[0] = RoundToByte(prototype, proto::kAttribute01);
    stats->attributes[1] = RoundToByte(prototype, proto::kAttribute01);
    stats->attributes[2] = RoundToByte(prototype, proto::kAttribute2);
    for (int i = 0; i < 3; ++i)
        stats->skills[3 + i] = static_cast<std::uint8_t>(GameFtol(Read<float>(prototype, proto::kSkills3to5 + 4 * i)));
}

// The body slots from the model: a slot with a share is working, with its type and vital mask
// from its level record (007AF3B8) and no layers; the others unused.
void SetSlots(UnitStats* stats, const char* model) {
    const RecordTable& levels = *reinterpret_cast<const RecordTable*>(0x007AF3B8);
    for (int i = 0; i < 6; ++i) {
        const char* entry = model + model::kSlots + 0x10 * i;
        UnitStatSlot& slot = stats->slots[i];
        float share = Read<float>(entry, 4);
        if (!(share < 0.0f || share > 0.0f)) {   // zero (or NaN)
            slot.kind = 0;
            continue;
        }
        const char* level = levels.At(Read<std::uint32_t>(entry, 0), kSkillLevelRecordSize);
        slot.type = Read<int>(level, 4);
        std::memcpy(&slot.unknown_EC, entry + 4, 4);
        std::memcpy(&slot.factor, entry + 8, 4);
        slot.vitalMask = Read<std::uint32_t>(level, 0x40);
        slot.layers[0].unknown_00 = 0;
        slot.layers[0].state = 0;
        slot.layers[1].state = 0;
        slot.layers[2].state = 0;
        slot.kind = kSlotWorking;
    }
}

// The combat values and damage weights.
void SetCombat(UnitStats* stats, const char* prototype, const char* model) {
    for (int i = 0; i < 5; ++i) CopyDword(stats, 0x70 + 4 * i, prototype, proto::kCombat + 4 * i);
    CopyDword(stats, 0x84, prototype, proto::kDamage);
    CopyDword(stats, 0x88, prototype, proto::kDamageSpread);
    std::memcpy(reinterpret_cast<char*>(stats) + 0x90, model + model::kDamageWeights, 7 * 4);
    CopyDword(stats, 0xAC, prototype, proto::kNaturalWeapon);
}

// Experience and the skills an NPC knows; the spent experience then equals it.
void LearnNpcSkills(UnitStats* stats, const char* npcRecord) {
    stats->spentExperience = 0.0f;
    if (!npcRecord) return;
    AddScaledExperienceAt(stats, nullptr, Read<float>(npcRecord, npc::kExperience));
    for (char* const* name = Read<char* const*>(npcRecord, npc::kSkillNames);
         name != Read<char* const*>(npcRecord, npc::kSkillNames + 4); ++name) {
        void* skill = FindSkill(reinterpret_cast<void*>(kSkillDatabase), nullptr, *name);
        if (skill) LearnSkillAt(stats, nullptr, reinterpret_cast<void*>(kSkillDatabase), skill);
    }
    std::memcpy(&stats->spentExperience, &stats->experience, 4);
}

float* StatField(UnitStats* stats, int offset) { return &At<float>(stats, offset); }
} // namespace

// --- Name lookups -----------------------------------------------------------------------------

EI_REPLACE_METHOD(0x005144D0, UnitPrototypeTable::FindByName);

char* NpcPrototypeTable::FindByName(const char* name) {
    stlp::string key(name);
    stlp::string pairKey(key);
    char* record = byName[pairKey];
    return record;
}
EI_REPLACE_METHOD(0x005145D0, NpcPrototypeTable::FindByName);

// --- Builders ---------------------------------------------------------------------------------

int UnitStatsBuilder::SetDefaults(UnitVitals* vitals, UnitStats* stats) {
    if (!stats) {
        SetBits(vitals, 0x08, kThousand);
        SetBits(vitals, 0x0C, kThousand);
        SetBits(vitals, 0x10, kTen);
        SetBits(vitals, 0x14, kThousand);
        SetBits(vitals, 0x18, kThousand);
        SetBits(vitals, 0x1C, kTen);
        for (int i = 0; i < 7; ++i) {
            SetBits(vitals, 0x24 + 8 * i, kHundred);
            SetBits(vitals, 0x20 + 8 * i, kHundred);
        }
        return 1;
    }
    SetBits(stats, 0x04, 0);
    SetBits(stats, 0x0C, kDefaultStat);
    SetBits(stats, 0x14, kDefaultStat);
    SetBits(stats, 0x1C, kDefaultStat);
    for (int i = 0; i < 4; ++i) SetBits(stats, 0x34 + 4 * i, kDefaultModeSpeed);
    SetBits(stats, 0x08, 0);
    SetBits(stats, 0x24, kDefaultSpeed);
    SetBits(stats, 0x2C, kOne);
    SetBits(stats, 0x54, 0);
    SetBits(stats, 0x60, 0);
    for (int i = 0; i < 6; ++i) {
        SetBits(stats, 0xB0 + 8 * i, kHundred);
        SetBits(stats, 0xB4 + 8 * i, kHundred);
        SetBits(stats, 0xE0 + 8 * i, kHundred);
        SetBits(stats, 0xE4 + 8 * i, kHundred);
    }
    for (UnitStatSlot& slot : stats->slots) {
        slot.type = 1;
        slot.vitalMask = 0;
        slot.kind = kSlotWorking;
        SetBits(&slot.current, 0, kHundred);
        SetBits(&slot.maximum, 0, kHundred);
        for (UnitStatLayer& layer : slot.layers) {
            layer.unknown_00 = 0;
            layer.state = 0;
            layer.unknown_40 = 0;
            layer.unknown_44 = 0;
            std::memset(layer.absorb, 0, sizeof(layer.absorb));
        }
    }
    return 1;
}
EI_REPLACE_METHOD(0x005134F0, UnitStatsBuilder::SetDefaults);

int UnitStatsBuilder::BuildFromPrototype(UnitVitals* vitals, UnitStats* stats, const char* name) {
    const char* prototype = FindPrototypeAt(&unitdb::PrototypeTable(), nullptr, name);
    if (!prototype) {
        SetDefaultsAt(this, nullptr, vitals, stats);
        return 1;
    }
    const char* model = ModelOf(prototype);
    const char* npcRecord = FindNpcAt(&unitdb::NpcTable(), nullptr, name);
    if (!vitals || !stats) return 0;
    ResetVitalsAt(vitals);
    ResetStatsAt(stats);

    // Vitals: hit points and mana at their maxima, the model's regeneration.
    FpuCopy2(&vitals->maxHitPoints, &vitals->hitPoints, reinterpret_cast<const float*>(prototype + proto::kHitPoints));
    CopyDword(vitals, 0x10, model, model::kHitPointRegen);
    FpuCopy2(&vitals->maxMana, &vitals->mana, reinterpret_cast<const float*>(prototype + proto::kMana));
    CopyDword(vitals, 0x1C, model, model::kManaRegen);
    std::memcpy(&stats->fixedMaxHitPoints, &vitals->maxHitPoints, 4);

    // The three stats, speed and rate.
    if (npcRecord) {
        FpuCopy2(&stats->stat10Base, &stats->stat10, reinterpret_cast<const float*>(npcRecord + npc::kStats));
        FpuCopy2(&stats->stat18Base, &stats->stat18, reinterpret_cast<const float*>(npcRecord + npc::kStats + 4));
        FpuCopy2(&stats->stat20Base, &stats->stat20, reinterpret_cast<const float*>(npcRecord + npc::kStats + 8));
        float rate = static_cast<float>(static_cast<double>(stats->stat18) * kHalf);
        CopyDword(stats, 0x24, prototype, proto::kSpeed);
        stats->rateBase = rate;
    } else {
        float strength = static_cast<float>(static_cast<double>(Read<float>(prototype, proto::kHitPoints)) * kThird);
        stats->stat10Base = strength;
        stats->stat10 = strength;
        float dexterity = static_cast<float>(static_cast<double>(Read<float>(prototype, proto::kMana)) * kThird);
        stats->stat18Base = dexterity;
        stats->stat18 = dexterity;
        SetBits(stats, 0x1C, kStat25);
        SetBits(stats, 0x20, kStat25);
        SetBits(stats, 0x2C, kRate15);
        CopyDword(stats, 0x24, prototype, proto::kSpeed);
    }
    CopyDword(stats, 0x8C, prototype, proto::kStats8C);
    std::memcpy(stats->modeSpeeds, model + model::kModeSpeeds, sizeof(stats->modeSpeeds));
    for (int i = 0; i < 6; ++i) {
        CopyDword(stats, 0xB0 + 8 * i, prototype, proto::kValuesB0 + 4 * i);
        CopyDword(stats, 0xE0 + 8 * i, prototype, proto::kValuesE0 + 4 * i);
    }
    *StatField(stats, 0x44) = HalfAngleCosine(model);
    CopyDword(stats, 0x48, prototype, proto::kStats48);
    *StatField(stats, 0x4C) = RightAngleCosine();
    CopyDword(stats, 0x60, model, model::kStats60);
    SetAttributesAndSkills(stats, prototype, npcRecord);
    CopyDword(stats, 0x64, model, model::kStats64);
    At<std::uint16_t>(stats, 0x68) = Read<std::uint16_t>(model, model::kSizeClass);
    std::uint16_t small = Read<std::uint16_t>(model, model::kSizeClassSmall);
    At<std::uint16_t>(stats, 0x6C) = small;
    At<std::uint16_t>(stats, 0x6E) = small;

    // Protection: the model's values scaled by the prototype.
    for (int i = 0; i < 7; ++i) {
        float value = static_cast<float>(static_cast<double>(Read<float>(model, model::kProtection + 4 * i)) *
                                         Read<float>(prototype, proto::kProtectionScale));
        vitals->protection[i].base = value;
        vitals->protection[i].current = value;
    }
    SetSlots(stats, model);
    SetCombat(stats, prototype, model);
    LearnNpcSkills(stats, npcRecord);
    return 1;
}
EI_REPLACE_METHOD(0x005136D0, UnitStatsBuilder::BuildFromPrototype);

int __stdcall BuildNpcStats(UnitStats* stats, const char* name) {
    const char* prototype = FindPrototypeAt(&unitdb::PrototypeTable(), nullptr, name);
    const char* model = ModelOf(prototype);   // reads the prototype before the null test
    const char* npcRecord = FindNpcAt(&unitdb::NpcTable(), nullptr, name);
    if (!prototype) return game::FatalError(reinterpret_cast<const char*>(0x0078F288), name);
    if (!npcRecord) game::DebugLog(reinterpret_cast<const char*>(0x0078F258), name);
    ResetStatsAt(stats);

    const float* speed = reinterpret_cast<const float*>(prototype + proto::kSpeed);
    if (npcRecord) {
        FpuCopy2(&stats->stat10Base, &stats->stat10, reinterpret_cast<const float*>(npcRecord + npc::kStats));
        FpuCopy2(&stats->stat18Base, &stats->stat18, reinterpret_cast<const float*>(npcRecord + npc::kStats + 4));
        FpuCopy2(&stats->stat20Base, &stats->stat20, reinterpret_cast<const float*>(npcRecord + npc::kStats + 8));
        FpuCopy2(&stats->speedBase, &stats->speed, speed);
        float rate = static_cast<float>(static_cast<double>(stats->stat18) * kHalf);
        stats->rateBase = rate;
        stats->rate = rate;
    } else {
        float strength = static_cast<float>(static_cast<double>(Read<float>(prototype, proto::kHitPoints)) * kThird);
        stats->stat10Base = strength;
        stats->stat10 = strength;
        float dexterity = static_cast<float>(static_cast<double>(Read<float>(prototype, proto::kMana)) * kThird);
        stats->stat18Base = dexterity;
        stats->stat18 = dexterity;
        SetBits(stats, 0x1C, kStat25);
        SetBits(stats, 0x20, kStat25);
        FpuCopy2(&stats->speedBase, &stats->speed, speed);
        SetBits(stats, 0x2C, kRate15);
        SetBits(stats, 0x30, kRate15);
    }
    CopyDword(stats, 0x8C, prototype, proto::kStats8C);
    std::memcpy(stats->modeSpeeds, model + model::kModeSpeeds, sizeof(stats->modeSpeeds));
    for (int i = 0; i < 6; ++i) {
        FpuCopy2(&stats->valuesB0[i].base, &stats->valuesB0[i].current,
                 reinterpret_cast<const float*>(prototype + proto::kValuesB0 + 4 * i));
        FpuCopy2(&stats->valuesE0[i].base, &stats->valuesE0[i].current,
                 reinterpret_cast<const float*>(prototype + proto::kValuesE0 + 4 * i));
    }
    *StatField(stats, 0x44) = HalfAngleCosine(model);
    CopyDword(stats, 0x48, prototype, proto::kStats48);
    *StatField(stats, 0x4C) = RightAngleCosine();
    CopyDword(stats, 0x60, model, model::kStats60);
    SetAttributesAndSkills(stats, prototype, npcRecord);
    CopyDword(stats, 0x64, model, model::kStats64);
    At<std::uint16_t>(stats, 0x68) = Read<std::uint16_t>(model, model::kSizeClass);
    std::uint16_t small = Read<std::uint16_t>(model, model::kSizeClassSmall);
    At<std::uint16_t>(stats, 0x6C) = small;
    At<std::uint16_t>(stats, 0x6E) = small;
    SetSlots(stats, model);
    SetCombat(stats, prototype, model);
    LearnNpcSkills(stats, npcRecord);
    RecomputeAt(stats, nullptr, kOne);
    RefillSlotsAt(stats);
    RecomputeAt(stats, nullptr, kOne);
    return 1;
}
EI_REPLACE(0x00513DA0, BuildNpcStats);

int __stdcall PrototypeObjectClass(const char* name) {
    constexpr int kDefaultClass = 0x33;
    const char* prototype = FindPrototypeAt(&unitdb::PrototypeTable(), nullptr, name);
    if (!prototype) return kDefaultClass;
    const char* model = ModelOf(prototype);
    if (!model) return kDefaultClass;
    return Read<int>(model, 4);
}
EI_REPLACE(0x00513240, PrototypeObjectClass);

// --- Statistics from their bases -----------------------------------------------------------------

namespace {
constexpr float kZero = 0.0f;                                     // 0073B858
constexpr float kOneF = 1.0f;                                     // 0073B84C
constexpr float kPercent = std::bit_cast<float>(0x3C23D70Au);     // 0073C548 / 0073D09C (0.01)
constexpr float kTwelve = 12.0f;                                  // 0073E138
constexpr float kPointTwo = std::bit_cast<float>(0x3E4CCCCDu);    // 0073BAD4
constexpr float kTenF = 10.0f;                                    // 0073BAAC
constexpr std::uint32_t kMinusTwo = 0xC0000000u;
constexpr int kSkillBodyB0 = 15;
constexpr int kSkillHitPoints = 16;
constexpr int kSkillRate = 20;
constexpr int kSkillCapacity = 21;
constexpr int kSkillStat10 = 23;
constexpr int kSkillStat18 = 24;
constexpr int kSkillStat20 = 25;

float Global(std::uintptr_t address) { return *reinterpret_cast<const float*>(address); }

// 007C5460 ^ (ln(experience / 007C545C) / ln(007C5464)): FYL2X with FLDLN2 and the CRT's
// _CIpow (006E81A0), as the original computes it. Unrounded.
__declspec(naked) double __cdecl HitPointExperienceCurve(const float* /*experience*/) {
    __asm {
        mov eax, 0x007C5460
        fld dword ptr [eax]
        mov eax, [esp + 4]
        fld dword ptr [eax]
        mov eax, 0x007C545C
        fdiv dword ptr [eax]
        fldln2
        fxch st(1)
        fyl2x
        mov eax, 0x007C5464
        fld dword ptr [eax]
        fldln2
        fxch st(1)
        fyl2x
        fdivp st(1), st
        mov eax, 0x006E81A0
        call eax
        ret
    }
}

// A level record's value (+0x34, unsigned) as the original's FILD of a zero-extended qword.
double LevelOf(const char* record) {
    volatile std::int64_t wide = Field<std::uint32_t>(record, 0x34);
    return static_cast<double>(wide);
}
double LevelOrZero(UnitStats* stats, int skill) {
    const char* record = stats->SkillLevelRecord(skill);
    return record ? LevelOf(record) : kZero;
}

// 00524060 / 00524110 of unit_vitals.cpp, through their game addresses.
__declspec(naked) float __fastcall LegsFactorAt(UnitStats*, void*, int) EI_ORIGINAL(0x00524060)
__declspec(naked) float __fastcall ArmsFactorAt(UnitStats*, void*, int) EI_ORIGINAL(0x00524110)
} // namespace

void UnitStats::ResetToBase(float newLoad) {
    std::memcpy(&stat10, &stat10Base, 4);
    std::memcpy(&stat18, &stat18Base, 4);
    std::memcpy(&stat20, &stat20Base, 4);
    for (int i = 0; i < 6; ++i) {
        std::memcpy(&valuesB0[i].current, &valuesB0[i].base, 4);
        std::memcpy(&valuesE0[i].current, &valuesE0[i].base, 4);
    }
    double bonus = kZero;
    if (const char* record = SkillLevelRecord(kSkillBodyB0)) {
        double level = LevelOf(record);
        if (level < kZero || level > kZero) bonus = level * valuesB0[1].current * kPercent;
    }
    valuesB0[1].current = static_cast<float>(bonus + valuesB0[1].current);
    stat10 = static_cast<float>(LevelOrZero(this, kSkillStat10) + stat10);
    stat18 = static_cast<float>(LevelOrZero(this, kSkillStat18) + stat18);
    std::memcpy(&unknown_044, &kMinusTwo, 4);
    stat20 = static_cast<float>(LevelOrZero(this, kSkillStat20) + stat20);

    // The slot maxima from the hit point curve; current values clamped to them.
    double curve = HitPointExperienceCurve(&experience);
    float hitPoints = static_cast<float>(
        curve * (static_cast<double>(stat10) / Global(0x007C5454) * Global(0x007C5458)));
    float perShare = static_cast<float>(
        (LevelOrZero(this, kSkillHitPoints) * kPercent + kOneF) * hitPoints / slots[1].unknown_EC);
    for (UnitStatSlot& slot : slots) {
        if (slot.kind <= 0) continue;
        float maximum = static_cast<float>(static_cast<double>(perShare) * slot.unknown_EC);
        slot.maximum = maximum;
        std::uint32_t current = std::bit_cast<std::uint32_t>(slot.current);
        std::uint32_t chosen = !(maximum >= slot.current) ? std::bit_cast<std::uint32_t>(maximum) : current;
        slot.current = std::bit_cast<float>(chosen);
    }

    std::memcpy(&load, &newLoad, 4);   // a dword copy
    float strengthCapacity = static_cast<float>(static_cast<double>(stat10) * kTwelve);
    capacity = static_cast<float>((LevelOrZero(this, kSkillCapacity) * kPercent + kOneF) * strengthCapacity);
    float legs = LegsFactorAt(this, nullptr, 1);
    loadFactor = legs;
    speed = static_cast<float>(static_cast<double>(legs) * speedBase);
    armsFactor = ArmsFactorAt(this, nullptr, 1);
    rateBase = static_cast<float>(static_cast<double>(stat18) * kPointTwo + kTenF);
    rate = static_cast<float>((LevelOrZero(this, kSkillRate) * kPercent + kOneF) * armsFactor * rateBase);
}
EI_REPLACE_METHOD(0x00524CD0, UnitStats::ResetToBase);

namespace {
// 005259B0 UnitStats::Reduce and 00525800 UnitStats::HealSlots of unit_stats.cpp / unit_vitals.cpp,
// through their game addresses. Reduce leaves its result unrounded in ST0.
__declspec(naked) double __fastcall ReduceAt(UnitStats*, void*, float) EI_ORIGINAL(0x005259B0)
__declspec(naked) bool __fastcall HealSlotsAt(UnitStats*, void*, float, float) EI_ORIGINAL(0x00525800)
// 0051FF00. Skill database: the index of `record` in its level records, or -1. Not rewritten.
__declspec(naked) int __fastcall SkillRecordIndex(const void*, void*, const void*) EI_ORIGINAL(0x0051FF00)

// The hit point maximum of the experience curve with skill 16 (the level byte read now).
float CurveHitPoints(UnitStats* stats) {
    double curve = HitPointExperienceCurve(&stats->experience);
    double scaled = curve * (static_cast<double>(stats->stat10) / Global(0x007C5454) * Global(0x007C5458));
    return static_cast<float>((LevelOrZero(stats, kSkillHitPoints) * kPercent + kOneF) * scaled);
}
} // namespace

void UnitStats::LearnSkill(const void* database, const void* record) {
    float oldMaximum = CurveHitPoints(this);
    float oldHitPoints = static_cast<float>(ReduceAt(this, nullptr, oldMaximum));
    int index = SkillRecordIndex(database, nullptr, record);
    int skill = index / 3;
    int level = index % 3;
    std::uint8_t& known = reinterpret_cast<std::uint8_t*>(this)[0x6D3 + skill];   // unchecked, as the original
    if (known != 0 && level <= known - 1) return;
    known = static_cast<std::uint8_t>(level + 1);
    float newMaximum = CurveHitPoints(this);
    if (!(oldMaximum > kOneF)) return;   // NaN returns too
    if (!(newMaximum > kOneF)) return;
    double newHitPoints = ReduceAt(this, nullptr, newMaximum);
    float heal = static_cast<float>(static_cast<double>(oldHitPoints) / oldMaximum * newMaximum - newHitPoints);
    HealSlotsAt(this, nullptr, newMaximum, heal);
}
EI_REPLACE_METHOD(0x00526450, UnitStats::LearnSkill);

namespace {
constexpr float kStatBase = 25.0f;   // 0073E124
constexpr int kSkillRecordSize = 0x18;

// 00527050 UnitStats::SkillWithBonus of unit_stats.cpp, through its game address; ST0.
__declspec(naked) double __fastcall SkillWithBonusAt(UnitStats*, void*, std::uint32_t) EI_ORIGINAL(0x00527050)

// The level value of skill `index` stored to a float (FILD qword, FSTP float); 0 without one.
float LevelAsFloat(const UnitStats* stats, int index) {
    std::uint8_t level = reinterpret_cast<const std::uint8_t*>(stats)[0x6D3 + index];   // unchecked
    if (level == 0) return kZero;
    const char* record =
        unitdb::SkillLevels().At(static_cast<std::uint32_t>(index * 3 - 1 + level), unitdb::kSkillLevelSize);
    if (!record) return kZero;
    return static_cast<float>(LevelOf(record));
}

// The bonus byte (+0x7B for skill 0, +0x7C for skill 1) of the unit's weapon in hand, or 0.
float WeaponBonus(CUnit* unit, int offset) {
    CEngineObject* weapon = unit->GetWeapon(unit->activeWeapon);
    if (!weapon) return kZero;
    return static_cast<float>(static_cast<int>(Read<std::uint8_t>(static_cast<CItem*>(weapon)->GetPrototype(), offset)));
}
} // namespace

double UnitStats::WeaponSkillBonus(int type, CUnit* unit) {
    if (type < 5 || type == 5 || type == 6) {
        int skill = type >= 5 ? 1 : 0;
        float level = LevelAsFloat(this, type);
        if (!(IsTextKey(unit->id_) & 0xFF)) return static_cast<double>(static_cast<int>(skills[skill])) + level;
        float weapon = WeaponBonus(unit, 0x7B + skill);
        float value = static_cast<float>(static_cast<int>(skills[skill]));
        const char* record = unitdb::Skills().At(static_cast<std::uint32_t>(skill), kSkillRecordSize);
        char stat = **Read<const char* const*>(record, 0xC);   // the skill's name
        double base;
        if (stat == 's') base = static_cast<double>(stat10) - kStatBase;
        else if (stat == 'i') base = static_cast<double>(stat20) - kStatBase;
        else if (stat == 'd') base = static_cast<double>(stat18) - kStatBase;
        else return (static_cast<double>(value) + weapon) + level;
        return ((base + value) + weapon) + level;
    }
    if (type == 8) {
        if (!(IsTextKey(unit->id_) & 0xFF)) return static_cast<double>(static_cast<int>(skills[0]));
        float weapon = WeaponBonus(unit, 0x7B);
        return SkillWithBonusAt(this, nullptr, 0) + weapon;
    }
    return kZero;
}
EI_REPLACE_METHOD(0x00526C70, UnitStats::WeaponSkillBonus);

// --- Applying a prototype ---------------------------------------------------------------------

int UnitStatsBuilder::ApplyPrototype(CEngineObject* object, const char* name) {
    UnitVitals vitals{};   // the original's stack copies start as whatever the stack held
    UnitStats stats{};
    const char* prototype = FindPrototypeAt(&unitdb::PrototypeTable(), nullptr, name);
    if (!prototype) {
        ResetVitalsAt(&vitals);
        if (object->GetClassId() != kClassUnit) {
            SetDefaultsAt(this, nullptr, &vitals, nullptr);
            ei::VCall<void>(object, kSlotServerData, &vitals);
        } else {
            ResetStatsAt(&stats);
            SetDefaultsAt(this, nullptr, &vitals, &stats);
            ei::VCall<void>(object, kSlotServerStats, &stats);
        }
        ei::VCall<void>(object, kSlotScaleEffect, 0);
        At<int>(object, 0x18) = 0;
        game::DebugLog(reinterpret_cast<const char*>(0x0078F22C), name, object->id_);
        return 1;
    }
    const char* model = ModelOf(prototype);
    if (object->GetClassId() == kClassUnit) {
        BuildFromPrototypeAt(this, nullptr, &vitals, &stats, name);
        stats.unknown_000 |= 1;
        vitals.unknown_00 |= 1;
        SetServerDataAt(object, nullptr, &vitals);
        ei::VCall<void>(object, kSlotServerStats, &stats);
        // The figure stands lower by the model's height.
        float lift = static_cast<float>(-static_cast<double>(Read<float>(model, model::kHeight)));
        SetBits(object, 0xF4, 0);
        At<float>(object, 0xF8) = lift;
        SetBits(object, 0xFC, 0);
        float scale[3];
        std::memcpy(scale, prototype + proto::kScale, sizeof(scale));
        SetFigureScaleAt(object, nullptr, scale);
        std::memcpy(&reinterpret_cast<CMapObject*>(object)->baseScale, scale, sizeof(scale));
        ei::VCall<void>(object, kSlotResetScale);
        SetPrototype23CAt(object, nullptr, prototype);
    }
    char* record = FindPrototypeAt2(&unitdb::PrototypeTable(), nullptr, name);
    At<int>(object, 0x18) =
        record ? static_cast<int>(record - unitdb::PrototypeTable().begin) / unitdb::kPrototypeSize : -1;
    return 1;
}
EI_REPLACE_METHOD(0x00513B60, UnitStatsBuilder::ApplyPrototype);

// --- The factory ------------------------------------------------------------------------------

namespace {
// The constructors (thiscall on the new block; each returns it).
using Constructor = CEngineObject*(__fastcall*)(void*);
struct FactoryEntry {
    int classId;
    std::size_t size;
    std::uintptr_t constructor;
};
constexpr FactoryEntry kFactory[] = {
    {0x32, 0x4DC, 0x0052E110},   // units (CUnit)
    {0x33, 0x4DC, 0x0052E110},
    {0x34, 0x4DC, 0x0052E110},
    {0x35, 0x238, 0x005296E0},
    {0x36, 0x238, 0x005297C0},
    {0x37, 0x238, 0x005298A0},
    {0x38, 0x238, 0x00529980},
    {0x39, 0x580, 0x004466D0},
    {0x3A, 0x23C, 0x0052BEE0},
    {0x3B, 0x290, 0x00529DD0},
    {0x3C, 0x264, 0x0052B1F0},
    {0x300D, 0x78, 0x0050C5F0},
    {0x3010, 0x23C, 0x00529A80},
};
} // namespace

CEngineObject* CMapObjectManager::CreateObject(int classId) {
    for (const FactoryEntry& entry : kFactory) {
        if (entry.classId != classId) continue;
        void* block = gamecrt::operator_new(entry.size);
        if (!block) return nullptr;
        return reinterpret_cast<Constructor>(entry.constructor)(block);
    }
    return nullptr;
}
EI_REPLACE_METHOD(0x00512730, CMapObjectManager::CreateObject);
