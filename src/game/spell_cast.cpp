// Spell construction: what a cast with modifiers runs before the spell exists. The script's
// CastSpellUnit / CastSpellPoint ("healing{e2;e2}") and the spell constructor screen parse the text
// (00689030, not rewritten) and then drive CSpellConstructor: SetManager, BeginConstruct (a new
// CSpellContainer for the prototype), AddModifier per modifier (each rebuilds the container's
// spell), EndConstruct, CSpell::Clone into a new spell object, SetManager(null).
#include "spell.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "originals_effects.h"
#include "originals_gameplay.h"
#include "originals_library.h"

#include <cstring>

#pragma float_control(precise, on)

namespace {
// The game's own messages (passed by address, as the original does).
const char* const kInvalidRune = reinterpret_cast<const char*>(0x007959F0);          // "Invalid rune"
const char* const kNoSpellManager = reinterpret_cast<const char*>(0x00795858);
const char* const kNoSpellPrototype = reinterpret_cast<const char*>(0x0079583C);
const char* const kNoManagerOrContainer = reinterpret_cast<const char*>(0x007958A4);
const char* const kNoSpellConstructor = reinterpret_cast<const char*>(0x00795990);
const char* const kCantBegin = reinterpret_cast<const char*>(0x0079595C);           // ... "%s"
const char* const kCantEnd = reinterpret_cast<const char*>(0x0079593C);

constexpr int kSlotCreate = 2;
constexpr int kSlotDestroy = 3;
constexpr int kSlotRelease = 4;
constexpr int kSlotSlot5 = 5;
constexpr int kManagerSlotCreateObject = 23;    // CBaseManager slot 23 (+0x5C): (class, key)
constexpr int kContextSlotSpellConstructor = 17; // +0x44
constexpr int kClassSpellContainer = 0x3003;

// Free list of the STLport node allocator for 16-byte blocks (the 12-byte list nodes).
SpellModifierNode*& NodeFreeList16() { return *reinterpret_cast<SpellModifierNode**>(0x0079B228); }

// The spell and modifier tables: a holder object whose +0x0C is a name hash_map; the lookups
// below are thiscall on it.
void* const kSpellTableHolder = reinterpret_cast<void*>(0x007C8510);
void* const kModifierTableHolder = reinterpret_cast<void*>(0x007C8530);

// 0068C680 (not rewritten). STLport string reserve(n) on the container's code string.
__declspec(naked) void __fastcall CodesReserve(char**, void*, unsigned) EI_ORIGINAL(0x0068C680)
// 0068C560 (not rewritten). list<char*>::insert(pos, *value), the iterator stored in *out.
// Arguments: list, unused EDX, out, pos, value.
void __fastcall ModifierListInsert(SpellModifierNode** a0, void*, SpellModifierNode** a2, SpellModifierNode* a3, char** a4) { ListInsert68C560Original(a0, nullptr, (void**)a2, a3, a4); }

// The rewritten functions are called through their game addresses, so the verify wrappers see
// every call (most have no other caller).
__declspec(naked) BOOL __fastcall ContainerBuild(CSpellContainer*) EI_ORIGINAL(0x0068BF60)
__declspec(naked) int __fastcall ContainerAllows(CSpellContainer*, void*, const char*) EI_ORIGINAL(0x0068BEC0)
__declspec(naked) BOOL __fastcall ContainerAddModifier(CSpellContainer*, void*, char*) EI_ORIGINAL(0x0068BE30)
__declspec(naked) CSpellContainer* __fastcall ManagerNewContainer(CSpellManager*) EI_ORIGINAL(0x00688EB0)

// _ftol (006E853C) of (unsigned 64-bit value) * factor, as FILD qword; FMUL dword; CALL _ftol.
__declspec(naked) int __cdecl ScaleTruncate(std::uint64_t, float) {
    __asm {
        fild qword ptr [esp + 4]
        fmul dword ptr [esp + 0xC]
        mov eax, 0x006E853C
        call eax
        ret
    }
}

// sqrt(area / pi) on the FPU stack, as FLD; FDIV [0x00743A7C]; FSQRT (returned unrounded in ST0).
__declspec(naked) double __cdecl AreaToRadius(float) {
    __asm {
        fld dword ptr [esp + 4]
        mov eax, 0x00743A7C
        fdiv dword ptr [eax]
        fsqrt
        ret
    }
}

std::uint32_t Bits(float f) {
    std::uint32_t u;
    std::memcpy(&u, &f, sizeof(u));
    return u;
}

float FloatAt(const void* p, int offset) {
    float f;
    std::memcpy(&f, static_cast<const char*>(p) + offset, sizeof(f));
    return f;
}

std::uint32_t DwordAt(const void* p, int offset) {
    std::uint32_t u;
    std::memcpy(&u, static_cast<const char*>(p) + offset, sizeof(u));
    return u;
}

void SetFloat(std::uint32_t& field, float value) { std::memcpy(&field, &value, sizeof(value)); }
float GetFloat(const std::uint32_t& field) {
    float f;
    std::memcpy(&f, &field, sizeof(f));
    return f;
}

// field += strength * base, stored as a float, then kept only when above zero (the original's
// branch-free select: 0 < value or NaN keeps it, anything else gives +0).
void AddScaledPositive(std::uint32_t& field, float strength, float base) {
    SetFloat(field, strength * base + GetFloat(field));
    float stored = GetFloat(field);
    if (!(0.0f < stored) && stored == stored) field = 0;
}

// Returns every node of the list (sentinel `*list`) to the node allocator's free list and leaves
// it empty. The sentinel is re-read on every step, as the original does.
void FreeModifierNodes(SpellModifierNode* const* list) {
    SpellModifierNode* node = (*list)->next;
    if (node != *list) {
        do {
            SpellModifierNode* current = node;
            node = node->next;
            if (current) {
                current->next = NodeFreeList16();
                NodeFreeList16() = current;
            }
        } while (node != *list);
    }
    (*list)->next = *list;
    (*list)->prev = *list;
}

// The code letters of a modifier record (+0x04).
char Letter(const char* modifier, int i) { return modifier[4 + i]; }
} // namespace

// --- CSpell ------------------------------------------------------------------------------------

void CSpell::ClearModifiers() {
    ei::VCall<void>(this, kSlotSlot5);
    std::memset(modifiers_, 0, sizeof(modifiers_));
}
EI_REPLACE_METHOD(0x0068B350, CSpell::ClearModifiers);

void CSpell::CopyTo(CSpell* target) {
    ei::VCall<void>(target, kSlotDestroy);
    std::memcpy(&target->prototypeIndex_, &prototypeIndex_, 0x54 - 0x1C);
    std::memset(target->modifiers_, 0, sizeof(target->modifiers_));
    for (int i = 0; i < 8; ++i) {
        if (modifiers_[i] == 0) break;
        target->modifiers_[i] = modifiers_[i];
    }
    target->unknown_18 = unknown_18;
}
EI_REPLACE_METHOD(0x0068B370, CSpell::CopyTo);

__declspec(naked) void CSpell::CallCopyTo(CSpell*) EI_ORIGINAL(0x0068B370)

CSpell* CSpell::Clone(CSpellManager* manager) {
    CSpell* spell = manager->CallCreateSpellObject(-1);
    CallCopyTo(spell);
    return spell;
}
EI_REPLACE_METHOD(0x0068B410, CSpell::Clone);

// --- CSpellContainer ---------------------------------------------------------------------------

BOOL CSpellContainer::Reset() {
    prototype_ = nullptr;
    FreeModifierNodes(&modifiers_);
    ei::VCall<int>(Spell(), kSlotCreate);
    return 1;
}
EI_REPLACE_METHOD(0x0068BD80, CSpellContainer::Reset);

void CSpellContainer::Shutdown() {
    ei::VCall<void>(this, kSlotSlot5);
    prototype_ = nullptr;
    FreeModifierNodes(&modifiers_);
    ei::VCall<void>(Spell(), kSlotDestroy);
}
EI_REPLACE_METHOD(0x0068BDD0, CSpellContainer::Shutdown);

const char* CSpellContainer::NameField() { return name_; }
EI_REPLACE_METHOD(0x0068BC00, CSpellContainer::NameField);

BOOL CSpellContainer::AddModifier(char* modifier) {
    if (!modifier) return game::FatalError(kInvalidRune);
    SpellModifierNode* inserted;
    ModifierListInsert(&modifiers_, nullptr, &inserted, modifiers_, &modifier);
    return 1;
}
EI_REPLACE_METHOD(0x0068BE30, CSpellContainer::AddModifier);

int CSpellContainer::AllowsModifier(const char* modifier) {
    if (!prototype_ || !modifier) return 0;
    switch (Letter(modifier, 0)) {
    case 'a': return static_cast<std::uint8_t>(prototype_[0x7A]);
    case 'd': return static_cast<std::uint8_t>(prototype_[0x7C]);
    case 'e': return static_cast<std::uint8_t>(prototype_[0x7B]);
    case 'r': return static_cast<std::uint8_t>(prototype_[0x78]);
    case 't': return static_cast<std::uint8_t>(prototype_[0x79]);
    default: return 1;
    }
}
EI_REPLACE_METHOD(0x0068BEC0, CSpellContainer::AllowsModifier);

BOOL CSpellContainer::Build() {
    CSpell* spell = Spell();
    const char* proto = prototype_;
    spell->unknown_30 = DwordAt(proto, 0x20);
    spell->unknown_34 = DwordAt(proto, 0x24);
    std::uint32_t kind = DwordAt(proto, 0x30);
    if (kind == 0x75) {             // 'u'
        spell->targetFlags_ = 0x10000000;
    } else if (kind == 0x74) {      // 't'
        spell->targetFlags_ = 0x20000000;
    }
    spell->unknown_38 = DwordAt(proto, 0x34);
    spell->unknown_40 = DwordAt(proto, 0x18);
    spell->unknown_44 = DwordAt(proto, 0x1C);
    spell->price_ = DwordAt(proto, 0x0C);
    spell->unknown_4C = DwordAt(proto, 0x3C);
    spell->unknown_1D = 0x10;

    char* found = FindSpellByCode(kSpellTableHolder, nullptr, *reinterpret_cast<char* const*>(proto + 4));
    int index = found ? static_cast<int>(found - itemdb::Spells().begin) / itemdb::kSpellSize : -1;
    proto = prototype_;
    spell->prototypeIndex_ = static_cast<std::uint8_t>(index);
    spell->unknown_24 = DwordAt(proto, 0x2C);
    spell->unknown_28 = DwordAt(proto, 0x28);
    spell->unknown_20 = DwordAt(proto, 0x38);
    spell->unknown_50 = DwordAt(proto, 0x44);
    spell->unknown_18 = static_cast<std::uint32_t>(index & 0xFF) << 16;

    if (codes_ != codesEnd_) {
        *codes_ = 0;
        codesEnd_ = codes_;
    }

    for (SpellModifierNode* node = modifiers_->next; node != modifiers_; node = node->next) {
        const char* modifier = node->modifier;
        float strength = FloatAt(modifier, 0x14);
        switch (Letter(modifier, 0)) {
        case 'a':   // area
            AddScaledPositive(spell->unknown_28, strength, FloatAt(prototype_, 0x28));
            break;
        case 'd': { // duration
            int sum = static_cast<int>(spell->unknown_20) + ScaleTruncate(DwordAt(prototype_, 0x38), strength);
            spell->unknown_20 = static_cast<std::uint32_t>(sum < 0 ? 0 : sum);
            break;
        }
        case 'e':   // effect
            AddScaledPositive(spell->unknown_24, strength, FloatAt(prototype_, 0x2C));
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
            AddScaledPositive(spell->unknown_40, strength, FloatAt(prototype_, 0x18));
            break;
        case 'r':   // range
            AddScaledPositive(spell->unknown_34, strength, FloatAt(prototype_, 0x24));
            break;
        case 't':
            spell->unknown_38 = static_cast<std::uint32_t>(static_cast<int>(spell->unknown_38) +
                                                           ScaleTruncate(DwordAt(prototype_, 0x34), strength));
            break;
        default:
            break;
        }

        spell->unknown_44 += 1;
        spell->unknown_50 += DwordAt(modifier, 0x18);
        SetFloat(spell->unknown_40, FloatAt(modifier, 0x10) + GetFloat(spell->unknown_40));
        SetFloat(spell->price_, FloatAt(modifier, 0x08) + GetFloat(spell->price_));

        char* record = FindModifierByCode(kModifierTableHolder, nullptr, modifier + 4);
        int modifierIndex =
            record ? static_cast<int>(record - itemdb::SpellModifiers().begin) / itemdb::kSpellModifierSize : -1;
        char code = static_cast<char>(modifierIndex + 1);
        // push_back on the STLport string: grow to size + max(size, 1) when full.
        if (codesEnd_ + 1 == codesCapacity_) {
            unsigned size = static_cast<unsigned>(codesEnd_ - codes_);
            CodesReserve(&codes_, nullptr, size + (size < 1 ? 1u : size));
        }
        codesEnd_[1] = 0;
        *codesEnd_ = code;
        ++codesEnd_;
    }

    float area = GetFloat(spell->unknown_28);
    if (area > 0.0f) {
        float radius = static_cast<float>(AreaToRadius(area));
        SetFloat(spell->unknown_28, radius);
    }

    // mana = max(prototype's mana, mana): the original's select keeps the computed value when the
    // prototype's is smaller or either is NaN.
    float base = FloatAt(prototype_, 0x18);
    float mana = GetFloat(spell->unknown_40);
    if (base >= mana) SetFloat(spell->unknown_40, base);

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
EI_REPLACE_METHOD(0x0068BF60, CSpellContainer::Build);

// --- CSpellManager -----------------------------------------------------------------------------

CSpellContainer* CSpellManager::NewContainer() {
    return ei::VCall<CSpellContainer*>(this, kManagerSlotCreateObject, kClassSpellContainer, -1);
}
EI_REPLACE_METHOD(0x00688EB0, CSpellManager::NewContainer);

__declspec(naked) CSpell* CSpellManager::CallCreateSpellObject(int) EI_ORIGINAL(0x00688EC0)

CSpell* CSpellManager::CreateSpell(char* prototype, SpellModifierNode** modifiers) {
    auto* constructor = ei::VCall<CSpellConstructor*>(context_, kContextSlotSpellConstructor);
    if (!constructor) return reinterpret_cast<CSpell*>(game::FatalError(kNoSpellConstructor));
    constructor->CallSetManager(this);
    if (!constructor->CallBeginConstruct(prototype))
        return reinterpret_cast<CSpell*>(game::FatalError(kCantBegin, *reinterpret_cast<char**>(prototype + 4)));
    int added = 0;
    for (SpellModifierNode* node = (*modifiers)->next; node != *modifiers; node = node->next) {
        if (added >= 8) break;
        constructor->CallAddModifier(node->modifier);
        ++added;
    }
    if (!constructor->CallEndConstruct()) return reinterpret_cast<CSpell*>(game::FatalError(kCantEnd));
    CSpell* spell = SpellCloneOriginal(constructor->container_->Spell(), nullptr, this);
    constructor->CallSetManager(nullptr);
    return spell;
}
EI_REPLACE_METHOD(0x00689310, CSpellManager::CreateSpell);

// --- CSpellConstructor -------------------------------------------------------------------------

void CSpellConstructor::DestructBody() {
    ei::SetVtable(this, kVtableAddress);
    CSpellContainer* container = container_;
    container_ = nullptr;
    if (container) container->Release();
    if (manager_) --manager_->refCount_;
    manager_ = nullptr;
    if (container_) container_->Release();   // the container member's destructor
    DestructBase();
}
EI_REPLACE_METHOD(0x006840A0, CSpellConstructor::DestructBody);

CSpellConstructor* CSpellConstructor::DeletingDestruct(unsigned flags) {
    DestructBody();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x00684080, CSpellConstructor::DeletingDestruct);

constexpr std::uintptr_t kSpellConstructorClassName = std::uintptr_t(0x00795828);   // "CSpellConstructor"
const char* __fastcall SpellConstructorClassName(CSpellConstructor*) { return reinterpret_cast<const char*>(kSpellConstructorClassName); }
int __fastcall SpellConstructorClassId(CSpellConstructor*) { return 0x1B; }
EI_REPLACE(0x00684060, SpellConstructorClassName);
EI_REPLACE(0x00684070, SpellConstructorClassId);

BOOL CSpellConstructor::Reset() {
    CSpellContainer* container = container_;
    container_ = nullptr;
    if (container) ei::VCall<void>(container, kSlotRelease);
    manager_ = nullptr;
    return 1;
}
EI_REPLACE_METHOD(0x00684120, CSpellConstructor::Reset);

void CSpellConstructor::Detach() {
    CSpellContainer* container = container_;
    container_ = nullptr;
    if (container) ei::VCall<void>(container, kSlotRelease);
    if (manager_) --manager_->refCount_;
    manager_ = nullptr;
}
EI_REPLACE_METHOD(0x00684150, CSpellConstructor::Detach);

void CSpellConstructor::SetManager(CSpellManager* manager) {
    CSpellContainer* container = container_;
    container_ = nullptr;
    if (container) ei::VCall<void>(container, kSlotRelease);
    if (manager_) --manager_->refCount_;
    manager_ = manager;
    if (manager) ++manager->refCount_;
}
EI_REPLACE_METHOD(0x006841A0, CSpellConstructor::SetManager);

BOOL CSpellConstructor::BeginConstruct(char* prototype) {
    if (!manager_) return game::FatalError(kNoSpellManager);
    if (!prototype) return game::FatalError(kNoSpellPrototype);
    CSpellContainer* container = ManagerNewContainer(manager_);
    CSpellContainer* previous = container_;
    container_ = container;
    if (container) ++container->refCount_;
    if (previous) ei::VCall<void>(previous, kSlotRelease);
    container_->prototype_ = prototype;   // no null check, as in the original
    return 1;
}
EI_REPLACE_METHOD(0x006841E0, CSpellConstructor::BeginConstruct);

BOOL CSpellConstructor::EndConstruct() {
    if (manager_ && container_) {
        ContainerBuild(container_);
        return 1;
    }
    return game::FatalError(kNoManagerOrContainer);
}
EI_REPLACE_METHOD(0x00684250, CSpellConstructor::EndConstruct);

BOOL CSpellConstructor::AddModifier(char* modifier) {
    if (!container_) return 0;
    if (!ContainerAllows(container_, nullptr, modifier)) return 0;
    ContainerAddModifier(container_, nullptr, modifier);
    ContainerBuild(container_);
    return 1;
}
EI_REPLACE_METHOD(0x006842A0, CSpellConstructor::AddModifier);

void CSpellConstructor::CallSetManager(CSpellManager* manager) { SpellConstructorSetManagerOriginal(this, nullptr, manager); }
BOOL CSpellConstructor::CallBeginConstruct(char* prototype) { return SpellConstructorBeginOriginal(this, nullptr, prototype); }
__declspec(naked) BOOL CSpellConstructor::CallEndConstruct() EI_ORIGINAL(0x00684250)
BOOL CSpellConstructor::CallAddModifier(char* modifier) { return SpellConstructorAddModifierOriginal(this, nullptr, modifier); }
