// Parties and their unit records: the records' constructor and destructor bodies and item clear,
// the record array (the array's resize and element construction), the party's constructor,
// destructor, Create / Destroy / Clear, and the constructors of the managers a party makes. The
// record's item lists are aligned pointer arrays; four more copies of their SetSize are
// here. See party.h and docs/notes/items_spells.md ("Party").
#include "game_strings.h"
#include "item.h"
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
    --object->refCount_;
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
    manager_ = nullptr;
}

CSpellManager::CSpellManager() {
    maxSequenceKey_ = 0;
}

CParty::CParty() = default;

CParty::~CParty() {
    Clear(1, 1);
    CParty::Destroy();
}

const char* CParty::ClassName() { return "CParty"; }
ClassId CParty::GetClassId() { return ClassId::Party; }

CItemConstructor* CParty::ItemConstructor() { return itemConstructor_; }
CSpellConstructor* CParty::SpellConstructor() { return spellConstructor_; }
CItemManager* CParty::ItemFactory() { return itemManager_; }
CSpellManager* CParty::SpellManager() { return spellManager_; }

void CParty::Create() {
    itemManager_ = new CItemManager();
    itemManager_->AddRef();   // not tested for null
    itemManager_->Init(this);

    spellManager_ = new CSpellManager();
    spellManager_->AddRef();
    spellManager_->Init(this);

    itemConstructor_ = new CItemConstructor();
    itemConstructor_->AddRef();
    itemConstructor_->Create();

    spellConstructor_ = new CSpellConstructor();
    spellConstructor_->AddRef();
    spellConstructor_->Create();

    Clear(1, 0);
}

void CParty::Destroy() {
    members_.clear();
    ReleaseItems(items_);
    items_.clear();
    Drop(itemConstructor_);
    Drop(spellConstructor_);
    Drop(itemManager_);
    Drop(spellManager_);
}

void CParty::Clear(int clearVariables, int keepSpells) {
    ReleaseItems(items_);
    items_.clear();
    if (itemManager_ != nullptr) itemManager_->DestroyAll();
    if (spellManager_ != nullptr) {
        spellManager_->Destroy();
        if (keepSpells == 0) spellManager_->Init(this);
    }
    members_.clear();
    if (clearVariables != 0) variables_.clear();
    zone_ = game::kEmptyText;
    entrance_ = 0;
    money_ = 0;
}
