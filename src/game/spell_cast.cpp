// Spell construction: what a cast with modifiers runs before the spell exists. The script's
// CastSpellUnit / CastSpellPoint ("healing{e2;e2}") and the spell constructor screen parse the text
// (00689030, not rewritten) and then drive CSpellConstructor: SetManager, BeginConstruct (a new
// CSpellContainer for the prototype), AddModifier per modifier (each rebuilds the container's
// spell), EndConstruct, CSpell::Clone into a new spell object, SetManager(null).
#include "item_db.h"
#include "spell.h"

#include "item.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "ei/x87.h"
#include "lib/stlp_list.h"
#include "lib/stlp_string.h"
#include "game_data.h"

#include <cstring>

namespace {
// The game's own messages (passed by address, as the original does).
const char* const kInvalidRune = "Invalid rune";
const char* const kNoSpellManager = "CSpellConstructor::BeginConstructSpell.\n There is no valid spell manager.\n";
const char* const kNoSpellPrototype = "No valid spell prototype\n";
const char* const kNoManagerOrContainer = "No valid spell manager or spell container.\n";
const char* const kNoSpellConstructor = "No valid spell constructor\n";
const char* const kCantBegin = "Can't begin spell construction with prototype \"%s\"\n";
const char* const kCantEnd = "Can't end spell construction\n";

constexpr int kClassSpellContainer = 0x3003;



// 0068C680: STLport string reserve(n) on the container's code string (stlp::StringAbi's).
void CodesReserve(char** codes, unsigned n) { static_cast<stlp::StringAbi*>(reinterpret_cast<stlp::string*>(codes))->Reserve(n); }

// _ftol (006E853C) of value * factor, the value loaded as an unsigned 64-bit integer (FILD qword,
// FMUL dword, CALL _ftol).
int ScaleTruncate(std::uint32_t value, float factor) { return static_cast<int>(gamecrt::Ftol(x87::FromUnsigned(value) * factor)); }

// sqrt(area / pi) on the FPU stack, as FLD, FDIV [0x00743A7C], FSQRT (returned unrounded in ST0).
double AreaToRadius(float area) { return std::sqrt(area / 3.1415927f /*00743A7C*/); }

// field += strength * base, stored as a float, then kept only when above zero (the original's
// branch-free select: 0 < value or NaN keeps it, anything else gives +0).
void AddScaledPositive(float& field, float strength, float base) {
    field = strength * base + field;
    if (field <= 0.0f) field = 0.0f;
}

// Returns every node of the list to the node allocator's free list and leaves it empty. The
// sentinel is re-read on every step, as the original does.
void FreeModifierNodes(SpellModifierList& list) {
    SpellModifierNode* node = list.head->next;
    if (node != list.head) {
        do {
            SpellModifierNode* current = node;
            node = node->next;
            if (current) libcrt::NodeDeallocate(current, sizeof(SpellModifierNode));
        } while (node != list.head);
    }
    list.head->next = list.head;
    list.head->prev = list.head;
}

// The code letters of a modifier record.
char Letter(const itemdb::SpellModifierRecord* modifier, int i) { return modifier->code[i]; }
} // namespace

// --- CSpell ------------------------------------------------------------------------------------

void CSpell::Destroy() {
    ClearState();
    std::memset(modifiers_, 0, sizeof(modifiers_));
}
EI_REWRITTEN(0x0068B350, CSpell::Destroy);

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
EI_REWRITTEN(0x0068B370, CSpell::CopyTo);


CSpell* CSpell::Clone(CSpellManager* manager) {
    CSpell* spell = manager->CreateSpellObject(-1);
    CopyTo(spell);   // 0068B370
    return spell;
}
EI_REWRITTEN(0x0068B410, CSpell::Clone);

// --- CSpellContainer ---------------------------------------------------------------------------

int CSpellContainer::Create() {
    prototype_ = nullptr;
    FreeModifierNodes(modifiers_);
    Spell()->Create();
    return 1;
}
EI_REWRITTEN(0x0068BD80, CSpellContainer::Create);

void CSpellContainer::Destroy() {
    ClearState();
    prototype_ = nullptr;
    FreeModifierNodes(modifiers_);
    Spell()->Destroy();
}
EI_REWRITTEN(0x0068BDD0, CSpellContainer::Destroy);

const char* CSpellContainer::GetName() { return name_; }
EI_REWRITTEN(0x0068BC00, CSpellContainer::GetName);

BOOL CSpellContainer::AddModifier(itemdb::SpellModifierRecord* modifier) {
    if (!modifier) return game::FatalError(kInvalidRune);
    SpellModifierList::Insert(modifiers_.End(), modifier);   // 0068C560
    return 1;
}
EI_REWRITTEN(0x0068BE30, CSpellContainer::AddModifier);

int CSpellContainer::AllowsModifier(const itemdb::SpellModifierRecord* modifier) {
    if (!prototype_ || !modifier) return 0;
    switch (Letter(modifier, 0)) {
    case 'a': return prototype_->bytes78[2];
    case 'd': return prototype_->bytes78[4];
    case 'e': return prototype_->bytes78[3];
    case 'r': return prototype_->bytes78[0];
    case 't': return prototype_->bytes78[1];
    default: return 1;
    }
}
EI_REWRITTEN(0x0068BEC0, CSpellContainer::AllowsModifier);

BOOL CSpellContainer::Build() {
    CSpell* spell = Spell();
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
    spell->prototypeField14_ = proto->value3C;
    spell->effectPower_ = 0x10;

    itemdb::SpellTable& spells = itemdb::TheSpellDatabase().spells;
    int index = spells.IndexOf(spells.FindByCode(proto->code));
    proto = prototype_;
    spell->prototypeIndex_ = static_cast<std::uint8_t>(index);
    spell->damage_ = proto->damage;
    spell->radius_ = proto->area;
    spell->duration_ = proto->duration;
    spell->skillNeeded_ = static_cast<std::uint32_t>(proto->skill);
    spell->prototypeId_ = static_cast<std::uint32_t>(index & 0xFF) << 16;

    if (codes_ != codesEnd_) {
        *codes_ = 0;
        codesEnd_ = codes_;
    }

    for (SpellModifierNode* node = modifiers_.First(); node != modifiers_.End(); node = node->next) {
        const itemdb::SpellModifierRecord* modifier = node->data;
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

        itemdb::SpellModifierTable& modifiers = itemdb::TheSpellDatabase().modifiers;
        int modifierIndex = modifiers.IndexOf(modifiers.FindByCode(modifier->code));
        char code = static_cast<char>(modifierIndex + 1);
        // push_back on the STLport string: grow to size + max(size, 1) when full.
        if (codesEnd_ + 1 == codesCapacity_) {
            unsigned size = static_cast<unsigned>(codesEnd_ - codes_);
            CodesReserve(&codes_, size + (size < 1 ? 1u : size));
        }
        codesEnd_[1] = 0;
        *codesEnd_ = code;
        ++codesEnd_;
    }

    float area = spell->radius_;
    if (area > 0.0f) spell->radius_ = static_cast<float>(AreaToRadius(area));

    // mana = max(prototype's mana, mana): the original's select keeps the computed value when the
    // prototype's is smaller or either is NaN.
    float base = prototype_->mana;
    if (base >= spell->manaCost_) spell->manaCost_ = base;

    // std::sort of the codes (signed chars). Any correct sort gives the same bytes.
    if (codes_ != codesEnd_) {
        for (char* i = codes_ + 1; i != codesEnd_; ++i) {
            char value = *i;
            char* j = i;
            while (j != codes_ && value < j[-1]) {
                *j = j[-1];
                --j;
            }
            *j = value;
        }
    }

    std::memset(spell->modifiers_, 0, sizeof(spell->modifiers_));
    if (codes_) {
        for (int i = 0; i < 8; ++i) {
            char c = codes_[i];
            if (c == 0) break;
            spell->modifiers_[i] = c;
        }
    }
    return 1;
}
EI_REWRITTEN(0x0068BF60, CSpellContainer::Build);

// --- CSpellManager -----------------------------------------------------------------------------

CSpellContainer* CSpellManager::NewContainer() {
    return CreateObjectAs<CSpellContainer>(kClassSpellContainer, -1);
}
EI_REWRITTEN(0x00688EB0, CSpellManager::NewContainer);

CSpell* CSpellManager::CreateSpell(const itemdb::SpellRecord* prototype, const SpellModifierList* modifiers) {
    CSpellConstructor* constructor = context_->SpellConstructor();
    if (!constructor) return game::FatalErrorAs<CSpell>(kNoSpellConstructor);
    constructor->SetManager(this);
    if (!constructor->BeginConstruct(prototype))
        return game::FatalErrorAs<CSpell>(kCantBegin, prototype->code.GetString());
    int added = 0;
    for (SpellModifierNode* node = modifiers->First(); node != modifiers->End(); node = node->next) {
        if (added >= 8) break;
        constructor->AddModifier(node->data);
        ++added;
    }
    if (!constructor->EndConstruct()) return game::FatalErrorAs<CSpell>(kCantEnd);
    CSpell* spell = constructor->container_->Spell()->Clone(this);
    constructor->SetManager(nullptr);
    return spell;
}
EI_REWRITTEN(0x00689310, CSpellManager::CreateSpell);

// --- CSpellConstructor -------------------------------------------------------------------------

CSpellConstructor::~CSpellConstructor() {
    CSpellConstructor::Destroy();
    if (container_) container_->Release();   // the container member's destructor
}
EI_REWRITTEN_SPECIAL(0x006840A0, CSpellConstructor::~CSpellConstructor);
EI_REWRITTEN_SPECIAL(0x00684080, CSpellConstructor::~CSpellConstructor);

const char* CSpellConstructor::GetClassName() { return "CSpellConstructor"; }
int CSpellConstructor::GetClassId() { return 0x1B; }
EI_REWRITTEN(0x00684060, CSpellConstructor::GetClassName);
EI_REWRITTEN(0x00684070, CSpellConstructor::GetClassId);

int CSpellConstructor::Create() {
    CSpellContainer* container = container_;
    container_ = nullptr;
    if (container) container->Release();
    manager_ = nullptr;
    return 1;
}
EI_REWRITTEN(0x00684120, CSpellConstructor::Create);

void CSpellConstructor::Destroy() {
    CSpellContainer* container = container_;
    container_ = nullptr;
    if (container) container->Release();
    if (manager_) --manager_->refCount_;
    manager_ = nullptr;
}
EI_REWRITTEN(0x00684150, CSpellConstructor::Destroy);

void CSpellConstructor::SetManager(CSpellManager* manager) {
    CSpellContainer* container = container_;
    container_ = nullptr;
    if (container) container->Release();
    if (manager_) --manager_->refCount_;
    manager_ = manager;
    if (manager) ++manager->refCount_;
}
EI_REWRITTEN(0x006841A0, CSpellConstructor::SetManager);

BOOL CSpellConstructor::BeginConstruct(const itemdb::SpellRecord* prototype) {
    if (!manager_) return game::FatalError(kNoSpellManager);
    if (!prototype) return game::FatalError(kNoSpellPrototype);
    CSpellContainer* container = manager_->NewContainer();
    CSpellContainer* previous = container_;
    container_ = container;
    if (container) ++container->refCount_;
    if (previous) previous->Release();
    container_->prototype_ = prototype;   // no null check, as in the original
    return 1;
}
EI_REWRITTEN(0x006841E0, CSpellConstructor::BeginConstruct);

BOOL CSpellConstructor::EndConstruct() {
    if (manager_ && container_) {
        container_->Build();
        return 1;
    }
    return game::FatalError(kNoManagerOrContainer);
}
EI_REWRITTEN(0x00684250, CSpellConstructor::EndConstruct);

BOOL CSpellConstructor::AddModifier(itemdb::SpellModifierRecord* modifier) {
    if (!container_) return 0;
    if (!container_->AllowsModifier(modifier)) return 0;
    container_->AddModifier(modifier);
    container_->Build();
    return 1;
}
EI_REWRITTEN(0x006842A0, CSpellConstructor::AddModifier);

