// Item construction: CItemConstructor (vtable 00744020) and the CItem methods it drives.
//
// The item manager's CreateItem paths run BeginCreateItem (the manager creates the empty item
// object), SetPrototype / SetMaterial / SetSpellObject, then EndCreateItem, whose Build fills the
// item from a temporary database record: the prototype's values combined with the material's.
#include "item.h"

#include "console.h"
#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "lib/mfc.h"

#include <cstring>
#include <new>

using namespace itemdb;

namespace {

constexpr int kSlotRelease = 4;
constexpr int kSlotInitFromRecord = 13;          // CItem slot 13 (+0x34)
constexpr int kManagerSlotCreateObject = 23;     // CBaseManager slot 23 (+0x5C): (kind, key)
constexpr int kContextSlotSpellManager = 22;     // the manager context's slot 22 (+0x58)

// Item class ids (GetClassId).
constexpr int kClassWeapon = 0x11;
constexpr int kClassArmor = 0x12;
constexpr int kClassQuick = 0x13;
constexpr int kClassQuest = 0x14;    // table 007C8758 (item_db.h calls it Loot())
constexpr int kClassLoot = 0x15;     // table 007C8778 (item_db.h calls it QuestItems())
constexpr int kClassSpell = 0x16;    // spell container; quick item table

constexpr int kCategoryWand = 8;     // quick item category whose spell comes from the prototype
constexpr std::uint32_t kFlagConstructing = 8;

// "CItemConstructor::BeginCreateItem.\n", passed by its game address.
const char* const kCantCreateItem = reinterpret_cast<const char*>(0x00796638);

// The charge rate every item starts with (0x3A83126F).
constexpr std::uint32_t kDefaultChargeRate = 0x3A83126F;

// --- The temporary database record ---------------------------------------------------------------
//
// The record classes (vtables 00744054..00744068) start with: +0x00 vtable, +0x04 / +0x08 / +0x10
// CStrings, +0x20 and +0x50 CStringArrays, +0x68 a byte flag set to 1. Build creates one of the
// item's kind, fills only the fields the item's slot 13 reads, and destroys it again.

constexpr std::uintptr_t kRecordVtable = 0x00744060;
constexpr std::uintptr_t kWeaponRecordVtable = 0x00744068;
constexpr std::uintptr_t kArmorRecordVtable = 0x00744064;
constexpr std::uintptr_t kQuickRecordVtable = 0x0074405C;
constexpr std::uintptr_t kQuestRecordVtable = 0x00744058;
constexpr std::uintptr_t kLootRecordVtable = 0x00744054;

// Writes a field of a record that may be null (the original then faults writing near 0).
template <typename T> void Put(char* record, int offset, T value) {
    *reinterpret_cast<volatile T*>(reinterpret_cast<std::uintptr_t>(record) + offset) = value;
}

// A dword copied as a dword (mov), so float bit patterns pass unchanged.
void CopyDword(char* record, int offset, const char* source, int sourceOffset) {
    Put<std::uint32_t>(record, offset, Field<std::uint32_t>(source, sourceOffset));
}

float F(const char* record, int offset) { return Field<float>(record, offset); }

// A float copied with FLD / FSTP (a signalling NaN arrives quieted, as in the original).
void FpuCopy(void* destination, const void* source) {
    __asm {
        mov eax, source
        mov ecx, destination
        fld dword ptr [eax]
        fstp dword ptr [ecx]
    }
}

// afxEmptyString's data pointer (what an empty CString points at).
char* EmptyStringData() { return *reinterpret_cast<char* const*>(0x00796E48); }

void SetVtable(char* record, std::uintptr_t vtable) { std::memcpy(record, &vtable, sizeof(vtable)); }

// The base record constructor's work (inline in Build; 006A63B0 as a function).
void ConstructRecordBase(char* record) {
    Put(record, 0x04, EmptyStringData());
    Put(record, 0x08, EmptyStringData());
    Put(record, 0x10, EmptyStringData());
    new (record + 0x20) console::CStringArray;
    new (record + 0x50) console::CStringArray;
    Put<std::uint8_t>(record, 0x68, 1);
    SetVtable(record, kRecordVtable);
}

// Allocates and constructs the temporary record for an item of class `classId`; null for other
// classes (and when the allocation fails).
char* NewRecord(int classId) {
    char* record = nullptr;
    switch (classId) {
    case kClassWeapon:
        record = static_cast<char*>(gamecrt::operator_new(kWeaponSize));
        if (!record) return nullptr;
        ConstructRecordBase(record);
        SetVtable(record, kWeaponRecordVtable);
        std::memset(record + 0x78, 0, 0x83 - 0x78);
        break;
    case kClassArmor:
        record = static_cast<char*>(gamecrt::operator_new(kArmorSize));
        if (!record) return nullptr;
        ConstructRecordBase(record);
        SetVtable(record, kArmorRecordVtable);
        break;
    case kClassQuick:
    case kClassSpell:
        record = static_cast<char*>(gamecrt::operator_new(kQuickItemSize));
        if (!record) return nullptr;
        ConstructRecordBase(record);
        Put(record, 0x7C, EmptyStringData());   // a CString the record's destructor would free
        SetVtable(record, kQuickRecordVtable);
        std::memset(record + 0x80, 0, 0x8B - 0x80);
        break;
    case kClassQuest:
        record = static_cast<char*>(gamecrt::operator_new(kLootSize));
        if (!record) return nullptr;
        ConstructRecordBase(record);
        std::memset(record + 0x70, 0, 12);          // an empty STLport container (0055A3E0)
        SetVtable(record, kQuestRecordVtable);
        break;
    case kClassLoot:
        record = static_cast<char*>(gamecrt::operator_new(kQuestItemSize));
        if (!record) return nullptr;
        ConstructRecordBase(record);
        SetVtable(record, kLootRecordVtable);
        break;
    default:
        break;
    }
    return record;
}

// Destroys what every record has (the extra members of the quick, quest and loot records are
// not destroyed: the original leaks them, though they hold nothing here) and frees it.
void DeleteRecord(char* record) {
    reinterpret_cast<console::CStringArray*>(record + 0x50)->~CStringArray();
    reinterpret_cast<console::CStringArray*>(record + 0x20)->~CStringArray();
    reinterpret_cast<mfc::CString*>(record + 0x10)->~CString();
    reinterpret_cast<mfc::CString*>(record + 0x08)->~CString();
    reinterpret_cast<mfc::CString*>(record + 0x04)->~CString();
    gamecrt::operator_delete(record);
}

// Index of `record` in `table` (the pointer difference divided by the stride, signed).
std::uint32_t IndexIn(const RecordTable& table, const char* record, int stride) {
    return static_cast<std::uint32_t>(static_cast<int>(record - table.begin) / stride);
}

// Called through their game addresses so the verify wrappers see the nested calls.
__declspec(naked) void __fastcall CallItemSetSpell(CItem*, void*, CSpell*) EI_ORIGINAL(0x006A0520)
__declspec(naked) void __fastcall CallItemAttachSpell(CItem*, void*, CSpell*) EI_ORIGINAL(0x006A04D0)
__declspec(naked) BOOL __fastcall CallBuild(CItemConstructor*) EI_ORIGINAL(0x006A5D60)
// 00689030 CSpellManager::CreateSpellFromText (thiscall, one argument).
__declspec(naked) CSpell* __fastcall CallCreateSpellFromText(void*, void*, const char*) EI_ORIGINAL(0x00689030)

#pragma warning(disable : 4740) // naked forwarders

} // namespace

// --- CItem ---------------------------------------------------------------------------------------

void CItem::InitFromRecord(const char* record) {
    // +0x48 and +0x40 pass through the FPU in the original (FLD, FST, FSTP), which quiets a
    // signalling NaN; the other fields are copied as dwords.
    FpuCopy(&unknown_24, record + 0x48);
    FpuCopy(&unknown_20, record + 0x48);
    std::memcpy(&chargeRate_, &kDefaultChargeRate, 4);
    FpuCopy(&chargeMax_, record + 0x40);
    FpuCopy(&charge_, record + 0x40);
    unknown_34 = Field<std::uint32_t>(record, 0x38);
    std::memcpy(&price_, record + 0x34, 4);
    unknown_3C = Field<std::uint32_t>(record, 0x3C);
    unknown_40 = Field<std::uint32_t>(record, 0x44);
}
EI_REPLACE_METHOD(0x006A0490, CItem::InitFromRecord);

void CItem::AttachSpell(CSpell* spell) {
    CEngineObject* previous = static_cast<CEngineObject*>(spell_);
    if (previous) price_ = price_ - F(reinterpret_cast<const char*>(previous), 0x48);
    spell_ = spell;
    if (spell) ++reinterpret_cast<CEngineObject*>(spell)->refCount_;
    if (previous) ei::VCall<void>(previous, kSlotRelease);
    if (spell_) price_ = F(static_cast<const char*>(spell_), 0x48) + price_;
}
EI_REPLACE_METHOD(0x006A04D0, CItem::AttachSpell);

void CItem::SetSpell(CSpell* spell) {
    CEngineObject* previous = static_cast<CEngineObject*>(spell_);
    spell_ = spell;
    if (spell) ++reinterpret_cast<CEngineObject*>(spell)->refCount_;
    if (previous) ei::VCall<void>(previous, kSlotRelease);
}
EI_REPLACE_METHOD(0x006A0520, CItem::SetSpell);

// --- CItemConstructor ----------------------------------------------------------------------------

BOOL CItemConstructor::Reset() {
    Detach();
    return 1;
}
EI_REPLACE_METHOD(0x006A5B90, CItemConstructor::Reset);

void CItemConstructor::Detach() {
    if (manager_) --manager_->refCount_;
    manager_ = nullptr;
    if (item_) ei::VCall<void>(item_, kSlotRelease);
    item_ = nullptr;
    material_ = nullptr;
    prototype_ = nullptr;
    spell_ = nullptr;
}
EI_REPLACE_METHOD(0x006A5BD0, CItemConstructor::Detach);

void CItemConstructor::DestructBody() {
    std::uintptr_t vtable = 0x00744020;
    std::memcpy(this, &vtable, sizeof(vtable));
    Detach();
    DestructBase();
}
EI_REPLACE_METHOD(0x006A5B20, CItemConstructor::DestructBody);

void CItemConstructor::SetManager(CEngineObject* manager) {
    if (manager_) --manager_->refCount_;
    manager_ = manager;
    if (manager) ++manager->refCount_;
}
EI_REPLACE_METHOD(0x006A5C00, CItemConstructor::SetManager);

void CItemConstructor::SetItem(CItem* item) {
    if (item_) ei::VCall<void>(item_, kSlotRelease);
    item_ = item;
    if (item) ++item->refCount_;
    material_ = nullptr;
    prototype_ = nullptr;
}
EI_REPLACE_METHOD(0x006A5C20, CItemConstructor::SetItem);

BOOL CItemConstructor::BeginCreateItem(int kind, int key) {
    CItem* item = ei::VCall<CItem*>(manager_, kManagerSlotCreateObject, kind, key);
    if (!item) return game::FatalError(kCantCreateItem);
    if (item_) ei::VCall<void>(item_, kSlotRelease);
    item_ = item;
    ++item->refCount_;
    material_ = nullptr;
    prototype_ = nullptr;
    count_ = 1;
    flags_ |= kFlagConstructing;
    return 1;
}
EI_REPLACE_METHOD(0x006A5C50, CItemConstructor::BeginCreateItem);

BOOL CItemConstructor::SetMaterial(char* material) {
    material_ = material;
    return 1;
}
EI_REPLACE_METHOD(0x006A5CC0, CItemConstructor::SetMaterial);

BOOL CItemConstructor::SetPrototype(char* prototype) {
    prototype_ = prototype;
    return 1;
}
EI_REPLACE_METHOD(0x006A5CD0, CItemConstructor::SetPrototype);

BOOL CItemConstructor::SetSpellObject(CSpell* spell) {
    spell_ = spell;
    return 1;
}
EI_REPLACE_METHOD(0x006A5CE0, CItemConstructor::SetSpellObject);

CItem* CItemConstructor::EndCreateItem() {
    if (!CallBuild(this)) {
        ei::VCall<void>(item_, kSlotRelease);
        item_ = nullptr;
        return nullptr;
    }
    flags_ &= ~kFlagConstructing;
    CItem* item = item_;
    --item->refCount_;
    item_ = nullptr;
    spell_ = nullptr;
    count_ = 0;
    return item;
}
EI_REPLACE_METHOD(0x006A5CF0, CItemConstructor::EndCreateItem);

BOOL CItemConstructor::Cancel() {
    if (item_) ei::VCall<void>(item_, kSlotRelease);
    item_ = nullptr;
    spell_ = nullptr;
    material_ = nullptr;
    prototype_ = nullptr;
    return 1;
}
EI_REPLACE_METHOD(0x006A5D30, CItemConstructor::Cancel);

namespace {

// Build's common part: price, values and charge of the record, from the prototype alone or
// scaled by the material.
void FillCommon(char* record, const char* prototype, const char* material) {
    if (!material) {
        CopyDword(record, 0x34, prototype, 0x34);
        CopyDword(record, 0x38, prototype, 0x38);
        CopyDword(record, 0x3C, prototype, 0x3C);
        CopyDword(record, 0x40, prototype, 0x40);
        CopyDword(record, 0x48, prototype, 0x48);
        CopyDword(record, 0x44, prototype, 0x44);
        CopyDword(record, 0x4C, prototype, 0x4C);
        return;
    }
    // Every operand is read before the arithmetic so that no partial result leaves the FPU (the
    // original keeps them in ST0). Price: the prototype's +0x4C (an unsigned count, FILD qword)
    // times the material's price, plus the prototype's price.
    std::int64_t units = Field<std::uint32_t>(prototype, 0x4C);
    float materialPrice = F(material, 0x10), price = F(prototype, 0x34);
    Put<float>(record, 0x34, static_cast<float>(static_cast<double>(units) * materialPrice + price));
    float value38 = F(prototype, 0x38), factor38 = F(material, 0x14);
    Put<float>(record, 0x38, value38 * factor38);
    CopyDword(record, 0x3C, prototype, 0x3C);
    float charge = F(prototype, 0x40), extraCharge = F(material, 0x18);
    Put<float>(record, 0x40, charge + extraCharge);
    float value48 = F(prototype, 0x48), factor48 = F(material, 0x20);
    Put<float>(record, 0x48, value48 * factor48);
    Put<int>(record, 0x44, Field<int>(prototype, 0x44) + Field<int>(material, 0x1C));
    CopyDword(record, 0x4C, prototype, 0x4C);
}

// Build's per-class part; returns the prototype's index in its table.
std::uint32_t FillClass(int classId, char* record, const char* prototype, const char* material) {
    switch (classId) {
    case kClassWeapon: {
        CopyDword(record, 0x8C, prototype, 0x8C);
        float damage = F(prototype, 0x90), factor = F(material, 0x28);
        Put<float>(record, 0x90, damage * factor);
        float spread = F(prototype, 0x94), factor2 = F(material, 0x28);
        Put<float>(record, 0x94, spread * factor2);
        return IndexIn(Weapons(), prototype, kWeaponSize);
    }
    case kClassArmor:
        Put<std::uint32_t>(record, 0x8C, Field<std::uint32_t>(prototype, 0x8C) | Field<std::uint32_t>(material, 0x48));
        Put<std::uint32_t>(record, 0xB0, Field<std::uint32_t>(prototype, 0xB0) | Field<std::uint32_t>(material, 0x48));
        // Seven protection values in two groups, each scaled by the material's factor for its
        // type and by a group factor of the prototype (+0x6C and +0x90).
        for (int k = 0; k < 7; ++k) {
            float a = F(prototype, 0x70 + 4 * k), b = F(material, 0x2C + 4 * k), c = F(prototype, 0x6C);
            Put<float>(record, 0x70 + 4 * k, a * b * c);
            float d = F(prototype, 0x94 + 4 * k), e = F(material, 0x2C + 4 * k), f = F(prototype, 0x90);
            Put<float>(record, 0x94 + 4 * k, d * e * f);
        }
        return IndexIn(Armors(), prototype, kArmorSize);
    case kClassQuick:
    case kClassSpell:
        CopyDword(record, 0x70, prototype, 0x70);
        return IndexIn(QuickItems(), prototype, kQuickItemSize);
    case kClassQuest:
        CopyDword(record, 0x6C, prototype, 0x6C);
        return IndexIn(Loot(), prototype, kLootSize);
    case kClassLoot:
        CopyDword(record, 0x6C, prototype, 0x6C);
        CopyDword(record, 0x70, prototype, 0x70);
        // Kind 1 (a material) takes the material's index when one is given.
        if (Field<int>(prototype, 0x6C) == 1 && material)
            CopyDword(record, 0x74, material, 0x0C);
        else
            CopyDword(record, 0x74, prototype, 0x74);
        return IndexIn(QuestItems(), prototype, kQuestItemSize);
    default:
        return 0;   // not reached: the record of an unknown class is null and faulted above
    }
}

} // namespace

BOOL CItemConstructor::Build() {
    int classId = item_->GetClassId();
    if ((classId == kClassWeapon || item_->GetClassId() == kClassArmor) && !material_) return 0;

    char* record = NewRecord(item_->GetClassId());
    FillCommon(record, prototype_, material_);
    std::uint32_t index = FillClass(item_->GetClassId(), record, prototype_, material_);

    ei::VCall<void>(item_, kSlotInitFromRecord, record);
    if (item_->GetClassId() == kClassQuick && Field<int>(prototype_, field::kItemCategory) == kCategoryWand) {
        void* context = *reinterpret_cast<void* const*>(reinterpret_cast<const char*>(manager_) + 0xAC);
        void* spellManager = ei::VCall<void*>(context, kContextSlotSpellManager);
        CSpell* spell = CallCreateSpellFromText(spellManager, nullptr, Field<const char*>(prototype_, 0x7C));
        spell_ = spell;
        CallItemSetSpell(item_, nullptr, spell);
    } else if (spell_) {
        CallItemAttachSpell(item_, nullptr, spell_);
    }
    if (record) DeleteRecord(record);

    if (material_)
        item_->unknown_18 = (Field<std::uint32_t>(material_, 0x0C) & 0xFFFF) | index << 16;
    else
        item_->unknown_18 = index << 16;
    item_->unknown_48 = static_cast<std::uint32_t>(count_);
    return 1;
}
EI_REPLACE_METHOD(0x006A5D60, CItemConstructor::Build);
