// Spell construction: what a cast with modifiers runs before the spell exists. The script's CastSpellUnit /
// CastSpellPoint ("healing{e2;e2}") and the spell constructor screen parse the text and then drive
// CSpellConstructor: SetManager, BeginConstruct (a new SpellContainer for the prototype), AddModifier per
// modifier (each rebuilds the container's spell), EndConstruct, CSpell::Clone into a new spell object,
// SetManager(null).
#include "item.h"
#include "item_db.h"
#include "manager_context.h"
#include "spell.h"

#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/pc24.h"


#include <cstring>
#include <iterator>

namespace {
// The game's own messages (passed by address).
const char* const kInvalidRune = "Invalid rune";
const char* const kNoSpellManager = "CSpellConstructor::BeginConstructSpell.\n There is no valid spell manager.\n";
const char* const kNoSpellPrototype = "No valid spell prototype\n";
const char* const kNoManagerOrContainer = "No valid spell manager or spell container.\n";
const char* const kNoSpellConstructor = "No valid spell constructor\n";
const char* const kCantBegin = "Can't begin spell construction with prototype \"%s\"\n";
const char* const kCantEnd = "Can't end spell construction\n";




// value * factor (rounded once) truncated as _ftol does: through a 64-bit integer, 0 for a NaN.
int ScaleTruncate(std::uint32_t value, float factor) {
    return static_cast<int>(static_cast<std::int64_t>(pc24::Mul(factor, value)));
}

// sqrt(area / pi).
float AreaToRadius(float area) { return std::sqrt(area / 3.1415927f); }

// field += strength * base, stored as a float, then kept only when above zero (a branch-free select: 0 <
// value or NaN keeps it, anything else gives +0).
void AddScaledPositive(float& field, float strength, float base) {
    field = strength * base + field;
    field = MaxKeepB(0.0f, field);
}

// The code letters of a modifier record.
char Letter(const itemdb::SpellModifierRecord* modifier, int i) { return modifier->code[i]; }
} // namespace


void CSpell::Destroy() {
    ClearState();
    std::memset(modifiers, 0, sizeof(modifiers));
}

void CSpell::CopyTo(CSpell* target) {
    target->Destroy();
    std::memcpy(&target->prototypeIndex, &prototypeIndex, 0x54 - 0x1C);
    std::memset(target->modifiers, 0, sizeof(target->modifiers));
    for (int i = 0; i < 8; ++i) {
        if (modifiers[i] == 0) break;
        target->modifiers[i] = modifiers[i];
    }
    target->prototypeId = prototypeId;
}

CSpell* CSpell::Clone(CSpellManager* manager) {
    CSpell* spell = manager->CreateSpellObject(-1);
    CopyTo(spell);
    return spell;
}

void SpellContainer::Create() {
    prototype = nullptr;
    modifiers.clear();
    spell.Create();
}

void SpellContainer::Destroy() {
    ClearState();
    prototype = nullptr;
    modifiers.clear();
    spell.Destroy();
}

const char* SpellContainer::GetName() { return name.c_str(); }

bool SpellContainer::AddModifier(itemdb::SpellModifierRecord* modifier) {
    if (!modifier) return game::FatalError(kInvalidRune);
    modifiers.push_back(modifier);
    return true;
}

bool SpellContainer::AllowsModifier(const itemdb::SpellModifierRecord* modifier) {
    if (!prototype || !modifier) return false;
    switch (Letter(modifier, 0)) {
    case 'a': return prototype->allowedModifiers[2];
    case 'd': return prototype->allowedModifiers[4];
    case 'e': return prototype->allowedModifiers[3];
    case 'r': return prototype->allowedModifiers[0];
    case 't': return prototype->allowedModifiers[1];
    default: return true;
    }
}

void SpellContainer::Build() {
    CSpell* currentSpell = &spell;
    const itemdb::SpellRecord* proto = prototype;
    currentSpell->speed = proto->speed;
    currentSpell->range = proto->range;
    if (proto->targetKind == 'u') {
        currentSpell->targetFlags = lib::Bits(SpellTargetFlag::AtUnits);
    } else if (proto->targetKind == 't') {
        currentSpell->targetFlags = lib::Bits(SpellTargetFlag::AtPoint);
    }
    currentSpell->targetCount = proto->targetCount;
    currentSpell->manaCost = proto->mana;
    currentSpell->complexity = proto->complexity;
    currentSpell->price = proto->price;
    currentSpell->castTime = proto->castTime;
    currentSpell->effectPower = 0x10;

    itemdb::SpellTable& spells = g_spellDatabase.spells;
    int index = spells.IndexOf(spells.FindByCode(proto->code));
    proto = prototype;
    currentSpell->prototypeIndex = static_cast<std::uint8_t>(index);
    currentSpell->damage = proto->damage;
    currentSpell->radius = proto->area;
    currentSpell->duration = proto->duration;
    currentSpell->skillNeeded = static_cast<std::uint32_t>(proto->skill);
    currentSpell->prototypeId = static_cast<std::uint32_t>(index & 0xFF) << 16;

    if (!codes.empty()) codes.clear();

    for (auto& node : modifiers) {
        const itemdb::SpellModifierRecord* modifier = node;
        float strength = modifier->strength;
        switch (Letter(modifier, 0)) {
        case 'a':   // area
            AddScaledPositive(currentSpell->radius, strength, prototype->area);
            break;
        case 'd': { // duration
            int sum = static_cast<int>(currentSpell->duration) + ScaleTruncate(prototype->duration, strength);
            currentSpell->duration = static_cast<std::uint32_t>(sum < 0 ? 0 : sum);
            break;
        }
        case 'e':   // effect
            AddScaledPositive(currentSpell->damage, strength, prototype->damage);
            break;
        case 'f':   // target restrictions
            switch (Letter(modifier, 1)) {
            case 'e': currentSpell->targetFlags |= SpellTargetFlag::EnemiesOnly; break;
            case 'f': currentSpell->targetFlags |= SpellTargetFlag::FriendsOnly; break;
            case 'g': currentSpell->targetFlags |= SpellTargetFlag::NoGoblins; break;
            case 'h': currentSpell->targetFlags |= SpellTargetFlag::NoHumans; break;
            case 'l': currentSpell->targetFlags |= SpellTargetFlag::NoMoli; break;
            case 'o': currentSpell->targetFlags |= SpellTargetFlag::NoOrcs; break;
            default: break;
            }
            break;
        case 'i':   // cast flags
            if (Letter(modifier, 1) == 'c') currentSpell->targetFlags |= SpellTargetFlag::ItemContinuous;
            else if (Letter(modifier, 1) == 't') currentSpell->targetFlags |= SpellTargetFlag::ItemTriggered;
            break;
        case 'm':   // mana
            AddScaledPositive(currentSpell->manaCost, strength, prototype->mana);
            break;
        case 'r':   // range
            AddScaledPositive(currentSpell->range, strength, prototype->range);
            break;
        case 't':
            currentSpell->targetCount = static_cast<std::uint32_t>(static_cast<int>(currentSpell->targetCount) +
                                                           ScaleTruncate(prototype->targetCount, strength));
            break;
        default:
            break;
        }

        currentSpell->complexity += 1;
        currentSpell->skillNeeded += static_cast<std::uint32_t>(modifier->skill);
        currentSpell->manaCost = modifier->mana + currentSpell->manaCost;
        currentSpell->price = modifier->price + currentSpell->price;

        itemdb::SpellModifierTable& spellDatabaseModifiers = g_spellDatabase.modifiers;
        int modifierIndex = spellDatabaseModifiers.IndexOf(spellDatabaseModifiers.FindByCode(modifier->code));
        char code = static_cast<char>(modifierIndex + 1);
        codes.push_back(code);
    }

    float area = currentSpell->radius;
    if (area > 0.0f) currentSpell->radius = AreaToRadius(area);

    // mana = max(prototype's mana, mana): the select keeps the computed value when the prototype's is
    // smaller or either is NaN.
    float base = prototype->mana;
    currentSpell->manaCost = MaxKeepB(base, currentSpell->manaCost);

    // std::sort of the codes (signed chars). Any correct sort gives the same bytes.
    if (!codes.empty()) {
        char* const first = codes.data();
        char* const last = first + codes.size();
        for (char* i = first + 1; i != last; ++i) {
            char value = *i;
            char* j = i;
            while (j != first && value < j[-1]) {
                *j = j[-1];
                --j;
            }
            *j = value;
        }
    }

    std::memset(currentSpell->modifiers, 0, sizeof(currentSpell->modifiers));
    for (int i = 0; i < 8; ++i) {
        char c = codes[i];
        if (c == 0) break;
        currentSpell->modifiers[i] = c;
    }
}

SpellContainer* CSpellManager::NewContainer() {
    return CreateObjectAs<SpellContainer>(SpellObjectType::Container, -1);
}

CSpell* CSpellManager::CreateSpell(const itemdb::SpellRecord* prototype, const std::vector<itemdb::SpellModifierRecord*>* modifiers) {
    CSpellConstructor* constructor = context->SpellConstructor();
    if (!constructor) return game::FatalErrorAs<CSpell>(kNoSpellConstructor);
    constructor->SetManager(this);
    if (!constructor->BeginConstruct(prototype))
        return game::FatalErrorAs<CSpell>(kCantBegin, prototype->code.c_str());
    int added = 0;
    for (itemdb::SpellModifierRecord* modifier : *modifiers) {
        if (added >= 8) break;
        constructor->AddModifier(modifier);
        ++added;
    }
    if (!constructor->EndConstruct()) return game::FatalErrorAs<CSpell>(kCantEnd);
    CSpell* spell = constructor->container->spell.Clone(this);
    constructor->SetManager(nullptr);
    return spell;
}

CSpellConstructor::CSpellConstructor() {
    manager = nullptr;
}

CSpellConstructor::~CSpellConstructor() { CSpellConstructor::Destroy(); }

const char* CSpellConstructor::ClassName() { return "CSpellConstructor"; }
ClassId CSpellConstructor::GetClassId() { return ClassId::SpellConstructor; }

void CSpellConstructor::Create() {
    container.Reset();
    manager = nullptr;
}

void CSpellConstructor::Destroy() {
    container.Reset();
    if (manager) --manager->refCount;
    manager = nullptr;
}

void CSpellConstructor::SetManager(CSpellManager* newManager) {
    container.Reset();
    if (manager) --manager->refCount;
    manager = newManager;
    if (newManager) newManager->AddRef();
}

bool CSpellConstructor::BeginConstruct(const itemdb::SpellRecord* prototype) {
    if (!manager) return game::FatalError(kNoSpellManager);
    if (!prototype) return game::FatalError(kNoSpellPrototype);
    SpellContainer* newContainer = manager->NewContainer();
    container.Assign(newContainer);
    container->prototype = prototype;     // no null check
    return true;
}

bool CSpellConstructor::EndConstruct() {
    if (manager && container) {
        container->Build();
        return true;
    }
    return game::FatalError(kNoManagerOrContainer);
}

bool CSpellConstructor::AddModifier(itemdb::SpellModifierRecord* modifier) {
    if (!container) return false;
    if (!container->AllowsModifier(modifier)) return false;
    container->AddModifier(modifier);
    container->Build();
    return true;
}
