// Parties and their unit records: the records' constructor and destructor bodies and item clear,
// the record array (the array's resize and element construction), the party's constructor,
// destructor, Create / Destroy / Clear, and the constructors of the managers a party makes. The
// record's item lists are aligned pointer arrays; four more copies of their SetSize are
// here. See party.h and docs/notes/items_spells.md ("Party").
#include "game_strings.h"
#include "item.h"
#include "item_manager.h"
#include "party.h"
#include "spell.h"
#include "unit.h"

#include "lib/crt.h"

#include <cstring>
#include <initializer_list>
#include <memory>
#include <new>
#include <utility>

namespace {

void ReleaseAll(const std::vector<CItem*>& list) {
    for (int i = 0; i < std::ssize(list); ++i) list[i]->Release();
}

// A counted object the party owns: its count dropped and the object deleted, whatever the count, and the
// pointer cleared.
template <typename T>
void Drop(T*& object) {
    if (object == nullptr) return;
    --object->refCount;
    delete object;
    object = nullptr;
}

void ReleaseItems(const std::vector<CItem*>& items) {
    for (int i = 0; i < std::ssize(items); ++i) items[i]->Release();
}

} // namespace

PartyUnitRecord& PartyUnitRecord::operator=(PartyUnitRecord&& from) noexcept {
    if (this != &from) {
        std::destroy_at(this);
        std::construct_at(this, std::move(from));
    }
    return *this;
}

void PartyUnitRecord::ClearItems() {
    ReleaseAll(weapons);
    ReleaseAll(quickItems);
    ReleaseAll(armor);
    ReleaseAll(spells);
    weapons.clear();
    quickItems.clear();
    armor.clear();
    spells.clear();
}

// --- The managers and constructors a party makes ------------------------------------------------

CSpellConstructor::CSpellConstructor() {
    manager = nullptr;
}

CSpellManager::CSpellManager() {
    maxSequenceKey = 0;
}

CParty::CParty() = default;

CParty::~CParty() {
    Clear(1, 1);
    CParty::Destroy();
}

const char* CParty::ClassName() { return "CParty"; }
ClassId CParty::GetClassId() { return ClassId::Party; }

CItemConstructor* CParty::ItemConstructor() { return itemConstructor; }
CSpellConstructor* CParty::SpellConstructor() { return spellConstructor; }
CItemManager* CParty::ItemFactory() { return itemManager; }
CSpellManager* CParty::SpellManager() { return spellManager; }

void CParty::Create() {
    itemManager = new CItemManager();
    itemManager->AddRef();    // not tested for null
    itemManager->Init(this);

    spellManager = new CSpellManager();
    spellManager->AddRef();
    spellManager->Init(this);

    itemConstructor = new CItemConstructor();
    itemConstructor->AddRef();
    itemConstructor->Create();

    spellConstructor = new CSpellConstructor();
    spellConstructor->AddRef();
    spellConstructor->Create();

    Clear(1, 0);
}

void CParty::Destroy() {
    members.clear();
    ReleaseItems(items);
    items.clear();
    Drop(itemConstructor);
    Drop(spellConstructor);
    Drop(itemManager);
    Drop(spellManager);
}

void CParty::Clear(int clearVariables, int keepSpells) {
    ReleaseItems(items);
    items.clear();
    if (itemManager != nullptr) itemManager->DestroyAll();
    if (spellManager != nullptr) {
        spellManager->Destroy();
        if (keepSpells == 0) spellManager->Init(this);
    }
    members.clear();
    if (clearVariables != 0) variables.clear();
    zone = game::kEmptyText;
    entrance = 0;
    money = 0;
}
