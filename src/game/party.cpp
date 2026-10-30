// The party: its constructor and destructor, Create / Destroy (the managers and constructors it makes) and
// Clear. See party.h and docs/notes/items_spells.md ("Party").
#include "party.h"

#include "game_strings.h"
#include "item.h"
#include "item_manager.h"
#include "spell.h"
#include "unit.h"

namespace {

// A counted object the party owns: its count dropped and the object deleted, whatever the count, and the
// pointer cleared.
template <typename T>
void Drop(T*& object) {
    if (object == nullptr) return;
    --object->refCount;
    delete object;
    object = nullptr;
}

void ReleaseItems(const std::vector<Item*>& items) {
    for (int i = 0; i < std::ssize(items); ++i) items[i]->Release();
}

} // namespace

CParty::CParty() = default;

CParty::~CParty() {
    Clear(1, 1);
    CParty::Destroy();
}

const char* CParty::ClassName() { return "CParty"; }
ClassId CParty::GetClassId() { return ClassId::Party; }

CItemConstructor* CParty::GetItemConstructor() { return itemConstructor; }
CSpellConstructor* CParty::GetSpellConstructor() { return spellConstructor; }
ItemManager* CParty::ItemFactory() { return itemManager; }
CSpellManager* CParty::GetSpellManager() { return spellManager; }

void CParty::Create() {
    itemManager = new ItemManager();
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
