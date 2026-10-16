// The unit AI's attack options (AIAttention, unit_ai.h): listing them from the unit's weapon and
// spells (005CEED0), the option helpers (005CEA30, 005CEA60, 005CEBD0, 005CEDC0, 005CE9B0) and the
// options array's SetSize (005D9250, registered on mfc::CArray).
#include "unit_ai.h"

#include "combat.h"
#include "object_tables.h"
#include "spell.h"

#include "ei/hook.h"

#include <bit>
#include <initializer_list>

namespace {
constexpr int kNaturalSpellsOnly = 0x10;    // natural weapon type of units that only cast
constexpr int kNoKind = 8;
constexpr float kAttackSpellWeight = 1.3f;  // 0073FF20
constexpr float kHelpWeight = 1.0f;         // 0073B84C
constexpr float kHealWeight = 10.0f;        // 0073BAAC

// The option kind per spell type (005CEDC0's byte table 005CEE4C through its jump table).
constexpr int kSpellKinds[42] = {1, 1, 1, 2, 2, 2, 3, 3, 3, 5, 5, 5, 6, 6, 6, 6, 6, 6, 8, 8, 8,
                                 8, 8, 8, 7, 4, 5, 8, 8, 5, 4, 6, 4, 6, 8, 8, 8, 8, 8, 8, 1, 1};

// 00522B20 (fastcall, AX): the largest of damage[1..7] (units area, not rewritten).
__declspec(naked) std::uint16_t __fastcall MaxDamage(const std::uint16_t*) EI_ORIGINAL(0x00522B20)

double __fastcall OptionWeight(const AIAttackOption*);
__declspec(naked) void __fastcall OptionsSetSizeAt(AIAttackOptions*, void*, int, int) EI_ORIGINAL(0x005D9250)

int SpellType(const CSpell* spell) { return static_cast<int>(spell->prototypeId_ >> 16); }

} // namespace

double AIAttackOption::Weight() const {
    switch (kind) {
    case 0:
        return MaxDamage(damage);
    case 1:
    case 2:
    case 3:
        return static_cast<double>(MaxDamage(damage)) * kAttackSpellWeight;
    case 4:
    case 5:
    case 6:
        return kHelpWeight;
    case 7:
        return kHealWeight;
    default:
        return 0.0;
    }
}

void AIAttackOption::Load(CStream* stream) {
    if (stream->version_ < 0x5A) {
        stream->Read(&part, 4);
        std::uint32_t dropped;
        stream->Read(&dropped, 4);
        stream->Read(damage, sizeof(damage));
        kind = part >= 0 ? 1 : 0;
        range = 1.0f;
        maxCount = 0;
        radius = 0.0f;
        effect = 0;
        return;
    }
    stream->Read(&kind, 4);
    stream->Read(&part, 4);
    stream->Read(&range, 4);
    stream->Read(&maxCount, 4);
    stream->Read(&radius, 4);
    stream->Read(&effect, 4);
    stream->Read(damage, sizeof(damage));
}

void AIAttackOption::Save(CStream* stream) {
    // The fields after the first go through a stack copy, as the original writes them.
    stream->Write(&kind, 4);
    for (std::uint32_t value : {std::bit_cast<std::uint32_t>(part), std::bit_cast<std::uint32_t>(range),
                                std::bit_cast<std::uint32_t>(maxCount), std::bit_cast<std::uint32_t>(radius),
                                std::bit_cast<std::uint32_t>(effect)})
        stream->Write(&value, 4);
    stream->Write(damage, sizeof(damage));
}

void AIAttention::InitOption(AIAttackOption* option, int kind, int part, float range, int maxCount, float radius,
                             int effect) {
    option->part = part;
    option->range = range;
    option->kind = kind;
    option->maxCount = maxCount;
    option->radius = radius;
    option->effect = effect;
}

std::uint16_t* AIAttention::AddWeaponOption(int kind, int part, float range, int maxCount, float radius, int effect) {
    weapons.SetSize(weapons.size + 1, -1);
    AIAttackOption& option = weapons.data[weapons.size - 1];
    InitOption(&option, kind, part, range, maxCount, radius, effect);
    return option.damage;
}

std::uint16_t* AIAttention::AddSpellOption(int kind, int part, float range, int maxCount, float radius, int effect) {
    spells.SetSize(spells.size + 1, -1);
    AIAttackOption& option = spells.data[spells.size - 1];
    InitOption(&option, kind, part, range, maxCount, radius, effect);
    return option.damage;
}

int AIAttention::SpellKind(const CSpell* spell) {
    if (!spell) return 0;
    int type = SpellType(spell);
    return static_cast<unsigned>(type) < 42 ? kSpellKinds[type] : kNoKind;
}

void AIAttention::Build(CUnit* unit) {
    auto* server = static_cast<CUnitServer*>(unit->controller);
    weapons.RemoveAll();
    spells.RemoveAll();
    if (!(IsTextKey(unit->id_) & 0xFF)) {
        if (server->Combat().naturalWeapon != kNaturalSpellsOnly) {
            float range = server->AttackRange();
            OptionsSetSizeAt(&weapons, nullptr, weapons.size + 1, -1);
            AIAttackOption& option = weapons.data[weapons.size - 1];
            InitOption(&option, 0, -1, range, 0, 0.0f, 0);
            server->GetDamageByType(reinterpret_cast<std::int16_t*>(option.damage));
        }
        for (std::uint32_t slot = 0; slot < 8; ++slot) {
            auto* spell = static_cast<const CSpell*>(unit->GetSpellObject(slot));
            if (!spell) continue;
            int kind = SpellKind(spell);
            if (kind == kNoKind) continue;
            float range = std::bit_cast<float>(spell->range_);
            if (server->Combat().naturalWeapon == kNaturalSpellsOnly) range = server->Combat().reach;
            std::uint32_t count = spell->targetCount_;
            int maxCount = static_cast<int>((count < 1 ? 1 : count) - 1);
            std::uint32_t radius = 0;
            int effect = 0;
            switch (kind) {
            case 2:
                radius = spell->radius_;
                break;
            case 3:
                radius = spell->radius_;
                effect = static_cast<int>(spell->duration_);
                break;
            case 4:
            case 5:
            case 6:
                effect = SpellType(spell);
                break;
            case 7:
                radius = spell->damage_;
                break;
            default:
                break;
            }
            float radiusValue = std::bit_cast<float>(radius);
            switch (kind) {
            case 0:
            case 1:
            case 2:
            case 3: {
                OptionsSetSizeAt(&weapons, nullptr, weapons.size + 1, -1);
                AIAttackOption& option = weapons.data[weapons.size - 1];
                InitOption(&option, kind, static_cast<int>(slot), range, maxCount, radiusValue, effect);
                server->GetSpellDamage(slot, reinterpret_cast<std::int16_t*>(option.damage));
                break;
            }
            case 4:
                AddWeaponOption(4, static_cast<int>(slot), range, maxCount, radiusValue, effect);
                break;
            case 5:
            case 6:
            case 7:
                AddSpellOption(kind, static_cast<int>(slot), range, maxCount, radiusValue, effect);
                break;
            default:
                break;
            }
        }
    } else {
        float range = server->AttackRange();
        std::uint16_t* damage = AddWeaponOption(0, -1, range, 0, 0.0f, 0);
        server->GetDamageByType(reinterpret_cast<std::int16_t*>(damage));
    }
    healWeight = 0.0f;
    for (int i = 0; i < weapons.size; ++i) {
        float weight = static_cast<float>(OptionWeight(&weapons.data[i]));
        healWeight = healWeight >= weight ? healWeight : weight;
    }
}

// 005D9B80 (fastcall, plain RET): the count, the array sized to it (SetSize inlined), then each
// option (005CE860, through its address).
void __fastcall LoadAttackOptions(AIAttackOptions* options, CStream* stream) {
    int count;
    stream->Read(&count, 4);
    options->SetSize(count, -1);
    for (int i = 0; i < options->size; ++i) options->data[i].Load(stream);
}
EI_REPLACE(0x005D9B80, LoadAttackOptions);

namespace {
// 005CE9B0 (thiscall on the option, ST0).
double __fastcall OptionWeight(const AIAttackOption* option) { return option->Weight(); }
} // namespace

EI_REPLACE_METHOD(0x005CEED0, AIAttention::Build);
EI_REPLACE_METHOD(0x005CEA30, AIAttention::InitOption);
EI_REPLACE_METHOD(0x005CEA60, AIAttention::AddWeaponOption);
EI_REPLACE_METHOD(0x005CEBD0, AIAttention::AddSpellOption);
EI_REPLACE_METHOD(0x005CEDC0, AIAttention::SpellKind);
EI_REPLACE(0x005CE9B0, OptionWeight);
EI_REPLACE_METHOD(0x005D9250, AIAttackOptions::SetSize);
EI_REPLACE_METHOD(0x005CE860, AIAttackOption::Load);
EI_REPLACE_METHOD(0x005CE920, AIAttackOption::Save);
