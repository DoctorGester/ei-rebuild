// The spell manager's own code and the spell container's lifetime (A-H23): CreateSpellObject, the
// factory (slot 14), Create / Destroy (slots 2 / 3), the destructor, and the "sequences" it keeps
// (spell.h, SpellSequence). See docs/notes/items_spells.md.
#include "spell.h"

#include "item_db.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"

#include <new>

namespace {

constexpr int kClassSpell = 0x3002;
constexpr int kClassSpellContainer = 0x3003;

constexpr std::uintptr_t kNoSequenceMessage = std::uintptr_t(0x00795908);    // "Can't find spell sequence ..."
constexpr std::uintptr_t kSpellManagerClassName = std::uintptr_t(0x007958F8);  // "CSpellManager"
constexpr std::uintptr_t kSpellManagerFolder = std::uintptr_t(0x007947FC);     // "spells"
constexpr std::uintptr_t kSpellContainerClassName = std::uintptr_t(0x007959E0);  // "CSpellContainer"

// The constructors through their game addresses, so their verify wrappers see the factory's calls.
__declspec(naked) CSpell* __fastcall ConstructSpellAt(void*) EI_ORIGINAL(0x0068B1F0)
__declspec(naked) CSpellContainer* __fastcall ConstructSpellContainerAt(void*) EI_ORIGINAL(0x0068BA80)

// Every container of a sequence loses a reference (slot 4), in iterator order; the walk stops at
// the first entry without a container.
void ReleaseContainers(SpellSequence* sequence) {
    for (auto* node = sequence->containers.First(); node && node->value; node = sequence->containers.After(node))
        node->value->Release();
}

// The modifier list's nodes back to the node allocator; the sentinel stays, linked to itself.
void FreeModifierNodes(SpellModifierNode* sentinel) {
    for (SpellModifierNode* node = sentinel->next; node != sentinel;) {
        SpellModifierNode* current = node;
        node = node->next;
        stlp::deallocate(current, sizeof(SpellModifierNode));
    }
    sentinel->next = sentinel;
    sentinel->prev = sentinel;
}

} // namespace

// --- Sequences -----------------------------------------------------------------------------------

void SpellSequence::Destruct() {
    ReleaseContainers(this);
    containers.Destroy();
}
EI_REPLACE_METHOD(0x006886F0, SpellSequence::Destruct);

// --- CSpellManager -------------------------------------------------------------------------------

CSpell* CSpellManager::CreateSpellObject(int id) {
    auto* spell = static_cast<CSpell*>(CreateObject(kClassSpell, -1));
    if (id == -1) return spell;
    const auto packed = static_cast<std::uint32_t>(id);
    const std::uint32_t low = packed & 0xFFFF;
    spell->prototypeId_ = packed;
    if (low == 0) {
        spell->InitFromPrototype(itemdb::Spells().At(packed >> 16, itemdb::kSpellSize));
        return spell;
    }
    const std::uint32_t key = packed & 0xFFFF0000;
    if (key > maxSequenceKey_) maxSequenceKey_ = key;
    SpellSequence* sequence = sequences_[key];
    if (!sequence) {
        game::FatalError(reinterpret_cast<const char*>(kNoSequenceMessage));
        return spell;
    }
    if (low > sequence->maxLow) sequence->maxLow = low;
    // A new entry has no container: the original then faults in CopyTo (this = 0x30), as this does.
    CSpellContainer* container = sequence->containers[low];
    container->Spell()->CopyTo(spell);
    return spell;
}
EI_REPLACE_METHOD(0x00688EC0, CSpellManager::CreateSpellObject);

CEngineObject* CSpellManager::NewObject(int type) {
    if (type == kClassSpell) {
        if (void* memory = gamecrt::operator_new(sizeof(CSpell))) return ConstructSpellAt(memory);
    } else if (type == kClassSpellContainer) {
        if (void* memory = gamecrt::operator_new(sizeof(CSpellContainer))) return ConstructSpellContainerAt(memory);
    }
    return nullptr;
}

int CSpellManager::Create() { return CBaseManager::Create() != 0 ? 1 : 0; }

void CSpellManager::Destroy() {
    for (auto* node = sequences_.First(); node && node->value; node = sequences_.After(node)) {
        SpellSequence* sequence = node->value;
        ReleaseContainers(sequence);
        sequence->containers.Destroy();
        gamecrt::operator_delete(sequence);
    }
    sequences_.Clear();
    maxSequenceKey_ = 0;
    CResourceManager::Destroy();
}

void CSpellManager::Destruct() {
    ei::SetVtable(this, kVtableAddress);
    for (auto* node = sequences_.First(); node && node->value; node = sequences_.After(node)) {
        SpellSequence* sequence = node->value;
        sequence->Destruct();
        gamecrt::operator_delete(sequence);
    }
    sequences_.Clear();
    maxSequenceKey_ = 0;
    CResourceManager::Destroy();
    sequences_.Destroy();
    CResourceManager::Destruct();
}
EI_REPLACE_METHOD(0x00688990, CSpellManager::Destruct);

CSpellManager* CSpellManager::DeletingDestruct(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x00688970, CSpellManager::DeletingDestruct);

CSpellContainer* __fastcall NextSpellContainer(SpellContainerWalk* walk) {
    if (!walk->node) return nullptr;
    walk->node = walk->table->After(walk->node);
    return walk->node ? walk->node->value : nullptr;
}
EI_REPLACE(0x00689820, NextSpellContainer);

namespace {
int __fastcall SpellManagerCreateHook(CSpellManager* self) { return self->CSpellManager::Create(); }
void __fastcall SpellManagerDestroyHook(CSpellManager* self) { self->CSpellManager::Destroy(); }
CEngineObject* __fastcall SpellManagerNewObjectHook(CSpellManager* self, void*, int type) {
    return self->CSpellManager::NewObject(type);
}
} // namespace
EI_REPLACE(0x00688B30, SpellManagerCreateHook);
EI_REPLACE(0x00688B40, SpellManagerDestroyHook);
EI_REPLACE(0x00688CD0, SpellManagerNewObjectHook);

const char* __fastcall SpellManagerClassName(CSpellManager*) { return reinterpret_cast<const char*>(kSpellManagerClassName); }
int __fastcall SpellManagerClassId(CSpellManager*) { return 0x33; }
EI_REPLACE(0x00688950, SpellManagerClassName);
EI_REPLACE(0x00688960, SpellManagerClassId);
// 00688940: slot 25, the folder "spells" (A-H24).
const char* __fastcall SpellManagerFolder(CSpellManager*) { return reinterpret_cast<const char*>(kSpellManagerFolder); }
EI_REPLACE(0x00688940, SpellManagerFolder);

// 00689570 / 00689610 (A-H25). The array takes the table's count, then element i the name of record
// i while i is below the count (read again each time).
namespace {
template <typename Record>
void ListRecordNames(const RecordTable& table, mfc::CStringArray* names) {
    const int stride = static_cast<int>(sizeof(Record));
    names->SetSize(static_cast<int>(table.end - table.begin) / stride, -1);
    for (int i = 0; i < static_cast<int>(table.end - table.begin) / stride; ++i) {
        auto* record = reinterpret_cast<const Record*>(table.At(static_cast<std::uint32_t>(i), stride));
        names->data[i] = record->name;
    }
}
} // namespace
void CSpellManager::ListSpellNames(mfc::CStringArray* names) { ListRecordNames<itemdb::SpellRecord>(itemdb::Spells(), names); }
void CSpellManager::ListModifierNames(mfc::CStringArray* names) {
    ListRecordNames<itemdb::SpellModifierRecord>(itemdb::SpellModifiers(), names);
}
EI_REPLACE_METHOD(0x00689570, CSpellManager::ListSpellNames);
EI_REPLACE_METHOD(0x00689610, CSpellManager::ListModifierNames);

// --- CSpellContainer -----------------------------------------------------------------------------

CSpellContainer* CSpellContainer::Construct() {
    ClearFields();
    auto* sentinel = static_cast<SpellModifierNode*>(stlp::allocate(sizeof(SpellModifierNode)));
    sentinel->next = sentinel;
    sentinel->prev = sentinel;
    modifiers_ = sentinel;
    // The code string: empty, with room for 8 bytes (00424E40).
    codes_ = codesEnd_ = static_cast<char*>(stlp::allocate(8));
    codesCapacity_ = codes_ + 8;
    *codesEnd_ = '\0';
    Spell()->Construct();
    new (&name_) mfc::CString();
    ei::SetVtable(this, kVtableAddress);
    Reset();   // inlined in the original
    return this;
}
EI_REPLACE_METHOD(0x0068BA80, CSpellContainer::Construct);

void CSpellContainer::Destruct() {
    ei::SetVtable(this, kVtableAddress);
    // (Slot 5, 0068BE20, called directly here, is empty.)
    prototype_ = nullptr;
    FreeModifierNodes(modifiers_);
    Spell()->Destroy();   // slot 3: CSpell::ClearModifiers
    name_.~CString();
    Spell()->Destruct();
    if (codes_) stlp::deallocate(codes_, static_cast<std::size_t>(codesCapacity_ - codes_));
    // The list's destructor: its (empty) clear, then the sentinel.
    FreeModifierNodes(modifiers_);
    stlp::deallocate(modifiers_, sizeof(SpellModifierNode));
    DestructBase();
}
EI_REPLACE_METHOD(0x0068BC50, CSpellContainer::Destruct);

CSpellContainer* CSpellContainer::DeletingDestruct(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x0068BC30, CSpellContainer::DeletingDestruct);

const char* __fastcall SpellContainerClassName(CSpellContainer*) {
    return reinterpret_cast<const char*>(kSpellContainerClassName);
}
int __fastcall SpellContainerClassId(CSpellContainer*) { return 0x1A; }
EI_REPLACE(0x0068BC10, SpellContainerClassName);
EI_REPLACE(0x0068BC20, SpellContainerClassId);
