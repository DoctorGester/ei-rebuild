// Parties and their unit records: the records' constructor and destructor bodies and item clear,
// the record array (MFC CArray::SetSize and ConstructElements), the party's constructor,
// destructor, Create / Destroy / Clear, and the constructors of the managers a party makes. The
// record's item lists are aligned pointer arrays; four more copies of their SetSize (00519D70) are
// registered here. See party.h and docs/notes/items_spells.md ("Party").
#include "party.h"

#include "class_names_640.h"
#include "engine_object_class.h"
#include "world_class_slots.h"

#include "game_strings.h"
#include "item.h"
#include "item_class.h"
#include "spell.h"
#include "spell_class.h"
#include "unit.h"

#include "lib/crt.h"
#include "lib/mfc_slots.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "game_data.h"

#include <cstring>
#include <initializer_list>
#include <new>

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

// --- PartyUnitRecord ----------------------------------------------------------------------------

PartyUnitRecord* PartyUnitRecord::Construct() {
    for (mfc::CString& text : texts) new (&text) mfc::CString();
    for (CItem*** data : {&weapons, &quickItems, &armor, &spells}) {
        PartyItemArray& list = ItemList(*data);
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
EI_REWRITTEN(0x0065F740, PartyUnitRecord::Destruct);

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

PartyUnitRecord* PartyUnitRecord::DeletingDestruct(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REWRITTEN(0x00664AC0, PartyUnitRecord::DeletingDestruct);

namespace {
void __fastcall DestroyPartyMembers(PartyMembers* members) { members->Destruct(); }
// Slot 2 through the slot's archive type (mfc::impl's; the template's Serialize takes mfc.h's).
struct PartyMembersStore : PartyMembers {
    void Store(mfc::impl::CArchive* ar) { Serialize(reinterpret_cast<mfc::CArchive*>(ar)); }
};

// CArray<PartyUnitRecord>'s compiled table (C19; game.exe's 00742FF4): CObject's GetRuntimeClass, the destructor
// 00664160 (the compiler's deleting destructor in place of 00664470), Serialize 006641D0.
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
// 006641D0 (A-H17): CArray<PartyUnitRecord>::Serialize, slot 2 of the array's table (PartyMembers::Vtable()); no
// code calls it (the parties go through CStream, party_save.cpp).
EI_REWRITTEN(0x006641D0, PartyMemberArray::Serialize);

// --- The managers and constructors a party makes ------------------------------------------------

CItemConstructor* CItemConstructor::Construct() {
    ClearFields();
    ei::SetVtable(this, Vtable());
    material_ = nullptr;
    prototype_ = nullptr;
    item_ = nullptr;
    spell_ = nullptr;
    manager_ = nullptr;
    return this;
}
EI_REWRITTEN(0x006A5AB0, CItemConstructor::Construct);

CSpellConstructor* CSpellConstructor::Construct() {
    ClearFields();
    container_ = nullptr;
    ei::SetVtable(this, Vtable());
    // (The original then releases +0x20, which it has just cleared.)
    manager_ = nullptr;
    return this;
}
EI_REWRITTEN(0x00683FF0, CSpellConstructor::Construct);

CSpellManager::CSpellManager() {
    sequences_.InitBuckets(100);
    maxSequenceKey_ = 0;
}

CSpellManager* CSpellManager::Construct() { return new (this) CSpellManager(); }
EI_REWRITTEN(0x00688840, CSpellManager::Construct);

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

void ReleaseItems(const PartyItemArray& items) {
    for (int i = 0; i < items.size; ++i) items.data[i]->Release();
}

} // namespace

namespace {

// CParty's base's slots (game.exe's 0074306C): CEngineObject's deleting destructor (006611B0), Create 0055E930 (1),
// Destroy / ClassName / ClassId pure, and the manager context's slots 13-26 as CWorld's (world_class.h): every getter
// null (0065F4B0 ... 0065F500, 00661140 ... 00661180), 24 the party's copy of an item (0055E940), 25 / 26 null and 0
// (0055EB20 / 0055EB30).
template <typename Self, typename T>
class PartyContextClass : public EngineObjectClass<Self, T> {
public:
    int Create() override { return PartyBaseCreate(this->Object()); }   // 2: 0055E930

    virtual void* ThisWorld() { return nullptr; }                       // 13: 0065F4B0
    virtual void* TimedObjectManager() { return nullptr; }              // 14: 00661140
    virtual void* FigureManager() { return nullptr; }                   // 15: 0065F4C0
    virtual CItemConstructor* ItemConstructor() { return nullptr; }     // 16: 00661150
    virtual CSpellConstructor* SpellConstructor() { return nullptr; }   // 17: 00661160
    virtual CItemManager* ItemManager() { return nullptr; }             // 18: 00661170
    virtual void* MapObjectManager() { return nullptr; }                // 19: 0065F4D0
    virtual void* PlayerList() { return nullptr; }                      // 20: 0065F4E0
    virtual void* SpecificManager() { return nullptr; }                 // 21: 0065F4F0
    virtual CSpellManager* SpellManager() { return nullptr; }           // 22: 00661180
    virtual void* FigureLoader() { return nullptr; }                    // 23: 0065F500
    virtual CItem* CopyItem(CItem* item) { return Party()->CParty::MakeCopyOf(item); }   // 24: 0055E940
    virtual const char* TexturePath() { return nullptr; }               // 25: 0055EB20
    virtual int IsServer() { return 0; }                                // 26: 0055EB30

protected:
    CParty* Party() { return this->template As<CParty>(); }
};

class PartyBaseClass final : public PartyContextClass<PartyBaseClass, CEngineObject> {};

// The party's own (game.exe's 00743000): its destructor, Create / Destroy (006614F0 / 00661640), "CParty" / 9
// (0065F560 / 0065F570), 14 null (0065F530) and the item and spell getters (0065F540 / 0065F550 / 0065F520 /
// 0065F510).
class PartyClass final : public PartyContextClass<PartyClass, CParty> {
public:
    int Create() override { return Party()->CreateParty(); }             // 2: 006614F0
    void Destroy() override { Party()->DestroyParty(); }                 // 3: 00661640
    const char* ClassName() override { return PartyClassName(Object()); }   // 11: 0065F560
    int ClassId() override { return PartyClassId(Object()); }               // 12: 0065F570
    void* TimedObjectManager() override { return nullptr; }                       // 14: 0065F530
    CItemConstructor* ItemConstructor() override { return Party()->itemConstructor_; }     // 16: 0065F540
    CSpellConstructor* SpellConstructor() override { return Party()->spellConstructor_; }  // 17: 0065F550
    CItemManager* ItemManager() override { return Party()->itemManager_; }                 // 18: 0065F520
    CSpellManager* SpellManager() override { return Party()->spellManager_; }              // 22: 0065F510
};

} // namespace

void* CParty::Vtable() { return PartyClass::Vtable(); }
void* CParty::BaseVtable() { return PartyBaseClass::Vtable(); }

CParty* CParty::Construct() {
    ClearFields();
    ei::SetVtable(this, BaseVtable());
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
    new (&zone_) mfc::CString();
    itemManager_ = nullptr;
    spellManager_ = nullptr;
    itemConstructor_ = nullptr;
    spellConstructor_ = nullptr;
    ei::SetVtable(this, Vtable());
    return this;
}
EI_REWRITTEN(0x00661050, CParty::Construct);

void CParty::DestructBody() {
    ei::SetVtable(this, Vtable());
    Clear(1, 1);
    DestroyParty();
    zone_.~CString();
    variables_.Destroy();
    if (items_.data != nullptr) gamecrt::operator_delete(items_.data[-1]);
    members_.vtable = PartyMembers::Vtable();
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
EI_REWRITTEN(0x00661190, CParty::DeletingDestruct);

BOOL CParty::CreateParty() {
    // The base class's slot 2 (0055E930) only returns 1.
    CItemManager* items = gamecrt::operator_new_as<CItemManager>();
    itemManager_ = items != nullptr ? items->Construct() : nullptr;
    ++itemManager_->refCount_;   // not tested for null, as in the original
    itemManager_->Init(this);

    CSpellManager* spells = gamecrt::operator_new_as<CSpellManager>();
    spellManager_ = spells != nullptr ? spells->Construct() : nullptr;
    ++spellManager_->refCount_;
    spellManager_->Init(this);

    CItemConstructor* itemMaker = gamecrt::operator_new_as<CItemConstructor>();
    itemConstructor_ = itemMaker != nullptr ? itemMaker->Construct() : nullptr;
    ++itemConstructor_->refCount_;
    itemConstructor_->Create();

    CSpellConstructor* spellMaker = gamecrt::operator_new_as<CSpellConstructor>();
    spellConstructor_ = spellMaker != nullptr ? spellMaker->Construct() : nullptr;
    ++spellConstructor_->refCount_;
    spellConstructor_->Create();

    Clear(1, 0);
    return 1;
}
EI_REWRITTEN(0x006614F0, CParty::CreateParty);

void CParty::DestroyParty() {
    members_.SetSize(0, -1);
    ReleaseItems(items_);
    items_.SetSize(0, -1);
    Drop(itemConstructor_);
    Drop(spellConstructor_);
    Drop(itemManager_);
    Drop(spellManager_);
}
EI_REWRITTEN(0x00661640, CParty::DestroyParty);

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
