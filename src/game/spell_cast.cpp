// Spell construction: what a cast with modifiers runs before the spell exists. The script's CastSpellUnit /
// CastSpellPoint ("healing{e2;e2}") and the spell constructor screen parse the text and then drive
// CSpellConstructor: SetManager, BeginConstruct (a new CSpellContainer for the prototype), AddModifier per
// modifier (each rebuilds the container's spell), EndConstruct, CSpell::Clone into a new spell object,
// SetManager(null).
#include "item.h"
#include "item_db.h"
#include "spell.h"

#include "lib/crt.h"

#include "ei/x87.h"

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




// _ftol of value * factor, the value loaded as an unsigned 64-bit integer.
int ScaleTruncate(std::uint32_t value, float factor) { return static_cast<int>(libcrt::Ftol(x87::FromUnsigned(value) * factor)); }

// sqrt(area / pi) (returned unrounded).
double AreaToRadius(float area) { return std::sqrt(area / 3.1415927f); }

// field += strength * base, stored as a float, then kept only when above zero (a branch-free select: 0 <
// value or NaN keeps it, anything else gives +0).
void AddScaledPositive(float& field, float strength, float base) {
    field = strength * base + field;
    if (field <= 0.0f) field = 0.0f;
}

// The code letters of a modifier record.
char Letter(const itemdb::SpellModifierRecord* modifier, int i) { return modifier->code[i]; }
} // namespace


void CSpell::Destroy() {
    ClearState();
    std::memset(modifiers_, 0, sizeof(modifiers_));
}

void CSpell::CopyTo(CSpell* target) {
    target->Destroy();
    std::memcpy(&target->prototypeIndex_, &prototypeIndex_, 0x54 - 0x1C);
    std::memset(target->modifiers_, 0, sizeof(target->modifiers_));
    for (int i = 0; i < 8; ++i) {
        if (modifiers_[i] == 0) break;
        target->modifiers_[i] = modifiers_[i];
    }
    target->prototypeId_ = prototypeId_;
}

CSpell* CSpell::Clone(CSpellManager* manager) {
    CSpell* spell = manager->CreateSpellObject(-1);
    CopyTo(spell);
    return spell;
}

int CSpellContainer::Create() {
    prototype_ = nullptr;
    modifiers_.clear();
    spell_.Create();
    return 1;
}

void CSpellContainer::Destroy() {
    ClearState();
    prototype_ = nullptr;
    modifiers_.clear();
    spell_.Destroy();
}

const char* CSpellContainer::GetName() { return name_.c_str(); }

bool CSpellContainer::AddModifier(itemdb::SpellModifierRecord* modifier) {
    if (!modifier) return game::FatalError(kInvalidRune);
    modifiers_.insert(modifiers_.end(), modifier);
    return true;
}

bool CSpellContainer::AllowsModifier(const itemdb::SpellModifierRecord* modifier) {
    if (!prototype_ || !modifier) return false;
    switch (Letter(modifier, 0)) {
    case 'a': return prototype_->allowedModifiers[2];
    case 'd': return prototype_->allowedModifiers[4];
    case 'e': return prototype_->allowedModifiers[3];
    case 'r': return prototype_->allowedModifiers[0];
    case 't': return prototype_->allowedModifiers[1];
    default: return true;
    }
}

void CSpellContainer::Build() {
    CSpell* spell = &spell_;
    const itemdb::SpellRecord* proto = prototype_;
    spell->speed_ = proto->speed;
    spell->range_ = proto->range;
    if (proto->targetKind == 'u') {
        spell->targetFlags_ = 0x10000000;
    } else if (proto->targetKind == 't') {
        spell->targetFlags_ = 0x20000000;
    }
    spell->targetCount_ = proto->targetCount;
    spell->manaCost_ = proto->mana;
    spell->complexity_ = proto->complexity;
    spell->price_ = proto->price;
    spell->castTime_ = proto->castTime;
    spell->effectPower_ = 0x10;

    itemdb::SpellTable& spells = g_spellDatabase->spells;
    int index = spells.IndexOf(spells.FindByCode(proto->code));
    proto = prototype_;
    spell->prototypeIndex_ = static_cast<std::uint8_t>(index);
    spell->damage_ = proto->damage;
    spell->radius_ = proto->area;
    spell->duration_ = proto->duration;
    spell->skillNeeded_ = static_cast<std::uint32_t>(proto->skill);
    spell->prototypeId_ = static_cast<std::uint32_t>(index & 0xFF) << 16;

    if (!codes_.empty()) codes_.clear();

    for (auto& node : modifiers_) {
        const itemdb::SpellModifierRecord* modifier = node;
        float strength = modifier->strength;
        switch (Letter(modifier, 0)) {
        case 'a':   // area
            AddScaledPositive(spell->radius_, strength, prototype_->area);
            break;
        case 'd': { // duration
            int sum = static_cast<int>(spell->duration_) + ScaleTruncate(prototype_->duration, strength);
            spell->duration_ = static_cast<std::uint32_t>(sum < 0 ? 0 : sum);
            break;
        }
        case 'e':   // effect
            AddScaledPositive(spell->damage_, strength, prototype_->damage);
            break;
        case 'f':   // target restrictions
            switch (Letter(modifier, 1)) {
            case 'e': spell->targetFlags_ |= 2; break;
            case 'f': spell->targetFlags_ |= 1; break;
            case 'g': spell->targetFlags_ |= 8; break;
            case 'h': spell->targetFlags_ |= 4; break;
            case 'l': spell->targetFlags_ |= 0x20; break;
            case 'o': spell->targetFlags_ |= 0x10; break;
            default: break;
            }
            break;
        case 'i':   // cast flags
            if (Letter(modifier, 1) == 'c') spell->targetFlags_ |= 0x20000;
            else if (Letter(modifier, 1) == 't') spell->targetFlags_ |= 0x10000;
            break;
        case 'm':   // mana
            AddScaledPositive(spell->manaCost_, strength, prototype_->mana);
            break;
        case 'r':   // range
            AddScaledPositive(spell->range_, strength, prototype_->range);
            break;
        case 't':
            spell->targetCount_ = static_cast<std::uint32_t>(static_cast<int>(spell->targetCount_) +
                                                           ScaleTruncate(prototype_->targetCount, strength));
            break;
        default:
            break;
        }

        spell->complexity_ += 1;
        spell->skillNeeded_ += static_cast<std::uint32_t>(modifier->skill);
        spell->manaCost_ = modifier->mana + spell->manaCost_;
        spell->price_ = modifier->price + spell->price_;

        itemdb::SpellModifierTable& modifiers = g_spellDatabase->modifiers;
        int modifierIndex = modifiers.IndexOf(modifiers.FindByCode(modifier->code));
        char code = static_cast<char>(modifierIndex + 1);
        codes_.push_back(code);
    }

    float area = spell->radius_;
    if (area > 0.0f) spell->radius_ = static_cast<float>(AreaToRadius(area));

    // mana = max(prototype's mana, mana): the select keeps the computed value when the prototype's is
    // smaller or either is NaN.
    float base = prototype_->mana;
    if (base >= spell->manaCost_) spell->manaCost_ = base;

    // std::sort of the codes (signed chars). Any correct sort gives the same bytes.
    if (!codes_.empty()) {
        char* const first = codes_.data();
        char* const last = first + codes_.size();
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

    std::memset(spell->modifiers_, 0, sizeof(spell->modifiers_));
    for (int i = 0; i < 8; ++i) {
        char c = codes_[i];
        if (c == 0) break;
        spell->modifiers_[i] = c;
    }
}

CSpellContainer* CSpellManager::NewContainer() {
    return CreateObjectAs<CSpellContainer>(SpellObjectType::Container, -1);
}

CSpell* CSpellManager::CreateSpell(const itemdb::SpellRecord* prototype, const std::list<itemdb::SpellModifierRecord*>* modifiers) {
    CSpellConstructor* constructor = context_->SpellConstructor();
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
    CSpell* spell = constructor->container_->spell_.Clone(this);
    constructor->SetManager(nullptr);
    return spell;
}

CSpellConstructor::~CSpellConstructor() {
    CSpellConstructor::Destroy();
    if (container_) container_->Release();   // the container member's destructor
}

const char* CSpellConstructor::ClassName() { return "CSpellConstructor"; }
ClassId CSpellConstructor::GetClassId() { return ClassId::SpellConstructor; }

int CSpellConstructor::Create() {
    container_.Reset();
    manager_ = nullptr;
    return 1;
}

void CSpellConstructor::Destroy() {
    container_.Reset();
    if (manager_) --manager_->refCount_;
    manager_ = nullptr;
}

void CSpellConstructor::SetManager(CSpellManager* manager) {
    container_.Reset();
    if (manager_) --manager_->refCount_;
    manager_ = manager;
    if (manager) manager->AddRef();
}

bool CSpellConstructor::BeginConstruct(const itemdb::SpellRecord* prototype) {
    if (!manager_) return game::FatalError(kNoSpellManager);
    if (!prototype) return game::FatalError(kNoSpellPrototype);
    CSpellContainer* container = manager_->NewContainer();
    container_.Assign(container);
    container_->prototype_ = prototype;   // no null check
    return true;
}

bool CSpellConstructor::EndConstruct() {
    if (manager_ && container_) {
        container_->Build();
        return true;
    }
    return game::FatalError(kNoManagerOrContainer);
}

bool CSpellConstructor::AddModifier(itemdb::SpellModifierRecord* modifier) {
    if (!container_) return false;
    if (!container_->AllowsModifier(modifier)) return false;
    container_->AddModifier(modifier);
    container_->Build();
    return true;
}
