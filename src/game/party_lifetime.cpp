// Parties and their unit records: the records' constructor and destructor bodies and item clear,
// the record array (MFC CArray::SetSize and ConstructElements), the party's constructor,
// destructor, Create / Destroy / Clear, and the constructors of the managers a party makes. The
// record's item lists are aligned pointer arrays; four more copies of their SetSize are
// here. See party.h and docs/notes/items_spells.md ("Party").
#include "party.h"

#include "game_strings.h"
#include "item.h"
#include "spell.h"
#include "unit.h"

#include "lib/crt.h"

#include <cstring>
#include <initializer_list>
#include <memory>
#include <new>
#include <utility>

namespace {

void ReleaseAll(const PartyItemArray& list) {
    for (int i = 0; i < std::ssize(list); ++i) list[i]->Release();
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
    ClearFields();
    container_ = nullptr;
    // (The game then releases +0x20, which it has just cleared.)
    manager_ = nullptr;
}

CSpellManager::CSpellManager() {
    maxSequenceKey_ = 0;
}

namespace {

// A counted object the party owns: its count dropped and the object deleted, whatever the count, and the
// pointer cleared.
template <typename T>
void Drop(T*& object) {
    if (object == nullptr) return;
    --object->refCount_;
    delete object;
    object = nullptr;
}

void ReleaseItems(const PartyItemArray& items) {
    for (int i = 0; i < std::ssize(items); ++i) items[i]->Release();
}

} // namespace

CParty::CParty() {
    ClearFields();
    itemManager_ = nullptr;
    spellManager_ = nullptr;
    itemConstructor_ = nullptr;
    spellConstructor_ = nullptr;
}

CParty::~CParty() {
    Clear(1, 1);
    CParty::Destroy();
    lib::FreeStorage(items_);
}

const char* CParty::GetClassName() { return "CParty"; }
int CParty::GetClassId() { return 9; }

CItemConstructor* CParty::ItemConstructor() { return itemConstructor_; }
CSpellConstructor* CParty::SpellConstructor() { return spellConstructor_; }
CItemManager* CParty::ItemFactory() { return itemManager_; }
CSpellManager* CParty::SpellManager() { return spellManager_; }

int CParty::Create() {
    itemManager_ = new CItemManager();
    ++itemManager_->refCount_;   // not tested for null
    itemManager_->Init(this);

    spellManager_ = new CSpellManager();
    ++spellManager_->refCount_;
    spellManager_->Init(this);

    itemConstructor_ = new CItemConstructor();
    ++itemConstructor_->refCount_;
    itemConstructor_->Create();

    spellConstructor_ = new CSpellConstructor();
    ++spellConstructor_->refCount_;
    spellConstructor_->Create();

    Clear(1, 0);
    return 1;
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
    zone_ = game::EmptyText();
    entrance_ = 0;
    money_ = 0;
}
