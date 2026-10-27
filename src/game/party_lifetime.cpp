// Parties and their unit records: the records' constructor and destructor bodies and item clear,
// the record array (MFC CArray::SetSize and ConstructElements), the party's constructor,
// destructor, Create / Destroy / Clear, and the constructors of the managers a party makes. The
// record's item lists are aligned pointer arrays; four more copies of their SetSize are
// registered here. See party.h and docs/notes/items_spells.md ("Party").
#include "party.h"

#include "game_strings.h"
#include "item.h"
#include "spell.h"
#include "unit.h"

#include "lib/crt.h"
#include "lib/mfc_slots.h"

#include "ei/hook.h"
#include "game_data.h"

#include <cstring>
#include <initializer_list>
#include <memory>
#include <new>
#include <utility>

namespace {

// The aligned block of a list (kept at data[-1]) freed, the pointer left as it is.
void FreeStorage(PartyItemArray& list) {
    if (list.data != nullptr) gamecrt::operator_delete(list.data[-1]);
}

void ReleaseAll(const PartyItemArray& list) {
    for (int i = 0; i < list.size; ++i) list.data[i]->Release();
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
    for (std::string& text : texts) std::construct_at(&text);
    for (PartyItemArray* list : {&weapons, &quickItems, &armor, &spells}) {
        list->data = nullptr;
        list->growBy = 0;
        list->capacity = 0;
        list->size = 0;
    }
    scaleX = 0.8f;
    scaleY = 0.2f;
    height = 1.0f;
    SetDefaults(*this);
    ClearItems();
    SetDefaults(*this);
    fightAtWill = 1;
    return this;
}
EI_REWRITTEN(0x0065F5F0, PartyUnitRecord::Construct);

void PartyUnitRecord::Destruct() {
    ClearItems();
    SetDefaults(*this);
    fightAtWill = 1;
    FreeStorage(spells);
    FreeStorage(armor);
    FreeStorage(quickItems);
    FreeStorage(weapons);
    for (int i = 6; i >= 0; --i) std::destroy_at(&texts[i]);
}
EI_REWRITTEN(0x0065F740, PartyUnitRecord::Destruct);

void PartyUnitRecord::ClearItems() {
    ReleaseAll(weapons);
    ReleaseAll(quickItems);
    ReleaseAll(armor);
    ReleaseAll(spells);
    weapons.SetSize(0, -1);
    quickItems.SetSize(0, -1);
    armor.SetSize(0, -1);
    spells.SetSize(0, -1);
}
EI_REWRITTEN(0x0065F860, PartyUnitRecord::ClearItems);

EI_REWRITTEN(0x00663960, AlignedPtrArray::SetSize);
EI_REWRITTEN(0x00663AD0, AlignedPtrArray::SetSize);
EI_REWRITTEN(0x00663C00, AlignedPtrArray::SetSize);
EI_REWRITTEN(0x00663D30, AlignedPtrArray::SetSize);
// --- The record array ---------------------------------------------------------------------------

void __stdcall ConstructPartyUnitRecords(PartyUnitRecord* records, int count) {
    std::memset(records, 0, static_cast<std::size_t>(count) * sizeof(PartyUnitRecord));
    for (PartyUnitRecord* record = records; count != 0; --count, ++record)
        if (record != nullptr) record->Construct();
}
EI_REWRITTEN(0x00664880, ConstructPartyUnitRecords);

void __stdcall DestructPartyUnitRecords(PartyUnitRecord* records, int count) {
    for (; count != 0; --count, ++records) records->Destruct();
}
EI_REWRITTEN(0x00664850, DestructPartyUnitRecords);

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

PartyUnitRecord* PartyUnitRecord::DeletingDestruct(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REWRITTEN(0x00664AC0, PartyUnitRecord::DeletingDestruct);

namespace {
void __fastcall DestroyPartyMembers(PartyMembers* members) { members->Destruct(); }
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
EI_REWRITTEN(0x00664160, PartyMemberArray::Destruct);

PartyMemberArray* PartyMemberArray::DeletingDestruct(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REWRITTEN(0x00664470, PartyMemberArray::DeletingDestruct);

EI_REWRITTEN(0x00663FA0, PartyMemberArray::SetSize);
// CArray<PartyUnitRecord>::Serialize, in the array's table (PartyMembers::Vtable()); no code calls it (the
// parties go through CStream, party_save.cpp).
EI_REWRITTEN(0x006641D0, PartyMemberArray::Serialize<mfc::impl::CArchive>);

// --- The managers and constructors a party makes ------------------------------------------------

CSpellConstructor::CSpellConstructor() {
    ClearFields();
    container_ = nullptr;
    // (The game then releases +0x20, which it has just cleared.)
    manager_ = nullptr;
}
EI_REWRITTEN_SPECIAL(0x00683FF0, CSpellConstructor::CSpellConstructor);

CSpellManager::CSpellManager() {
    sequences_.InitBuckets(100);
    maxSequenceKey_ = 0;
}
EI_REWRITTEN_SPECIAL(0x00688840, CSpellManager::CSpellManager);


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
    for (int i = 0; i < items.size; ++i) items.data[i]->Release();
}

} // namespace

CParty::CParty() {
    ClearFields();
    members_.vtable = PartyMembers::Vtable();
    members_.data = nullptr;
    members_.growBy = 0;
    members_.maxSize = 0;
    members_.size = 0;
    items_.data = nullptr;
    items_.growBy = 0;
    items_.capacity = 0;
    items_.size = 0;
    variables_.InitZeroedBuckets(100);
    itemManager_ = nullptr;
    spellManager_ = nullptr;
    itemConstructor_ = nullptr;
    spellConstructor_ = nullptr;
}
EI_REWRITTEN_SPECIAL(0x00661050, CParty::CParty);

CParty::~CParty() {
    Clear(1, 1);
    CParty::Destroy();
    variables_.Destroy();
    if (items_.data != nullptr) gamecrt::operator_delete(items_.data[-1]);
    members_.vtable = PartyMembers::Vtable();
    if (members_.data != nullptr) {
        for (int i = 0; i != members_.size; ++i) members_.data[i].Destruct();
        gamecrt::operator_delete(members_.data);
    }
}
EI_REWRITTEN_SPECIAL(0x00661190, CParty::~CParty);
EI_REWRITTEN_SPECIAL(0x006611D0, CParty::~CParty);   // the destructor called in place

const char* CParty::GetClassName() { return "CParty"; }
int CParty::GetClassId() { return 9; }
EI_REWRITTEN(0x0065F560, CParty::GetClassName);
EI_REWRITTEN(0x0065F570, CParty::GetClassId);

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
EI_REWRITTEN(0x006614F0, CParty::Create);

void CParty::Destroy() {
    members_.SetSize(0, -1);
    ReleaseItems(items_);
    items_.SetSize(0, -1);
    Drop(itemConstructor_);
    Drop(spellConstructor_);
    Drop(itemManager_);
    Drop(spellManager_);
}
EI_REWRITTEN(0x00661640, CParty::Destroy);

void CParty::Clear(int clearVariables, int keepSpells) {
    ReleaseItems(items_);
    items_.SetSize(0, -1);
    if (itemManager_ != nullptr) itemManager_->DestroyAll();
    if (spellManager_ != nullptr) {
        spellManager_->Destroy();
        if (keepSpells == 0) spellManager_->Init(this);
    }
    members_.SetSize(0, -1);
    if (clearVariables != 0) variables_.Clear();   // each name destroyed, its node back to the allocator
    zone_ = game::EmptyText();
    entrance_ = 0;
    money_ = 0;
}
EI_REWRITTEN(0x00661700, CParty::Clear);
