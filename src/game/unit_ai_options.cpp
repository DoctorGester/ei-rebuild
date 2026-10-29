// The unit AI's attack options (AIAttention, unit_ai.h): listing them from the unit's weapon and
// spells, and the option helpers.
#include "unit_ai.h"

#include "combat.h"
#include "object_tables.h"
#include "spell.h"
#include "save_versions.h"

#include <bit>
#include <initializer_list>
#include <iterator>

std::uint16_t MaxDamage(const std::uint16_t*);
namespace {
constexpr int kNaturalSpellsOnly = 0x10;    // natural weapon type of units that only cast
constexpr float kAttackSpellWeight = 1.3f;
constexpr float kHelpWeight = 1.0f;
constexpr float kHealWeight = 10.0f;

// The option kind per spell type.
constexpr AIOptionKind kSpellKinds[42] = {
    AIOptionKind::SpellAttack, AIOptionKind::SpellAttack, AIOptionKind::SpellAttack, AIOptionKind::AreaAttack, AIOptionKind::AreaAttack, AIOptionKind::AreaAttack,
    AIOptionKind::LastingArea, AIOptionKind::LastingArea, AIOptionKind::LastingArea, AIOptionKind::Protect, AIOptionKind::Protect, AIOptionKind::Protect,
    AIOptionKind::Enhance, AIOptionKind::Enhance, AIOptionKind::Enhance, AIOptionKind::Enhance, AIOptionKind::Enhance, AIOptionKind::Enhance,
    AIOptionKind::Unused, AIOptionKind::Unused, AIOptionKind::Unused, AIOptionKind::Unused, AIOptionKind::Unused, AIOptionKind::Unused,
    AIOptionKind::Heal, AIOptionKind::Curse, AIOptionKind::Protect, AIOptionKind::Unused, AIOptionKind::Unused, AIOptionKind::Protect,
    AIOptionKind::Curse, AIOptionKind::Enhance, AIOptionKind::Curse, AIOptionKind::Enhance, AIOptionKind::Unused, AIOptionKind::Unused,
    AIOptionKind::Unused, AIOptionKind::Unused, AIOptionKind::Unused, AIOptionKind::Unused, AIOptionKind::SpellAttack, AIOptionKind::SpellAttack};

double OptionWeight(const AIAttackOption*);

double OptionWeight(const AIAttackOption* option) { return option->Weight(); }

// The option kind of a spell by its type: 1..7, 8 for none; 0 without a spell.
AIOptionKind SpellKind(const CSpell* spell) {
    if (!spell) return AIOptionKind::Weapon;
    unsigned type = static_cast<unsigned>(spell->Type());
    return type < std::size(kSpellKinds) ? kSpellKinds[type] : AIOptionKind::Unused;
}
} // namespace

double AIAttackOption::Weight() const {
    switch (kind) {
    case AIOptionKind::Weapon:
        return MaxDamage(damage.words);
    case AIOptionKind::SpellAttack:
    case AIOptionKind::AreaAttack:
    case AIOptionKind::LastingArea:
        return static_cast<double>(MaxDamage(damage.words)) * kAttackSpellWeight;
    case AIOptionKind::Curse:
    case AIOptionKind::Protect:
    case AIOptionKind::Enhance:
        return kHelpWeight;
    case AIOptionKind::Heal:
        return kHealWeight;
    default:
        return 0.0;
    }
}

void AIAttackOption::Load(CStream* stream) {
    if (stream->version < kVersionSpellOptions) {
        stream->ReadValue(part);
        std::uint32_t dropped;
        stream->ReadValue(dropped);
        stream->ReadValue(damage);
        kind = part >= 0 ? AIOptionKind::SpellAttack : AIOptionKind::Weapon;
        range = 1.0f;
        maxCount = 0;
        radius = 0.0f;
        effect = 0;
        return;
    }
    stream->ReadValue(kind);
    stream->ReadValue(part);
    stream->ReadValue(range);
    stream->ReadValue(maxCount);
    stream->ReadValue(radius);
    stream->ReadValue(effect);
    stream->ReadValue(damage);
}

void AIAttackOption::Save(CStream* stream) const {
    stream->WriteValue(kind);
    for (std::uint32_t value : {std::bit_cast<std::uint32_t>(part), std::bit_cast<std::uint32_t>(range),
                                std::bit_cast<std::uint32_t>(maxCount), std::bit_cast<std::uint32_t>(radius),
                                std::bit_cast<std::uint32_t>(effect)})
        stream->WriteValue(value);
    stream->WriteValue(damage);
}

void AIAttention::InitOption(AIAttackOption* option, AIOptionKind kind, int part, float range, int maxCount, float radius,
                             int effect) {
    option->part = part;
    option->range = range;
    option->kind = kind;
    option->maxCount = maxCount;
    option->radius = radius;
    option->effect = effect;
}

DamageWords* AIAttention::AddWeaponOption(AIOptionKind kind, int part, float range, int maxCount, float radius, int effect) {
    weapons.resize(std::ssize(weapons) + 1);
    AIAttackOption& option = weapons[std::ssize(weapons) - 1];
    InitOption(&option, kind, part, range, maxCount, radius, effect);
    return &option.damage;
}

DamageWords* AIAttention::AddSpellOption(AIOptionKind kind, int part, float range, int maxCount, float radius, int effect) {
    spells.resize(std::ssize(spells) + 1);
    AIAttackOption& option = spells[std::ssize(spells) - 1];
    InitOption(&option, kind, part, range, maxCount, radius, effect);
    return &option.damage;
}


void AIAttention::Build(CUnit* unit) {
    auto* server = unit->ServerController();
    weapons.clear();
    spells.clear();
    if (!IsTextKey(unit->id)) {
        if (server->stats.naturalWeapon != kNaturalSpellsOnly) {
            float range = server->AttackRange();
            weapons.resize(std::ssize(weapons) + 1);
            AIAttackOption& option = weapons[std::ssize(weapons) - 1];
            InitOption(&option, AIOptionKind::Weapon, -1, range, 0, 0.0f, 0);
            server->GetDamageByType(&option.damage);
        }
        for (std::uint32_t slot = 0; slot < 8; ++slot) {
            const CSpell* spell = unit->GetSpellObject(slot);
            if (!spell) continue;
            AIOptionKind kind = SpellKind(spell);
            if (kind == AIOptionKind::Unused) continue;
            float range = spell->range;
            if (server->stats.naturalWeapon == kNaturalSpellsOnly) range = server->stats.combat[UnitStats::kReach];
            std::uint32_t count = spell->targetCount;
            int maxCount = static_cast<int>((count < 1 ? 1 : count) - 1);
            float radius = 0.0f;
            int effect = 0;
            switch (kind) {
            case AIOptionKind::AreaAttack:
                radius = spell->radius;
                break;
            case AIOptionKind::LastingArea:
                radius = spell->radius;
                effect = static_cast<int>(spell->duration);
                break;
            case AIOptionKind::Curse:
            case AIOptionKind::Protect:
            case AIOptionKind::Enhance:
                effect = static_cast<int>(spell->Type());
                break;
            case AIOptionKind::Heal:
                radius = spell->damage;
                break;
            default:
                break;
            }
            switch (kind) {
            case AIOptionKind::Weapon:
            case AIOptionKind::SpellAttack:
            case AIOptionKind::AreaAttack:
            case AIOptionKind::LastingArea: {
                weapons.resize(std::ssize(weapons) + 1);
                AIAttackOption& option = weapons[std::ssize(weapons) - 1];
                InitOption(&option, kind, static_cast<int>(slot), range, maxCount, radius, effect);
                server->GetSpellDamage(slot, &option.damage);
                break;
            }
            case AIOptionKind::Curse:
                AddWeaponOption(AIOptionKind::Curse, static_cast<int>(slot), range, maxCount, radius, effect);
                break;
            case AIOptionKind::Protect:
            case AIOptionKind::Enhance:
            case AIOptionKind::Heal:
                AddSpellOption(kind, static_cast<int>(slot), range, maxCount, radius, effect);
                break;
            default:
                break;
            }
        }
    } else {
        float range = server->AttackRange();
        DamageWords* damage = AddWeaponOption(AIOptionKind::Weapon, -1, range, 0, 0.0f, 0);
        server->GetDamageByType(damage);
    }
    healWeight = 0.0f;
    for (int i = 0; i < std::ssize(weapons); ++i) {
        float weight = static_cast<float>(OptionWeight(&weapons[i]));
        healWeight = healWeight >= weight ? healWeight : weight;
    }
}

// The count, the array sized to it (SetSize inlined), then each option.
void LoadAttackOptions(std::vector<AIAttackOption>* options, CStream* stream) {
    int count;
    stream->ReadValue(count);
    options->resize(count);
    for (int i = 0; i < std::ssize(*options); ++i) (*options)[i].Load(stream);
}
