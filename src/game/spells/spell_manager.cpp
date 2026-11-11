// The spell manager and the spell containers: the manager's own code and the container's lifetime
// (CreateSpellObject, the factory, Create / Destroy), the spell constructor, and spells from text. See
// docs/notes/items_spells.md.
#include "game/spells/spell.h"

#include "game/console/console.h"
#include "game/items/item.h"
#include "game/items/item_db.h"
#include "game/spells/spell_db.h"
#include "game/items/item_manager.h"
#include "engine/core/manager_context.h"
#include "engine/core/text_split.h"
#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/str.h"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <memory>
#include <new>
#include <string>
#include <vector>

namespace {

constexpr const char* kSpellManagerClassName = "CSpellManager";

template <typename Record>
std::vector<std::string> ListRecordNames(const itemdb::RecordTable<Record>& table) {
    std::vector<std::string> names(table.Count());
    for (std::uint32_t i = 0; i < table.Count(); ++i) names[i] = table.At(i)->name;
    return names;
}

} // namespace

Spell* SpellManager::CreateSpellObject() { return CreateObjectAs<Spell>(SpellObjectType::Spell, -1); }

EngineObject* SpellManager::NewObject(int objectType) {
    switch (static_cast<SpellObjectType>(objectType)) {
    case SpellObjectType::Spell: return new Spell();
    case SpellObjectType::Container: return new SpellContainer();
    default: break;
    }
    return nullptr;
}

const char* SpellManager::ClassName() { return kSpellManagerClassName; }
ClassId SpellManager::GetClassId() { return ClassId::SpellManager; }

std::vector<std::string> SpellManager::ListSpellNames() { return ListRecordNames(g_spellDatabase.spells); }
std::vector<std::string> SpellManager::ListModifierNames() { return ListRecordNames(g_spellDatabase.modifiers); }

// The spell and the name are members, constructed before the body (the game builds them after the code
// string) and destroyed after it (the game: the name and the spell before the string); neither allocates.
SpellContainer::SpellContainer() {
    // The code string: empty, with room for 8 bytes.
    SpellContainer::Create();
}

SpellContainer::~SpellContainer() {
    // (ClearState is empty and called directly here.)
    prototype = nullptr;
    modifiers.clear();
    spell.Destroy();
}

const char* SpellContainer::ClassName() { return "CSpellContainer"; }
ClassId SpellContainer::GetClassId() { return ClassId::SpellContainer; }

// --- Spell construction ------------------------------------------------------------------------------

// Spell construction: what a cast with modifiers runs before the spell exists. The script's CastSpellUnit /
// CastSpellPoint ("healing{e2;e2}") and the spell constructor screen parse the text and then drive
// SpellConstructor: SetManager, BeginConstruct (a new SpellContainer for the prototype), AddModifier per
// modifier (each rebuilds the container's spell), EndConstruct, Spell::Clone into a new spell object,
// SetManager(null).

namespace {
// The game's own messages (passed by address).
const char* const kInvalidRune = "Invalid rune";
const char* const kNoSpellManager = "CSpellConstructor::BeginConstructSpell.\n There is no valid spell manager.\n";
const char* const kNoSpellPrototype = "No valid spell prototype\n";
const char* const kNoManagerOrContainer = "No valid spell manager or spell container.\n";
const char* const kNoSpellConstructor = "No valid spell constructor\n";

// value * factor truncated as _ftol does: through a 64-bit integer, 0 for a NaN.
int ScaleTruncate(std::uint32_t value, float factor) {
    return static_cast<int>(static_cast<std::int64_t>(factor * value));
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

std::string_view SpellContainer::GetName() { return name; }

bool SpellContainer::AddModifier(itemdb::SpellModifierRecord* modifier) {
    if (!modifier) game::FatalExit("%s", kInvalidRune);
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
    Spell* currentSpell = &spell;
    itemdb::SpellTable& spells = g_spellDatabase.spells;
    int index = spells.IndexOf(spells.Named(prototype->code));
    currentSpell->prototypeId = static_cast<std::uint32_t>(index & 0xFF) << 16;
    currentSpell->InitFromPrototype(prototype);

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
        int modifierIndex = spellDatabaseModifiers.IndexOf(spellDatabaseModifiers.Named(modifier->code));
        char code = static_cast<char>(modifierIndex + 1);
        codes.push_back(code);
    }

    float area = currentSpell->radius;
    if (area > 0.0f) currentSpell->radius = AreaToRadius(area);

    // mana = max(prototype's mana, mana): the select keeps the computed value when the prototype's is
    // smaller or either is NaN.
    float base = prototype->mana;
    currentSpell->manaCost = MaxKeepB(base, currentSpell->manaCost);

    // The codes sorted as signed chars.
    std::sort(codes.begin(), codes.end());

    currentSpell->modifiers = {};
    for (int i = 0; i < 8; ++i) {
        char c = codes[i];
        if (c == 0) break;
        currentSpell->modifiers[i] = c;
    }
}

SpellContainer* SpellManager::NewContainer() {
    return CreateObjectAs<SpellContainer>(SpellObjectType::Container, -1);
}

Spell* SpellManager::CreateSpell(const itemdb::SpellRecord* prototype, const std::vector<itemdb::SpellModifierRecord*>& modifiers) {
    SpellConstructor* constructor = context->GetSpellConstructor();
    if (!constructor) game::FatalExit("%s", kNoSpellConstructor);
    constructor->SetManager(this);
    constructor->BeginConstruct(prototype);
    int added = 0;
    for (itemdb::SpellModifierRecord* modifier : modifiers) {
        if (added >= 8) break;
        constructor->AddModifier(modifier);
        ++added;
    }
    constructor->EndConstruct();
    Spell* spell = constructor->container->spell.Clone(this);
    constructor->SetManager(nullptr);
    return spell;
}

SpellConstructor::~SpellConstructor() { SpellConstructor::Destroy(); }

const char* SpellConstructor::ClassName() { return "CSpellConstructor"; }
ClassId SpellConstructor::GetClassId() { return ClassId::SpellConstructor; }

void SpellConstructor::Create() {
    container.Reset();
    manager = nullptr;
}

void SpellConstructor::Destroy() {
    container.Reset();
    if (manager) --manager->refCount;
    manager = nullptr;
}

void SpellConstructor::SetManager(SpellManager* newManager) {
    container.Reset();
    if (manager) --manager->refCount;
    manager = newManager;
    if (newManager) newManager->AddRef();
}

void SpellConstructor::BeginConstruct(const itemdb::SpellRecord* prototype) {
    if (!manager) game::FatalExit("%s", kNoSpellManager);
    if (!prototype) game::FatalExit("%s", kNoSpellPrototype);
    SpellContainer* newContainer = manager->NewContainer();
    container.Assign(newContainer);
    container->prototype = prototype;
}

void SpellConstructor::EndConstruct() {
    if (!manager || !container) game::FatalExit("%s", kNoManagerOrContainer);
    container->Build();
}

bool SpellConstructor::AddModifier(itemdb::SpellModifierRecord* modifier) {
    if (!container) return false;
    if (!container->AllowsModifier(modifier)) return false;
    container->AddModifier(modifier);
    container->Build();
    return true;
}

// --- Spells from text --------------------------------------------------------------------------------

// Spells from text: "healing{e2;e2}" as the scripts' CastSpellUnit / CastSpellPoint and
// the potions' prototypes (ItemConstructor::FillItem) write them.

namespace {

// The spells and modifiers by name; operator[] files a missing name with a null record.
std::unordered_map<std::string, itemdb::SpellRecord*>& SpellNameMap() { return g_spellDatabase.spells.names; }
std::unordered_map<std::string, itemdb::SpellModifierRecord*>& ModifierNameMap() { return g_spellDatabase.modifiers.names; }

// Splits "name{a;b}" into the trimmed name (in `text`) and the modifier names (in `codes`).
void ParseSpellText(std::string& text, std::vector<std::string>& codes) {
    int brace = str::Find(text, '{');
    if (brace == -1) {
        str::TrimLeft(text);
        str::TrimRight(text);
        return;
    }
    std::string inner = str::Right(text, static_cast<int>(text.size()) - brace - 1);
    str::TrimLeft(inner);
    str::TrimRight(inner);
    str::TrimRight(inner, '}');
    str::TrimRight(inner);
    codes = text::Split(inner, ';');
    text = str::Left(text, brace);
    str::TrimLeft(text);
    str::TrimRight(text);
}

// The modifier record named `name` (find_or_insert with a null record).
itemdb::SpellModifierRecord* FindModifier(std::string_view name) {
    std::string key(name);
    return ModifierNameMap()[key];
}

} // namespace

Spell* SpellManager::CreateSpellFromText(std::string_view source) {
    std::string text(source);
    std::vector<std::string> codes;
    ParseSpellText(text, codes);

    SpellConstructor* constructor = context->GetSpellConstructor();
    if (!constructor) game::FatalExit("%s", kNoSpellConstructor);

    itemdb::SpellRecord* prototype;
    {
        std::string key(text);
        prototype = SpellNameMap()[key];
    }
    if (!prototype) {
        game::Warning("No spell \"%s\" in the spell database: no spell made", text);
        return nullptr;
    }
    constructor->SetManager(this);
    constructor->BeginConstruct(prototype);

    for (const std::string& code : codes) {
        if (code.empty()) continue;
        constructor->AddModifier(FindModifier(code));
    }

    constructor->EndConstruct();
    Spell* spell = constructor->container->spell.Clone(this);
    constructor->SetManager(nullptr);
    return spell;
}
