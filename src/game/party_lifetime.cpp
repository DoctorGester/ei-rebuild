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
#include "lib/mfc_slots.h"

#include "game_data.h"

#include <cstring>
#include <initializer_list>
#include <memory>
#include <new>
#include <utility>

namespace {

void ReleaseAll(const PartyItemArray& list) {
    for (int i = 0; i < std::ssize(list); ++i) list[i]->Release();
}

// The values the constructor and destructor set (twice in the constructor, around ClearItems).
void SetDefaults(PartyUnitRecord& record) {
    record.moveMode2 = 2;
    record.weaponIndex = 0;
    record.selected = 0;
    record.leaderId = -1;
}


} // namespace


PartyUnitRecord* PartyUnitRecord::Construct() {
    scaleX = 0.8f;
    scaleY = 0.2f;
    height = 1.0f;
    SetDefaults(*this);
    ClearItems();
    SetDefaults(*this);
    fightAtWill = 1;
    return this;
}

void PartyUnitRecord::Destruct() {
    ClearItems();
    SetDefaults(*this);
    fightAtWill = 1;
    lib::FreeStorage(spells);
    lib::FreeStorage(armor);
    lib::FreeStorage(quickItems);
    lib::FreeStorage(weapons);
    for (int i = 6; i >= 0; --i) std::destroy_at(&texts[i]);
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

// --- The record array ---------------------------------------------------------------------------

void ConstructPartyUnitRecords(PartyUnitRecord* records, int count) {
    for (PartyUnitRecord* record = records; count != 0; --count, ++record) {
        std::construct_at(record);   // value-initialised: zeroed, the texts and lists empty
        record->Construct();
    }
}

void DestructPartyUnitRecords(PartyUnitRecord* records, int count) {
    for (; count != 0; --count, ++records) records->Destruct();
}

void PartyUnitRecordElements::Destruct(PartyUnitRecord* records, int count) { DestructPartyUnitRecords(records, count); }

void PartyUnitRecordElements::Relocate(PartyUnitRecord* to, PartyUnitRecord* from, int count) {
    auto move = [](PartyUnitRecord* target, PartyUnitRecord* source) {
        std::construct_at(target, std::move(*source));
        std::destroy_at(source);
    };
    if (to < from) {
        for (int i = 0; i < count; ++i) move(to + i, from + i);
    } else {
        for (int i = count; i-- > 0;) move(to + i, from + i);
    }
}

namespace {
void DestroyPartyMembers(PartyMembers* members) { members->Destruct(); }
// Slot 2 through the slot's archive type (mfc::impl's).
struct PartyMembersStore : PartyMembers {
    void Store(mfc::impl::CArchive* ar) { Serialize(ar); }
};

// CArray<PartyUnitRecord>'s compiled table: CObject's GetRuntimeClass, the destructor (the compiler's deleting
// destructor), Serialize.
struct PartyMemberArrayClass final
    : mfc::impl::CompiledCollection<PartyMemberArrayClass, &DestroyPartyMembers, &PartyMembersStore::Store> {};
} // namespace

void* PartyMembers::Vtable() { return PartyMemberArrayClass::Vtable(); }

void PartyMemberArray::Destruct() {
    vtable = Vtable();
    FreeData();
}

PartyMemberArray* PartyMemberArray::DeletingDestruct(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
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
    members_.vtable = PartyMembers::Vtable();
    members_.data = nullptr;
    members_.growBy = 0;
    members_.maxSize = 0;
    members_.size = 0;
    itemManager_ = nullptr;
    spellManager_ = nullptr;
    itemConstructor_ = nullptr;
    spellConstructor_ = nullptr;
}

CParty::~CParty() {
    Clear(1, 1);
    CParty::Destroy();
    lib::FreeStorage(items_);
    members_.vtable = PartyMembers::Vtable();
    if (members_.data != nullptr) {
        for (int i = 0; i != members_.size; ++i) members_.data[i].Destruct();
        gamecrt::operator_delete(members_.data);
    }
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
    members_.SetSize(0, -1);
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
    members_.SetSize(0, -1);
    if (clearVariables != 0) variables_.clear();
    zone_ = game::EmptyText();
    entrance_ = 0;
    money_ = 0;
}
