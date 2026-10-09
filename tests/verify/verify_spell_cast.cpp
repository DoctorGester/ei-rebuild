// Checks for the spell construction code (spell_cast.cpp).
#include "verify.h"

#include "../../src/game/spell.h"

#include <cstring>

namespace {
constexpr std::uintptr_t kContainerVtable = 0x00743AC8;

SpellModifierNode* LastNode(const CSpellContainer* container) { return container->modifiers_->prev; }

bool ListEmpty(const CSpellContainer* container) {
    return container->modifiers_->next == container->modifiers_ && container->modifiers_->prev == container->modifiers_;
}

// The modifier bytes CopyTo produces from `source`: up to the first zero, zero after.
void ExpectedModifiers(const CSpell* source, std::int8_t out[8]) {
    std::memset(out, 0, 8);
    for (int i = 0; i < 8 && source->modifiers_[i] != 0; ++i) out[i] = source->modifiers_[i];
}
} // namespace

// --- CSpell ------------------------------------------------------------------------------------

// 0068B350 calls slot 5 (empty for CSpell, 00402CC0) and clears the modifiers: ours, modifier
// bytes saved and restored, original; compared. PUSH ESI; MOV ESI,ECX; MOV EAX,[ESI] (5 bytes).
static void(__fastcall* s_clearModifiers)(CSpell*);
static void __fastcall ClearModifiersVerify(CSpell* self) {
    std::int8_t before[8], ours[8];
    std::memcpy(before, self->modifiers_, 8);
    self->ClearModifiers();
    std::memcpy(ours, self->modifiers_, 8);
    std::memcpy(self->modifiers_, before, 8);
    s_clearModifiers(self);
    ei::verify::Check("CSpell::ClearModifiers", true, std::memcmp(ours, self->modifiers_, 8) == 0, "spell %08X",
                      self->unknown_18);
}
EI_VERIFY(0x0068B350, ClearModifiersVerify, 5, s_clearModifiers);

// 0068B370 writes the target's +0x18..+0x5C: ours, target bytes saved and restored, original;
// compared. PUSH EBX; MOV EBX,[ESP+8] (5 bytes).
static void(__fastcall* s_copyTo)(CSpell*, void*, CSpell*);
static void __fastcall CopyToVerify(CSpell* self, void*, CSpell* target) {
    char* bytes = reinterpret_cast<char*>(target) + 0x18;
    char before[0x44], ours[0x44];
    std::memcpy(before, bytes, sizeof(before));
    self->CopyTo(target);
    std::memcpy(ours, bytes, sizeof(ours));
    std::memcpy(bytes, before, sizeof(before));
    s_copyTo(self, nullptr, target);
    ei::verify::Check("CSpell::CopyTo", true, std::memcmp(ours, bytes, sizeof(ours)) == 0, "spell %08X",
                      self->unknown_18);
}
EI_VERIFY(0x0068B370, CopyToVerify, 5, s_copyTo);

// 0068B410 creates a spell object: ours only. The new spell carries this one's id, parameters
// and modifiers.
static void* s_clone;
static CSpell* __fastcall CloneVerify(CSpell* self, void*, CSpellManager* manager) {
    CSpell* spell = self->Clone(manager);
    bool same = spell != nullptr;
    if (same) {
        std::int8_t modifiers[8];
        ExpectedModifiers(self, modifiers);
        same = spell->unknown_18 == self->unknown_18 &&
               std::memcmp(&spell->prototypeIndex_, &self->prototypeIndex_, 0x54 - 0x1C) == 0 &&
               std::memcmp(spell->modifiers_, modifiers, 8) == 0;
    }
    ei::verify::Check("CSpell::Clone", true, same, "spell %08X -> %p", self->unknown_18, spell);
    return spell;
}
EI_VERIFY(0x0068B410, CloneVerify, 0, s_clone);

// --- CSpellContainer ---------------------------------------------------------------------------

// 0068BD80 / 0068BDD0 free the modifier list: ours only; the list ends empty, no prototype.
static void* s_containerReset;
static BOOL __fastcall ContainerResetVerify(CSpellContainer* self) {
    BOOL result = self->Reset();
    ei::verify::Check("CSpellContainer::Reset", true, result == 1 && ListEmpty(self) && !self->prototype_, "%p", self);
    return result;
}
EI_VERIFY(0x0068BD80, ContainerResetVerify, 0, s_containerReset);

static void* s_containerShutdown;
static void __fastcall ContainerShutdownVerify(CSpellContainer* self) {
    self->Shutdown();
    ei::verify::Check("CSpellContainer::Shutdown", true, ListEmpty(self) && !self->prototype_, "%p", self);
}
EI_VERIFY(0x0068BDD0, ContainerShutdownVerify, 0, s_containerShutdown);

// 0068BC00: MOV EAX,[ECX+0x8C] (6 bytes).
static const char*(__fastcall* s_nameField)(CSpellContainer*);
static const char* __fastcall NameFieldVerify(CSpellContainer* self) {
    const char* expected = s_nameField(self);
    ei::verify::Check("CSpellContainer::NameField", expected, self->NameField(), "%p", self);
    return expected;
}
EI_VERIFY(0x0068BC00, NameFieldVerify, 6, s_nameField);

// 0068BE30 allocates a list node: ours only; a non-null modifier ends up last in the list.
static void* s_containerAdd;
static BOOL __fastcall ContainerAddVerify(CSpellContainer* self, void*, char* modifier) {
    BOOL result = self->AddModifier(modifier);
    bool ok = !modifier || (result == 1 && LastNode(self)->modifier == modifier);
    ei::verify::Check("CSpellContainer::AddModifier", true, ok, "%p modifier %p", self, modifier);
    return result;
}
EI_VERIFY(0x0068BE30, ContainerAddVerify, 0, s_containerAdd);

// 0068BEC0: MOV EAX,[ECX+0x1C]; TEST EAX,EAX (5 bytes). Pure.
static int(__fastcall* s_allows)(CSpellContainer*, void*, const char*);
static int __fastcall AllowsVerify(CSpellContainer* self, void*, const char* modifier) {
    int expected = s_allows(self, nullptr, modifier);
    ei::verify::Check("CSpellContainer::AllowsModifier", expected, self->AllowsModifier(modifier), "modifier %p",
                      modifier);
    return expected;
}
EI_VERIFY(0x0068BEC0, AllowsVerify, 5, s_allows);

// 0068BF60 writes the container's spell (+0x48..+0x8C) and its code string: ours, the spell
// bytes saved and restored, original; spell bytes and codes compared. The original empties the
// code string before refilling it, so the second run starts from the same contents (with the
// capacity ours may have grown). PUSH EBP; MOV EBP,ESP; SUB ESP,0x40 (6 bytes).
static BOOL(__fastcall* s_build)(CSpellContainer*);
static BOOL __fastcall BuildVerify(CSpellContainer* self) {
    constexpr int kFrom = 0x48, kTo = 0x8C, kCodes = 64;
    char* bytes = reinterpret_cast<char*>(self) + kFrom;
    char before[kTo - kFrom], ours[kTo - kFrom], oursCodes[kCodes];
    std::memcpy(before, bytes, sizeof(before));

    BOOL actual = self->Build();
    std::memcpy(ours, bytes, sizeof(ours));
    int oursCount = static_cast<int>(self->codesEnd_ - self->codes_);
    std::memcpy(oursCodes, self->codes_, oursCount < kCodes ? oursCount : kCodes);
    std::memcpy(bytes, before, sizeof(before));

    BOOL expected = s_build(self);
    int count = static_cast<int>(self->codesEnd_ - self->codes_);
    bool same = expected == actual && std::memcmp(ours, bytes, sizeof(ours)) == 0 && count == oursCount &&
                std::memcmp(oursCodes, self->codes_, count < kCodes ? count : kCodes) == 0;
    const CSpell* spell = self->Spell();
    ei::verify::Check("CSpellContainer::Build", true, same, "spell %08X modifiers %d (ours %d)", spell->unknown_18,
                      count, oursCount);
    return expected;
}
EI_VERIFY(0x0068BF60, BuildVerify, 6, s_build);

// --- CSpellManager -----------------------------------------------------------------------------

// 00688EB0 creates an object: ours only; a container (or null).
static void* s_newContainer;
static CSpellContainer* __fastcall NewContainerVerify(CSpellManager* self) {
    CSpellContainer* container = self->NewContainer();
    bool ok = !container || *reinterpret_cast<std::uintptr_t*>(container) == kContainerVtable;
    ei::verify::Check("CSpellManager::NewContainer", true, ok, "%p", container);
    return container;
}
EI_VERIFY(0x00688EB0, NewContainerVerify, 0, s_newContainer);

// 00689310 builds a spell: ours only; the new spell's packed id names `prototype`.
static void* s_createSpell;
static CSpell* __fastcall CreateSpellVerify(CSpellManager* self, void*, char* prototype, SpellModifierNode** modifiers) {
    CSpell* spell = self->CreateSpell(prototype, modifiers);
    const RecordTable& spells = itemdb::Spells();
    std::uint32_t index = static_cast<std::uint32_t>(static_cast<int>(prototype - spells.begin) / itemdb::kSpellSize);
    bool ok = spell && (spell->unknown_18 >> 16) == (index & 0xFF);
    ei::verify::Check("CSpellManager::CreateSpell", true, ok, "prototype %p spell %p", prototype, spell);
    return spell;
}
EI_VERIFY(0x00689310, CreateSpellVerify, 0, s_createSpell);

static void* s_unusedDestruct;

// --- CSpellConstructor -------------------------------------------------------------------------
// All change references or create containers: ours only, the fields checked afterwards.

// 006840A0 / 00684080 (A-H21): the destructors leave CEngineObject's vtable, no manager or
// container. Scenario: spells (each cast's constructor) and every zone change / game end.
static void __fastcall ConstructorDestructVerify(CSpellConstructor* self) {
    self->DestructBody();
    bool ok = *reinterpret_cast<std::uintptr_t*>(self) == CEngineObject::kVtableAddress && !self->manager_ && !self->container_;
    ei::verify::Check("CSpellConstructor::DestructBody", true, ok, "%p", self);
}
EI_VERIFY(0x006840A0, ConstructorDestructVerify, 0, s_unusedDestruct);
static CSpellConstructor* __fastcall ConstructorDeleteVerify(CSpellConstructor* self, void*, unsigned flags) {
    CSpellConstructor* result = self->DeletingDestruct(flags);
    ei::verify::Check("CSpellConstructor::DeletingDestruct", true, result == self, "%p", self);
    return result;
}
EI_VERIFY(0x00684080, ConstructorDeleteVerify, 0, s_unusedDestruct);
// 00684060 / 00684070: MOV EAX,imm32 (5 bytes); against the original.
static const char*(__fastcall* s_constructorName)(CSpellConstructor*);
static const char* __fastcall ConstructorNameVerify(CSpellConstructor* self) {
    const char* expected = s_constructorName(self);
    ei::verify::Check("CSpellConstructor::GetClassName", expected, SpellConstructorClassName(self), "%p", self);
    return expected;
}
EI_VERIFY(0x00684060, ConstructorNameVerify, 5, s_constructorName);
static int(__fastcall* s_constructorId)(CSpellConstructor*);
static int __fastcall ConstructorIdVerify(CSpellConstructor* self) {
    int expected = s_constructorId(self);
    ei::verify::Check("CSpellConstructor::GetClassId", expected, SpellConstructorClassId(self), "%p", self);
    return expected;
}
EI_VERIFY(0x00684070, ConstructorIdVerify, 5, s_constructorId);

static void* s_constructorReset;
static BOOL __fastcall ConstructorResetVerify(CSpellConstructor* self) {
    BOOL result = self->Reset();
    ei::verify::Check("CSpellConstructor::Reset", true, result == 1 && !self->manager_ && !self->container_, "%p",
                      self);
    return result;
}
EI_VERIFY(0x00684120, ConstructorResetVerify, 0, s_constructorReset);

static void* s_constructorDetach;
static void __fastcall ConstructorDetachVerify(CSpellConstructor* self) {
    self->Detach();
    ei::verify::Check("CSpellConstructor::Detach", true, !self->manager_ && !self->container_, "%p", self);
}
EI_VERIFY(0x00684150, ConstructorDetachVerify, 0, s_constructorDetach);

static void* s_setManager;
static void __fastcall SetManagerVerify(CSpellConstructor* self, void*, CSpellManager* manager) {
    self->SetManager(manager);
    ei::verify::Check("CSpellConstructor::SetManager", true, self->manager_ == manager && !self->container_, "%p",
                      manager);
}
EI_VERIFY(0x006841A0, SetManagerVerify, 0, s_setManager);

static void* s_begin;
static BOOL __fastcall BeginVerify(CSpellConstructor* self, void*, char* prototype) {
    bool canBegin = self->manager_ && prototype;
    BOOL result = self->BeginConstruct(prototype);
    bool ok = !canBegin || (result == 1 && self->container_ && self->container_->prototype_ == prototype);
    ei::verify::Check("CSpellConstructor::BeginConstruct", true, ok, "prototype %p", prototype);
    return result;
}
EI_VERIFY(0x006841E0, BeginVerify, 0, s_begin);

static void* s_end;
static BOOL __fastcall EndVerify(CSpellConstructor* self) {
    bool canEnd = self->manager_ && self->container_;
    BOOL result = self->EndConstruct();
    ei::verify::Check("CSpellConstructor::EndConstruct", true, !canEnd || result == 1, "%p", self);
    return result;
}
EI_VERIFY(0x00684250, EndVerify, 0, s_end);

static void* s_addModifier;
static BOOL __fastcall AddModifierVerify(CSpellConstructor* self, void*, char* modifier) {
    BOOL result = self->AddModifier(modifier);
    bool ok = result == 0 || (result == 1 && LastNode(self->container_)->modifier == modifier);
    ei::verify::Check("CSpellConstructor::AddModifier", true, ok, "modifier %p result %d", modifier, result);
    return result;
}
EI_VERIFY(0x006842A0, AddModifierVerify, 0, s_addModifier);
