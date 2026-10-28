// The spell manager's own code and the spell container's lifetime: CreateSpellObject, the factory, Create /
// Destroy, the destructor, and the "sequences" it keeps (spell.h, SpellSequence). See
// docs/notes/items_spells.md.
#include "spell.h"

#include "item_db.h"

#include "lib/crt.h"

#include <iterator>
#include <new>
#include <vector>

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

namespace {

constexpr int kClassSpell = 0x3002;
constexpr int kClassSpellContainer = 0x3003;

constexpr const char* kNoSequenceMessage = "Can't find spell sequence to create child spell.\n";
constexpr const char* kSpellManagerClassName = "CSpellManager";
constexpr const char* kSpellManagerFolder = "spells";

// Every container of a sequence loses a reference, in iterator order; the walk stops at the first entry
// without a container.
void ReleaseContainers(SpellSequence* sequence) {
    for (auto* node = sequence->containers.First(); node && node->value; node = sequence->containers.After(node))
        node->value->Release();
}

// The modifier list's nodes back to the node allocator; the sentinel stays, linked to itself.
void FreeModifierNodes(SpellModifierNode sentinel) {
    for (SpellModifierNode node = std::next(sentinel); node != sentinel;) {
        SpellModifierNode current = node;
        ++node;
        stlp::deallocate(current, sizeof(SpellModifierNode));
    }
    std::next(sentinel) = sentinel;
    std::prev(sentinel) = sentinel;
}

} // namespace


void SpellSequence::Destruct() {
    ReleaseContainers(this);
    containers.Destroy();
}

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
    // A new entry has no container: CopyTo then faults (this = 0x30), as the game does.
    CSpellContainer* container = sequence->containers[low];
    container->Spell()->CopyTo(spell);
    return spell;
}

CEngineObject* CSpellManager::NewObject(int type) {
    if (type == kClassSpell) return new CSpell();
    if (type == kClassSpellContainer) return new CSpellContainer();
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

const char* CSpellManager::GetClassName() { return kSpellManagerClassName; }
int CSpellManager::GetClassId() { return 0x33; }
const char* CSpellManager::GetFolderName() { return kSpellManagerFolder; }

// The array takes the table's count, then element i the name of record i while i is below the count (read
// again each time).
namespace {
template <typename Record>
void ListRecordNames(const itemdb::CodeIndexedTable<Record>& table, std::vector<std::string>* names) {
    names->resize(table.Count());
    for (std::uint32_t i = 0; i < table.Count(); ++i) (*names)[i] = table.At(i)->name;
}
} // namespace
void CSpellManager::ListSpellNames(std::vector<std::string>* names) { ListRecordNames(itemdb::TheSpellDatabase().spells, names); }
void CSpellManager::ListModifierNames(std::vector<std::string>* names) {
    ListRecordNames(itemdb::TheSpellDatabase().modifiers, names);
}

// The spell and the name are members, constructed before the body (the game builds them after the code
// string) and destroyed after it (the game: the name and the spell before the string); neither allocates.
CSpellContainer::CSpellContainer() {
    ClearFields();
    // The code string: empty, with room for 8 bytes.
    CSpellContainer::Create();
}

CSpellContainer::~CSpellContainer() {
    // (ClearState is empty and called directly here.)
    prototype_ = nullptr;
    FreeModifierNodes(modifiers_.end());
    Spell()->Destroy();
    // The list's destructor: its (empty) clear, then the sentinel.
    FreeModifierNodes(modifiers_.end());
    stlp::deallocate(modifiers_.end(), sizeof(SpellModifierNode));
}

const char* CSpellContainer::GetClassName() { return "CSpellContainer"; }
int CSpellContainer::GetClassId() { return 0x1A; }
