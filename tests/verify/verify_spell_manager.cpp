// Checks for spell_manager.cpp (the spell manager's factory, CreateSpellObject, Create / Destroy,
// destructors; the spell container's lifetime). Scenarios spells / spells_cast: a cast with
// modifiers makes a container (slot 23 -> NewObject -> the container constructor) and clones the
// spell (CreateSpellObject(-1) -> NewObject -> CSpell::Construct). The managers of a party are
// destroyed when the party is (new game over an old one, exit).
// - Everything that allocates or releases runs ours only (stolen bytes 0); the state left is checked.
// - The class name / id getters run against the original (MOV EAX,imm32: 5 bytes).
#include "verify.h"

#include "../../src/game/spell.h"
#include "../../src/lib/crt.h"

#include <cstring>

namespace {

void* s_unused;

std::uintptr_t VtableOf(const void* object) {
    std::uintptr_t v;
    std::memcpy(&v, object, 4);
    return v;
}

CSpell* __fastcall CreateSpellObjectVerify(CSpellManager* self, void*, int id) {
    CSpell* spell = self->CreateSpellObject(id);
    bool ok = spell == nullptr ||
              (VtableOf(spell) == CSpell::kVtableAddress && (id == -1 || spell->unknown_18 == static_cast<std::uint32_t>(id)));
    ei::verify::Check("CSpellManager::CreateSpellObject", true, ok, "manager %p id %d -> %p", self, id, spell);
    return spell;
}

CEngineObject* __fastcall NewObjectVerify(CSpellManager* self, void*, int type) {
    CEngineObject* object = self->CSpellManager::NewObject(type);
    std::uintptr_t want = type == 0x3002 ? CSpell::kVtableAddress : type == 0x3003 ? CSpellContainer::kVtableAddress : 0;
    bool ok = object ? VtableOf(object) == want : want == 0;
    ei::verify::Check("CSpellManager::NewObject", true, ok, "manager %p type 0x%X -> %p", self, type, object);
    return object;
}

int __fastcall CreateVerify(CSpellManager* self) {
    int result = self->CSpellManager::Create();
    ei::verify::Check("CSpellManager::Create", true, result == 0 || result == 1, "manager %p -> %d", self, result);
    return result;
}

bool Emptied(const CSpellManager* m) { return m->sequences_.Size() == 0 && m->maxSequenceKey_ == 0; }

void __fastcall DestroyVerify(CSpellManager* self) {
    self->CSpellManager::Destroy();
    ei::verify::Check("CSpellManager::Destroy", true, Emptied(self), "manager %p", self);
}

void __fastcall ManagerDestructVerify(CSpellManager* self) {
    self->Destruct();
    ei::verify::Check("CSpellManager::Destruct", true, Emptied(self), "manager %p", self);
}

CSpellManager* __fastcall ManagerDeleteVerify(CSpellManager* self, void*, unsigned flags) {
    CSpellManager* result = self->DeletingDestruct(flags & ~1u);
    ei::verify::Check("CSpellManager::DeletingDestruct", true, result == self && Emptied(self), "manager %p", self);
    if (flags & 1) gamecrt::operator_delete(self);
    return result;
}

void __fastcall SequenceDestructVerify(SpellSequence* self) {
    self->Destruct();
    ei::verify::Check("SpellSequence::Destruct", true, self->containers.Size() == 0, "sequence %p", self);
}

CSpellContainer* __fastcall ContainerConstructVerify(CSpellContainer* self) {
    CSpellContainer* result = self->Construct();
    const char* empty = *reinterpret_cast<char* const*>(0x00796E48);   // afxEmptyString's data
    bool ok = result == self && VtableOf(self) == CSpellContainer::kVtableAddress && self->prototype_ == nullptr &&
              self->modifiers_ && self->modifiers_->next == self->modifiers_ && self->modifiers_->prev == self->modifiers_ &&
              self->codes_ && self->codesEnd_ == self->codes_ && self->codesCapacity_ == self->codes_ + 8 &&
              *self->codes_ == 0 && VtableOf(self->Spell()) == CSpell::kVtableAddress &&
              static_cast<const char*>(self->name_) == empty && self->id_ == -1 && self->refCount_ == 0;
    ei::verify::Check("CSpellContainer::Construct", true, ok, "container %p", self);
    return result;
}

bool ContainerDestroyed(const CSpellContainer* c) {
    return VtableOf(c) == CEngineObject::kVtableAddress && c->prototype_ == nullptr && c->owner_ == nullptr;
}

void __fastcall ContainerDestructVerify(CSpellContainer* self) {
    self->Destruct();
    ei::verify::Check("CSpellContainer::Destruct", true, ContainerDestroyed(self), "container %p", self);
}

CSpellContainer* __fastcall ContainerDeleteVerify(CSpellContainer* self, void*, unsigned flags) {
    CSpellContainer* result = self->DeletingDestruct(flags & ~1u);
    ei::verify::Check("CSpellContainer::DeletingDestruct", true, result == self && ContainerDestroyed(self), "container %p",
                      self);
    if (flags & 1) gamecrt::operator_delete(self);
    return result;
}

const char*(__fastcall* s_managerName)(CSpellManager*);
const char* __fastcall ManagerNameVerify(CSpellManager* self) {
    const char* expected = s_managerName(self);
    ei::verify::Check("CSpellManager::GetClassName", expected, SpellManagerClassName(self), "%p", self);
    return expected;
}
int(__fastcall* s_managerId)(CSpellManager*);
int __fastcall ManagerIdVerify(CSpellManager* self) {
    int expected = s_managerId(self);
    ei::verify::Check("CSpellManager::GetClassId", expected, SpellManagerClassId(self), "%p", self);
    return expected;
}
const char*(__fastcall* s_managerFolder)(CSpellManager*);
const char* __fastcall ManagerFolderVerify(CSpellManager* self) {
    const char* expected = s_managerFolder(self);
    ei::verify::Check("CSpellManager::Folder", expected, SpellManagerFolder(self), "%p", self);
    return expected;
}
const char*(__fastcall* s_containerName)(CSpellContainer*);
const char* __fastcall ContainerNameVerify(CSpellContainer* self) {
    const char* expected = s_containerName(self);
    ei::verify::Check("CSpellContainer::GetClassName", expected, SpellContainerClassName(self), "%p", self);
    return expected;
}
int(__fastcall* s_containerId)(CSpellContainer*);
int __fastcall ContainerIdVerify(CSpellContainer* self) {
    int expected = s_containerId(self);
    ei::verify::Check("CSpellContainer::GetClassId", expected, SpellContainerClassId(self), "%p", self);
    return expected;
}

} // namespace

EI_VERIFY(0x00688EC0, CreateSpellObjectVerify, 0, s_unused);
EI_VERIFY(0x00688CD0, NewObjectVerify, 0, s_unused);
EI_VERIFY(0x00688B30, CreateVerify, 0, s_unused);
EI_VERIFY(0x00688B40, DestroyVerify, 0, s_unused);
EI_VERIFY(0x00688990, ManagerDestructVerify, 0, s_unused);
EI_VERIFY(0x00688970, ManagerDeleteVerify, 0, s_unused);
EI_VERIFY(0x006886F0, SequenceDestructVerify, 0, s_unused);
EI_VERIFY(0x0068BA80, ContainerConstructVerify, 0, s_unused);
EI_VERIFY(0x0068BC50, ContainerDestructVerify, 0, s_unused);
EI_VERIFY(0x0068BC30, ContainerDeleteVerify, 0, s_unused);
EI_VERIFY(0x00688950, ManagerNameVerify, 5, s_managerName);
EI_VERIFY(0x00688960, ManagerIdVerify, 5, s_managerId);
EI_VERIFY(0x00688940, ManagerFolderVerify, 5, s_managerFolder);
EI_VERIFY(0x0068BC10, ContainerNameVerify, 5, s_containerName);
EI_VERIFY(0x0068BC20, ContainerIdVerify, 5, s_containerId);
