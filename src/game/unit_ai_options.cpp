// The unit AI's attack options (AIAttention, unit_ai.h): listing them from the unit's weapon and
// spells, and the option helpers.
#include "unit_ai.h"

#include "combat.h"
#include "object_tables.h"
#include "spell.h"

#include <bit>
#include <initializer_list>

std::uint16_t __fastcall MaxDamage(const std::uint16_t*);
namespace {
constexpr int kNaturalSpellsOnly = 0x10;    // natural weapon type of units that only cast
constexpr int kNoKind = 8;
constexpr float kAttackSpellWeight = 1.3f;
constexpr float kHelpWeight = 1.0f;
constexpr float kHealWeight = 10.0f;

// The option kind per spell type.
constexpr int kSpellKinds[42] = {1, 1, 1, 2, 2, 2, 3, 3, 3, 5, 5, 5, 6, 6, 6, 6, 6, 6, 8, 8, 8,
                                 8, 8, 8, 7, 4, 5, 8, 8, 5, 4, 6, 4, 6, 8, 8, 8, 8, 8, 8, 1, 1};

double OptionWeight(const AIAttackOption*);

int SpellType(const CSpell* spell) { return static_cast<int>(spell->prototypeId_ >> 16); }

} // namespace

double AIAttackOption::Weight() const {
    switch (kind) {
    case 0:
        return MaxDamage(damage.words);
    case 1:
    case 2:
    case 3:
        return static_cast<double>(MaxDamage(damage.words)) * kAttackSpellWeight;
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
        stream->Read(&damage, sizeof(damage));
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
    stream->Read(&damage, sizeof(damage));
}

void AIAttackOption::Save(CStream* stream) const {
    // The fields after the first go through a stack copy.
    stream->Write(&kind, 4);
    for (std::uint32_t value : {std::bit_cast<std::uint32_t>(part), std::bit_cast<std::uint32_t>(range),
                                std::bit_cast<std::uint32_t>(maxCount), std::bit_cast<std::uint32_t>(radius),
                                std::bit_cast<std::uint32_t>(effect)})
        stream->Write(&value, 4);
    stream->Write(&damage, sizeof(damage));
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

DamageWords* AIAttention::AddWeaponOption(int kind, int part, float range, int maxCount, float radius, int effect) {
    weapons.resize(std::ssize(weapons) + 1);
    AIAttackOption& option = weapons[std::ssize(weapons) - 1];
    InitOption(&option, kind, part, range, maxCount, radius, effect);
    return &option.damage;
}

DamageWords* AIAttention::AddSpellOption(int kind, int part, float range, int maxCount, float radius, int effect) {
    spells.resize(std::ssize(spells) + 1);
    AIAttackOption& option = spells[std::ssize(spells) - 1];
    InitOption(&option, kind, part, range, maxCount, radius, effect);
    return &option.damage;
}

int AIAttention::SpellKind(const CSpell* spell) {
    if (!spell) return 0;
    int type = SpellType(spell);
    return static_cast<unsigned>(type) < 42 ? kSpellKinds[type] : kNoKind;
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
            InitOption(&option, 0, -1, range, 0, 0.0f, 0);
            server->GetDamageByType(&option.damage);
        }
        for (std::uint32_t slot = 0; slot < 8; ++slot) {
            const CSpell* spell = unit->GetSpellObject(slot);
            if (!spell) continue;
            int kind = SpellKind(spell);
            if (kind == kNoKind) continue;
            float range = spell->range_;
            if (server->stats.naturalWeapon == kNaturalSpellsOnly) range = server->stats.combat[UnitStats::kReach];
            std::uint32_t count = spell->targetCount_;
            int maxCount = static_cast<int>((count < 1 ? 1 : count) - 1);
            float radius = 0.0f;
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
            switch (kind) {
            case 0:
            case 1:
            case 2:
            case 3: {
                weapons.resize(std::ssize(weapons) + 1);
                AIAttackOption& option = weapons[std::ssize(weapons) - 1];
                InitOption(&option, kind, static_cast<int>(slot), range, maxCount, radius, effect);
                server->GetSpellDamage(slot, &option.damage);
                break;
            }
            case 4:
                AddWeaponOption(4, static_cast<int>(slot), range, maxCount, radius, effect);
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
        DamageWords* damage = AddWeaponOption(0, -1, range, 0, 0.0f, 0);
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
    stream->Read(&count, 4);
    options->resize(count);
    for (int i = 0; i < std::ssize(*options); ++i) (*options)[i].Load(stream);
}

namespace {
double OptionWeight(const AIAttackOption* option) { return option->Weight(); }
} // namespace
