// The unit AI's attack options (AIAttention, unit_ai.h): listing them from the unit's weapon and
// spells, and the option helpers.
#include "unit_ai.h"

#include "combat.h"
#include "object_tables.h"
#include "spell.h"

#include <bit>
#include <initializer_list>

std::uint16_t MaxDamage(const std::uint16_t*);
namespace {
constexpr int kNaturalSpellsOnly = 0x10;    // natural weapon type of units that only cast
constexpr float kAttackSpellWeight = 1.3f;
constexpr float kHelpWeight = 1.0f;
constexpr float kHealWeight = 10.0f;

// The option kind per spell type.
constexpr AIOptionKind kSpellKinds[42] = {
    kOptionSpellAttack, kOptionSpellAttack, kOptionSpellAttack, kOptionAreaAttack, kOptionAreaAttack, kOptionAreaAttack,
    kOptionLastingArea, kOptionLastingArea, kOptionLastingArea, kOptionProtect, kOptionProtect, kOptionProtect,
    kOptionEnhance, kOptionEnhance, kOptionEnhance, kOptionEnhance, kOptionEnhance, kOptionEnhance,
    kOptionUnused, kOptionUnused, kOptionUnused, kOptionUnused, kOptionUnused, kOptionUnused,
    kOptionHeal, kOptionCurse, kOptionProtect, kOptionUnused, kOptionUnused, kOptionProtect,
    kOptionCurse, kOptionEnhance, kOptionCurse, kOptionEnhance, kOptionUnused, kOptionUnused,
    kOptionUnused, kOptionUnused, kOptionUnused, kOptionUnused, kOptionSpellAttack, kOptionSpellAttack};

double OptionWeight(const AIAttackOption*);

int SpellType(const CSpell* spell) { return static_cast<int>(spell->prototypeId_ >> 16); }

double OptionWeight(const AIAttackOption* option) { return option->Weight(); }

} // namespace

double AIAttackOption::Weight() const {
    switch (kind) {
    case kOptionWeapon:
        return MaxDamage(damage.words);
    case kOptionSpellAttack:
    case kOptionAreaAttack:
    case kOptionLastingArea:
        return static_cast<double>(MaxDamage(damage.words)) * kAttackSpellWeight;
    case kOptionCurse:
    case kOptionProtect:
    case kOptionEnhance:
        return kHelpWeight;
    case kOptionHeal:
        return kHealWeight;
    default:
        return 0.0;
    }
}

void AIAttackOption::Load(CStream* stream) {
    if (stream->version_ < 0x5A) {
        stream->ReadValue(part);
        std::uint32_t dropped;
        stream->ReadValue(dropped);
        stream->ReadValue(damage);
        kind = part >= 0 ? kOptionSpellAttack : kOptionWeapon;
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

AIOptionKind AIAttention::SpellKind(const CSpell* spell) {
    if (!spell) return kOptionWeapon;
    int type = SpellType(spell);
    return static_cast<unsigned>(type) < 42 ? kSpellKinds[type] : kOptionUnused;
}

void AIAttention::Build(CUnit* unit) {
    auto* server = unit->ServerController();
    weapons.clear();
    spells.clear();
    if (!(IsTextKey(unit->id_) & 0xFF)) {
        if (server->stats.naturalWeapon != kNaturalSpellsOnly) {
            float range = server->AttackRange();
            weapons.resize(std::ssize(weapons) + 1);
            AIAttackOption& option = weapons[std::ssize(weapons) - 1];
            InitOption(&option, kOptionWeapon, -1, range, 0, 0.0f, 0);
            server->GetDamageByType(&option.damage);
        }
        for (std::uint32_t slot = 0; slot < 8; ++slot) {
            const CSpell* spell = unit->GetSpellObject(slot);
            if (!spell) continue;
            AIOptionKind kind = SpellKind(spell);
            if (kind == kOptionUnused) continue;
            float range = spell->range_;
            if (server->stats.naturalWeapon == kNaturalSpellsOnly) range = server->stats.combat[UnitStats::kReach];
            std::uint32_t count = spell->targetCount_;
            int maxCount = static_cast<int>((count < 1 ? 1 : count) - 1);
            float radius = 0.0f;
            int effect = 0;
            switch (kind) {
            case kOptionAreaAttack:
                radius = spell->radius_;
                break;
            case kOptionLastingArea:
                radius = spell->radius_;
                effect = static_cast<int>(spell->duration_);
                break;
            case kOptionCurse:
            case kOptionProtect:
            case kOptionEnhance:
                effect = SpellType(spell);
                break;
            case kOptionHeal:
                radius = spell->damage_;
                break;
            default:
                break;
            }
            switch (kind) {
            case kOptionWeapon:
            case kOptionSpellAttack:
            case kOptionAreaAttack:
            case kOptionLastingArea: {
                weapons.resize(std::ssize(weapons) + 1);
                AIAttackOption& option = weapons[std::ssize(weapons) - 1];
                InitOption(&option, kind, static_cast<int>(slot), range, maxCount, radius, effect);
                server->GetSpellDamage(slot, &option.damage);
                break;
            }
            case 4:
                AddWeaponOption(kOptionCurse, static_cast<int>(slot), range, maxCount, radius, effect);
                break;
            case 5:
            case 6:
            case 7:
                AddSpellOption(kind, static_cast<int>(slot), range, maxCount, radius, effect);
                break;
            default:
                break;
            }
        }
    } else {
        float range = server->AttackRange();
        DamageWords* damage = AddWeaponOption(kOptionWeapon, -1, range, 0, 0.0f, 0);
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
