// The spell manager's own code and the spell container's lifetime: CreateSpellObject, the factory, Create /
// Destroy, the destructor, and the "sequences" it keeps (spell.h, SpellSequence). See
// docs/notes/items_spells.md.
#include "item_db.h"
#include "spell.h"

#include "lib/crt.h"

#include <iterator>
#include <memory>
#include <new>
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
