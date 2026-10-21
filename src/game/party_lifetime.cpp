// Parties and their unit records: the records' constructor and destructor bodies and item clear,
// the record array (MFC CArray::SetSize and ConstructElements), the party's constructor,
// destructor, Create / Destroy / Clear, and the constructors of the managers a party makes. The
// record's item lists are aligned pointer arrays; four more copies of their SetSize (00519D70) are
// registered here. See party.h and docs/notes/items_spells.md ("Party").
#include "party.h"

#include "game_strings.h"
#include "item.h"
#include "spell.h"
#include "unit.h"

#include "lib/crt.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "game_data.h"

#include <cstring>
#include <initializer_list>
#include <new>

namespace {

// The aligned block of a list (kept at data[-1]) freed, the pointer left as it is.
void FreeStorage(AlignedPtrArray& list) {
    if (list.data != nullptr) gamecrt::operator_delete(list.data[-1]);
}

void ReleaseAll(const AlignedPtrArray& list) {
    for (int i = 0; i < list.size; ++i) static_cast<CEngineObject*>(list.data[i])->Release();
}

// The values the constructor and destructor set (twice in the constructor, around ClearItems).
void SetDefaults(PartyUnitRecord& record) {
    record.moveMode2 = 2;
    record.weaponIndex = 0;
    record.selected = 0;
    record.leaderId = -1;
}


} // namespace

// --- PartyUnitRecord ----------------------------------------------------------------------------

PartyUnitRecord* PartyUnitRecord::Construct() {
    for (mfc::CString& text : texts) new (&text) mfc::CString();
    for (CItem*** data : {&weapons, &quickItems, &armor, &spells}) {
        AlignedPtrArray& list = ItemList(*data);
        list.data = nullptr;
        list.growBy = 0;
        list.capacity = 0;
        list.size = 0;
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
EI_REPLACE_METHOD(0x0065F5F0, PartyUnitRecord::Construct);

void PartyUnitRecord::Destruct() {
    ClearItems();
    SetDefaults(*this);
    fightAtWill = 1;
    FreeStorage(ItemList(spells));
    FreeStorage(ItemList(armor));
    FreeStorage(ItemList(quickItems));
    FreeStorage(ItemList(weapons));
    for (int i = 6; i >= 0; --i) texts[i].~CString();
}
EI_REPLACE_METHOD(0x0065F740, PartyUnitRecord::Destruct);

void PartyUnitRecord::ClearItems() {
    ReleaseAll(ItemList(weapons));
    ReleaseAll(ItemList(quickItems));
    ReleaseAll(ItemList(armor));
    ReleaseAll(ItemList(spells));
    ItemList(weapons).SetSize(0, -1);
    ItemList(quickItems).SetSize(0, -1);
    ItemList(armor).SetSize(0, -1);
    ItemList(spells).SetSize(0, -1);
}
EI_REPLACE_METHOD(0x0065F860, PartyUnitRecord::ClearItems);

// The record lists' own copies of AlignedPtrArray::SetSize (00519D70; the same code): the record
// load 0065F9B0 and 006621F0 call them.
EI_REPLACE_METHOD(0x00663960, AlignedPtrArray::SetSize);
EI_REPLACE_METHOD(0x00663AD0, AlignedPtrArray::SetSize);
EI_REPLACE_METHOD(0x00663C00, AlignedPtrArray::SetSize);
EI_REPLACE_METHOD(0x00663D30, AlignedPtrArray::SetSize);

// --- The record array ---------------------------------------------------------------------------

void __stdcall ConstructPartyUnitRecords(PartyUnitRecord* records, int count) {
    std::memset(records, 0, static_cast<std::size_t>(count) * sizeof(PartyUnitRecord));
    for (PartyUnitRecord* record = records; count != 0; --count, ++record)
        if (record != nullptr) record->Construct();
}
EI_REPLACE(0x00664880, ConstructPartyUnitRecords);

void __stdcall DestructPartyUnitRecords(PartyUnitRecord* records, int count) {
    for (; count != 0; --count, ++records) records->Destruct();
}
EI_REPLACE(0x00664850, DestructPartyUnitRecords);

void PartyUnitRecordElements::Destruct(PartyUnitRecord* records, int count) { DestructPartyUnitRecords(records, count); }

PartyUnitRecord* PartyUnitRecord::DeletingDestruct(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x00664AC0, PartyUnitRecord::DeletingDestruct);

void __fastcall DestructPartyMemberArray(PartyMemberArray* array) {
    array->vtable = ei::GamePointer<void>(kPartyMemberArrayVtable);
    array->FreeData();
}
EI_REPLACE(0x00664160, DestructPartyMemberArray);

PartyMemberArray* __fastcall DeletePartyMemberArray(PartyMemberArray* array, void*, unsigned flags) {
    DestructPartyMemberArray(array);
    if (flags & 1) gamecrt::operator_delete(array);
    return array;
}
EI_REPLACE(0x00664470, DeletePartyMemberArray);

// 00663FA0: CArray<PartyUnitRecord>::SetSize.
EI_REPLACE_METHOD(0x00663FA0, PartyMemberArray::SetSize);
// 006641D0 (A-H17): CArray<PartyUnitRecord>::Serialize, slot 2 of the array's vtable 00742FF4; no
// code calls it (the parties go through CStream, party_save.cpp).
EI_REPLACE_METHOD(0x006641D0, PartyMemberArray::Serialize);

// --- The managers and constructors a party makes ------------------------------------------------

CItemConstructor* CItemConstructor::Construct() {
    ClearFields();
    ei::SetVtable(this, kVtableAddress);
    material_ = nullptr;
    prototype_ = nullptr;
    item_ = nullptr;
    spell_ = nullptr;
    manager_ = nullptr;
    return this;
}
EI_REPLACE_METHOD(0x006A5AB0, CItemConstructor::Construct);

CSpellConstructor* CSpellConstructor::Construct() {
    ClearFields();
    container_ = nullptr;
    ei::SetVtable(this, kVtableAddress);
    // (The original then releases +0x20, which it has just cleared.)
    manager_ = nullptr;
    return this;
}
EI_REPLACE_METHOD(0x00683FF0, CSpellConstructor::Construct);

CSpellManager* CSpellManager::Construct() {
    CResourceManager::Construct();
    sequences_.InitBuckets(100);
    maxSequenceKey_ = 0;
    ei::SetVtable(this, kVtableAddress);
    return this;
}
EI_REPLACE_METHOD(0x00688840, CSpellManager::Construct);

// --- CParty -------------------------------------------------------------------------------------

namespace {

// A counted object the party owns: its count dropped and the object deleted (slot 0), whatever
// the count, and the pointer cleared.
template <typename T>
void Drop(T*& object) {
    if (object == nullptr) return;
    --object->refCount_;
    delete object;
    object = nullptr;
}

void ReleaseItems(const AlignedPtrArray& items) {
    for (int i = 0; i < items.size; ++i) static_cast<CEngineObject*>(items.data[i])->Release();
}

} // namespace

CParty* CParty::Construct() {
    ClearFields();
    ei::SetVtable(this, kBaseVtableAddress);
    members_.vtable = ei::GamePointer<void>(kPartyMemberArrayVtable);
    members_.data = nullptr;
    members_.growBy = 0;
    members_.maxSize = 0;
    members_.size = 0;
    items_.data = nullptr;
    items_.growBy = 0;
    items_.capacity = 0;
    items_.size = 0;
    variables_.InitZeroedBuckets(100);
    new (&zone_) mfc::CString();
    itemManager_ = nullptr;
    spellManager_ = nullptr;
    itemConstructor_ = nullptr;
    spellConstructor_ = nullptr;
    ei::SetVtable(this, kVtableAddress);
    return this;
}
EI_REPLACE_METHOD(0x00661050, CParty::Construct);

void CParty::DestructBody() {
    ei::SetVtable(this, kVtableAddress);
    Clear(1, 1);
    DestroyParty();
    zone_.~CString();
    variables_.Destroy();
    if (items_.data != nullptr) gamecrt::operator_delete(items_.data[-1]);
    members_.vtable = ei::GamePointer<void>(kPartyMemberArrayVtable);
    if (members_.data != nullptr) {
        for (int i = 0; i != members_.size; ++i) members_.data[i].Destruct();
        gamecrt::operator_delete(members_.data);
    }
    DestructBase();
}
EI_REPLACE_METHOD(0x006611D0, CParty::DestructBody);

CParty* CParty::DeletingDestruct(unsigned flags) {
    DestructBody();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x00661190, CParty::DeletingDestruct);

BOOL CParty::CreateParty() {
    // The base class's slot 2 (0055E930) only returns 1.
    void* memory = gamecrt::operator_new(CItemManager::kSize);
    if (memory != nullptr) {
        static_cast<CResourceManager*>(memory)->Construct();
        ei::SetVtable(memory, CItemManager::kVtableAddress);
    }
    itemManager_ = static_cast<CResourceManager*>(memory);
    ++itemManager_->refCount_;   // not tested for null, as in the original
    itemManager_->Init(this);

    memory = gamecrt::operator_new(sizeof(CSpellManager));
    spellManager_ = memory != nullptr ? static_cast<CSpellManager*>(memory)->Construct() : nullptr;
    ++spellManager_->refCount_;
    spellManager_->Init(this);

    memory = gamecrt::operator_new(sizeof(CItemConstructor));
    itemConstructor_ = memory != nullptr ? static_cast<CItemConstructor*>(memory)->Construct() : nullptr;
    ++itemConstructor_->refCount_;
    itemConstructor_->Create();

    memory = gamecrt::operator_new(sizeof(CSpellConstructor));
    spellConstructor_ = memory != nullptr ? static_cast<CSpellConstructor*>(memory)->Construct() : nullptr;
    ++spellConstructor_->refCount_;
    spellConstructor_->Create();

    Clear(1, 0);
    return 1;
}
EI_REPLACE_METHOD(0x006614F0, CParty::CreateParty);

void CParty::DestroyParty() {
    members_.SetSize(0, -1);
    ReleaseItems(items_);
    items_.SetSize(0, -1);
    Drop(itemConstructor_);
    Drop(spellConstructor_);
    Drop(itemManager_);
    Drop(spellManager_);
}
EI_REPLACE_METHOD(0x00661640, CParty::DestroyParty);

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
EI_REPLACE_METHOD(0x00661700, CParty::Clear);
