// The spell manager's own code and the spell container's lifetime (A-H23): CreateSpellObject, the
// factory (slot 14), Create / Destroy (slots 2 / 3), the destructor, and the "sequences" it keeps
// (spell.h, SpellSequence). See docs/notes/items_spells.md.
#include "spell.h"

#include "item_db.h"

#include "ei/hook.h"
#include "lib/crt.h"

#include <new>

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

namespace {

constexpr int kClassSpell = 0x3002;
constexpr int kClassSpellContainer = 0x3003;

constexpr const char* kNoSequenceMessage = "Can't find spell sequence to create child spell.\n";
constexpr const char* kSpellManagerClassName = "CSpellManager";
constexpr const char* kSpellManagerFolder = "spells";

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
EI_REWRITTEN(0x006886F0, SpellSequence::Destruct);

// --- CSpellManager -------------------------------------------------------------------------------

CSpell* CSpellManager::CreateSpellObject(int id) {
    auto* spell = CreateObjectAs<CSpell>(kClassSpell, -1);
    if (id == -1) return spell;
    const auto packed = static_cast<std::uint32_t>(id);
    const std::uint32_t low = packed & 0xFFFF;
    spell->prototypeId_ = packed;
    if (low == 0) {
        spell->InitFromPrototype(itemdb::SpellAt(packed >> 16));
        return spell;
    }
    const std::uint32_t key = packed & 0xFFFF0000;
    if (key > maxSequenceKey_) maxSequenceKey_ = key;
    SpellSequence* sequence = sequences_[key];
    if (!sequence) {
        game::FatalError(kNoSequenceMessage);
        return spell;
    }
    if (low > sequence->maxLow) sequence->maxLow = low;
    // A new entry has no container: the original then faults in CopyTo (this = 0x30), as this does.
    CSpellContainer* container = sequence->containers[low];
    container->Spell()->CopyTo(spell);
    return spell;
}
EI_REWRITTEN(0x00688EC0, CSpellManager::CreateSpellObject);

CEngineObject* CSpellManager::NewObject(int type) {
    if (type == kClassSpell) return new CSpell();
    if (type == kClassSpellContainer) return new CSpellContainer();
    return nullptr;
}
EI_REWRITTEN(0x00688CD0, CSpellManager::NewObject);

int CSpellManager::Create() { return CBaseManager::Create() != 0 ? 1 : 0; }
EI_REWRITTEN(0x00688B30, CSpellManager::Create);

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
EI_REWRITTEN(0x00688B40, CSpellManager::Destroy);

CSpellManager::~CSpellManager() {
    for (auto* node = sequences_.First(); node && node->value; node = sequences_.After(node)) {
        SpellSequence* sequence = node->value;
        sequence->Destruct();
        gamecrt::operator_delete(sequence);
    }
    sequences_.Clear();
    maxSequenceKey_ = 0;
    CResourceManager::Destroy();
    sequences_.Destroy();
}
EI_REWRITTEN_SPECIAL(0x00688990, CSpellManager::~CSpellManager);
EI_REWRITTEN_SPECIAL(0x00688970, CSpellManager::~CSpellManager);

const char* CSpellManager::GetClassName() { return kSpellManagerClassName; }
int CSpellManager::GetClassId() { return 0x33; }
const char* CSpellManager::GetFolderName() { return kSpellManagerFolder; }

// 00689570 / 00689610 (A-H25). The array takes the table's count, then element i the name of record
// i while i is below the count (read again each time).
namespace {
template <typename Record>
void ListRecordNames(const itemdb::CodeIndexedTable<Record>& table, mfc::CStringArray* names) {
    names->SetSize(static_cast<int>(table.Count()), -1);
    for (int i = 0; i < static_cast<int>(table.Count()); ++i) names->data[i] = table.At(static_cast<std::uint32_t>(i))->name;
}
} // namespace
void CSpellManager::ListSpellNames(mfc::CStringArray* names) { ListRecordNames(itemdb::TheSpellDatabase().spells, names); }
void CSpellManager::ListModifierNames(mfc::CStringArray* names) {
    ListRecordNames(itemdb::TheSpellDatabase().modifiers, names);
}
EI_REWRITTEN(0x00689570, CSpellManager::ListSpellNames);
EI_REWRITTEN(0x00689610, CSpellManager::ListModifierNames);

// --- CSpellContainer -----------------------------------------------------------------------------

// The spell and the name are members, constructed before the body (the original builds them after the code string)
// and destroyed after it (the original: the name and the spell before the string); neither allocates.
CSpellContainer::CSpellContainer() {
    ClearFields();
    modifiers_.Construct();
    // The code string: empty, with room for 8 bytes.
    codes_ = codesEnd_ = stlp::allocate_as<char>(8);
    codesCapacity_ = codes_ + 8;
    *codesEnd_ = '\0';
    CSpellContainer::Create();   // inlined in the original
}
EI_REWRITTEN_SPECIAL(0x0068BA80, CSpellContainer::CSpellContainer);

CSpellContainer::~CSpellContainer() {
    // (Slot 5, 0068BE20, called directly here, is empty.)
    prototype_ = nullptr;
    FreeModifierNodes(modifiers_.head);
    Spell()->Destroy();
    if (codes_) stlp::deallocate(codes_, static_cast<std::size_t>(codesCapacity_ - codes_));
    // The list's destructor: its (empty) clear, then the sentinel.
    FreeModifierNodes(modifiers_.head);
    stlp::deallocate(modifiers_.head, sizeof(SpellModifierNode));
}
EI_REWRITTEN_SPECIAL(0x0068BC50, CSpellContainer::~CSpellContainer);
EI_REWRITTEN_SPECIAL(0x0068BC30, CSpellContainer::~CSpellContainer);

const char* CSpellContainer::GetClassName() { return "CSpellContainer"; }
int CSpellContainer::GetClassId() { return 0x1A; }
EI_REWRITTEN(0x0068BC10, CSpellContainer::GetClassName);
EI_REWRITTEN(0x0068BC20, CSpellContainer::GetClassId);
