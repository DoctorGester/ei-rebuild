// The spell manager and the spell containers: the manager's own code and the container's lifetime
// (CreateSpellObject, the factory, Create / Destroy, the sequences it keeps, spell.h's SpellSequence), the spell
// constructor, and spells from text. See docs/notes/items_spells.md.
#include "spell.h"

#include "console.h"
#include "item.h"
#include "item_db.h"
#include "item_manager.h"
#include "manager_context.h"
#include "text_split.h"
#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/pc24.h"
#include "lib/str.h"

#include <cstring>
#include <iterator>
#include <memory>
#include <new>
#include <string>
#include <vector>

namespace {

constexpr const char* kNoSequenceMessage = "Can't find spell sequence to create child spell.\n";
constexpr const char* kSpellManagerClassName = "CSpellManager";
constexpr const char* kSpellManagerFolder = "spells";

template <typename Record>
std::vector<std::string> ListRecordNames(const itemdb::CodeIndexedTable<Record>& table) {
    std::vector<std::string> names(table.Count());
    for (std::uint32_t i = 0; i < table.Count(); ++i) names[i] = table.At(i)->name;
    return names;
}

} // namespace

SpellSequence::~SpellSequence() {
    for (auto& [low, container] : containers) {
        if (!container) break;
        container->Release();
    }
}

Spell* SpellManager::CreateSpellObject(int newId) {
    auto* spell = CreateObjectAs<Spell>(SpellObjectType::Spell, -1);
    if (newId == -1) return spell;
    const auto packed = static_cast<std::uint32_t>(newId);
    const std::uint32_t low = packed & 0xFFFF;
    spell->prototypeId = packed;
    if (low == 0) {
        spell->InitFromPrototype(itemdb::SpellAt(packed >> 16));
        return spell;
    }
    const std::uint32_t key = packed & 0xFFFF0000;
    if (key > maxSequenceKey) maxSequenceKey = key;
    SpellSequence* sequence = sequences[key];
    if (!sequence) {
        game::FatalError(kNoSequenceMessage);
        return spell;
    }
    if (low > sequence->maxLow) sequence->maxLow = low;
    // A new entry has no container: CopyTo then faults (this = 0x30), as the game does.
    SpellContainer* container = sequence->containers[low];
    container->spell.CopyTo(spell);
    return spell;
}

EngineObject* SpellManager::NewObject(int objectType) {
    switch (static_cast<SpellObjectType>(objectType)) {
    case SpellObjectType::Spell: return new Spell();
    case SpellObjectType::Container: return new SpellContainer();
    default: break;
    }
    return nullptr;
}

void SpellManager::Destroy() {
    for (auto& [key, sequence] : sequences) {
        if (!sequence) break;
        delete sequence;
    }
    sequences.clear();
    maxSequenceKey = 0;
    ResourceManager::Destroy();
}

SpellManager::SpellManager() {
    maxSequenceKey = 0;
}

SpellManager::~SpellManager() {
    for (auto& [key, sequence] : sequences) {
        if (!sequence) break;
        delete sequence;
    }
    sequences.clear();
    maxSequenceKey = 0;
    ResourceManager::Destroy();
}

const char* SpellManager::ClassName() { return kSpellManagerClassName; }
ClassId SpellManager::GetClassId() { return ClassId::SpellManager; }
const char* SpellManager::GetFolderName() { return kSpellManagerFolder; }

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
    Spell* currentSpell = &spell;
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

SpellContainer* SpellManager::NewContainer() {
    return CreateObjectAs<SpellContainer>(SpellObjectType::Container, -1);
}

Spell* SpellManager::CreateSpell(const itemdb::SpellRecord* prototype, const std::vector<itemdb::SpellModifierRecord*>* modifiers) {
    SpellConstructor* constructor = context->GetSpellConstructor();
    if (!constructor) return game::FatalErrorAs<Spell>(kNoSpellConstructor);
    constructor->SetManager(this);
    if (!constructor->BeginConstruct(prototype))
        return game::FatalErrorAs<Spell>(kCantBegin, prototype->code.c_str());
    int added = 0;
    for (itemdb::SpellModifierRecord* modifier : *modifiers) {
        if (added >= 8) break;
        constructor->AddModifier(modifier);
        ++added;
    }
    if (!constructor->EndConstruct()) return game::FatalErrorAs<Spell>(kCantEnd);
    Spell* spell = constructor->container->spell.Clone(this);
    constructor->SetManager(nullptr);
    return spell;
}

SpellConstructor::SpellConstructor() {
    manager = nullptr;
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

bool SpellConstructor::BeginConstruct(const itemdb::SpellRecord* prototype) {
    if (!manager) return game::FatalError(kNoSpellManager);
    if (!prototype) return game::FatalError(kNoSpellPrototype);
    SpellContainer* newContainer = manager->NewContainer();
    container.Assign(newContainer);
    container->prototype = prototype;     // no null check
    return true;
}

bool SpellConstructor::EndConstruct() {
    if (manager && container) {
        container->Build();
        return true;
    }
    return game::FatalError(kNoManagerOrContainer);
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
std::unordered_map<std::string, itemdb::SpellRecord*>& SpellNameMap() { return g_spellDatabase.spells.byCode; }
std::unordered_map<std::string, itemdb::SpellModifierRecord*>& ModifierNameMap() { return g_spellDatabase.modifiers.byCode; }

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
    if (!constructor) return game::FatalErrorAs<Spell>(kNoSpellConstructor);
    constructor->SetManager(this);

    itemdb::SpellRecord* prototype;
    {
        std::string key(text);
        prototype = SpellNameMap()[key];
    }
    if (!constructor->BeginConstruct(prototype))
        return game::FatalErrorAs<Spell>(kCantBegin, text.c_str());

    for (const std::string& code : codes) {
        if (code.empty()) continue;
        constructor->AddModifier(FindModifier(code));
    }

    if (!constructor->EndConstruct()) return game::FatalErrorAs<Spell>(kCantEnd);
    Spell* spell = constructor->container->spell.Clone(this);
    constructor->SetManager(nullptr);
    return spell;
}
