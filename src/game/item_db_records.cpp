// The item databases' records as values: the head's assignment (006A8F20) and each record kind's
// copy constructor as STLport's uninitialized_copy / uninitialized_fill_n inline it (the vectors'
// growth while the databases load). See item_db.h and docs/notes/items_spells.md ("Databases").
#include "item_db.h"

#include "console.h"

#include "ei/hook.h"
#include "originals_library.h"

#include <cstddef>
#include <cstring>
#include <new>

using namespace itemdb;

namespace {

// Bytes [from, to) of `source` copied into `target` (the original's dword, word and byte moves).
template <typename R>
void CopyBytes(R* target, const R* source, std::size_t from, std::size_t to) {
    std::memcpy(reinterpret_cast<char*>(target) + from, reinterpret_cast<const char*>(source) + from, to - from);
}

} // namespace

// The records' slot 0 (A-H25): the item kind each record makes.
int __fastcall itemdb::WeaponRecordKind(ItemRecordHead*) { return 0x3004; }
int __fastcall itemdb::ArmorRecordKind(ItemRecordHead*) { return 0x3005; }
int __fastcall itemdb::QuickItemRecordKind(ItemRecordHead*) { return 0x3006; }
int __fastcall itemdb::Record7CKind(ItemRecordHead*) { return 0x3009; }
int __fastcall itemdb::Record78Kind(ItemRecordHead*) { return 0x3007; }
EI_REPLACE(0x006A6430, itemdb::WeaponRecordKind);
EI_REPLACE(0x006A6440, itemdb::ArmorRecordKind);
EI_REPLACE(0x006A6450, itemdb::QuickItemRecordKind);
EI_REPLACE(0x006A6460, itemdb::Record7CKind);
EI_REPLACE(0x006A6470, itemdb::Record78Kind);

ItemRecordHead* ItemRecordHead::Assign(const ItemRecordHead* source) {
    text04 = source->text04;
    text08 = source->text08;
    type = source->type;
    text10 = source->text10;
    CopyBytes(this, source, offsetof(ItemRecordHead, unknown_14), offsetof(ItemRecordHead, list20));
    CopyBytes(this, source, offsetof(ItemRecordHead, price), offsetof(ItemRecordHead, list50));
    unknown_68 = source->unknown_68;
    StringArrayCopyOriginal(&list20, nullptr, &source->list20);
    return this;
}
EI_REPLACE_METHOD(0x006A8F20, ItemRecordHead::Assign);

void ItemRecordHead::ConstructHead(std::uintptr_t vtableAddress) {
    new (&text04) mfc::CString();
    new (&text08) mfc::CString();
    new (&text10) mfc::CString();
    new (&list20) console::CStringArray();
    new (&list50) console::CStringArray();
    unknown_68 = 1;
    vtable = reinterpret_cast<void*>(vtableAddress);
}

void WeaponRecord::CopyOwn(const WeaponRecord* source) {
    CopyBytes(this, source, offsetof(WeaponRecord, category), 0x83);
    CopyBytes(this, source, offsetof(WeaponRecord, value8C), sizeof(WeaponRecord));
}

WeaponRecord* WeaponRecord::AssignWeapon(const WeaponRecord* source) {
    Assign(source);
    CopyOwn(source);
    return this;
}
EI_REPLACE_METHOD(0x006A8FB0, WeaponRecord::AssignWeapon);

void ArmorRecord::CopyOwn(const ArmorRecord* source) {
    CopyBytes(this, source, offsetof(ArmorRecord, category), 0xB4);
}

void QuickItemRecord::CopyOwn(const QuickItemRecord* source) {
    value70 = source->value70;
    CopyBytes(this, source, offsetof(QuickItemRecord, attributeBonus), 0x8B);
}

void QuestItemRecord::CopyOwn(const QuestItemRecord* source) {
    category = source->category;
}

void LootItemRecord::CopyOwn(const LootItemRecord* source) {
    CopyBytes(this, source, offsetof(LootItemRecord, category), sizeof(LootItemRecord));
}

namespace itemdb {

// The kinds' extra members, constructed between the head and the Assign.
template <typename R>
void ConstructExtra(R*) {}
template <>
void ConstructExtra(QuickItemRecord* record) { new (&record->text7C) mfc::CString(); }
template <>
void ConstructExtra(QuestItemRecord* record) {
    record->zonesBegin = nullptr;
    record->zonesEnd = nullptr;
    record->zonesCapacity = nullptr;
}

// uninitialized_copy(first, last, dest) and uninitialized_fill_n(dest, n, x) (__fastcall, RET 8:
// the second stack argument is STLport's unused type tag). Null destinations are skipped, as in
// the original's placement new. Both return the end of the destination.
template <typename R>
R* __fastcall CopyItemRecords(const R* first, const R* last, R* dest, int /*tag*/) {
    for (; first != last; ++first, ++dest) {
        if (!dest) continue;
        dest->ConstructHead(R::kVtableAddress);
        ConstructExtra(dest);
        dest->Assign(first);
        dest->CopyOwn(first);
    }
    return dest;
}

template <typename R>
R* __fastcall FillItemRecords(R* dest, int count, const R* value, int /*tag*/) {
    for (; count != 0; --count, ++dest) {
        if (!dest) continue;
        dest->ConstructHead(R::kVtableAddress);
        ConstructExtra(dest);
        dest->Assign(value);
        dest->CopyOwn(value);
    }
    return dest;
}

template WeaponRecord* __fastcall CopyItemRecords(const WeaponRecord*, const WeaponRecord*, WeaponRecord*, int);
template WeaponRecord* __fastcall FillItemRecords(WeaponRecord*, int, const WeaponRecord*, int);
template ArmorRecord* __fastcall CopyItemRecords(const ArmorRecord*, const ArmorRecord*, ArmorRecord*, int);
template ArmorRecord* __fastcall FillItemRecords(ArmorRecord*, int, const ArmorRecord*, int);
template QuickItemRecord* __fastcall CopyItemRecords(const QuickItemRecord*, const QuickItemRecord*, QuickItemRecord*, int);
template QuickItemRecord* __fastcall FillItemRecords(QuickItemRecord*, int, const QuickItemRecord*, int);
template QuestItemRecord* __fastcall CopyItemRecords(const QuestItemRecord*, const QuestItemRecord*, QuestItemRecord*, int);
template QuestItemRecord* __fastcall FillItemRecords(QuestItemRecord*, int, const QuestItemRecord*, int);
template LootItemRecord* __fastcall CopyItemRecords(const LootItemRecord*, const LootItemRecord*, LootItemRecord*, int);
template LootItemRecord* __fastcall FillItemRecords(LootItemRecord*, int, const LootItemRecord*, int);

} // namespace itemdb

EI_REPLACE(0x006AB9C0, CopyItemRecords<WeaponRecord>);
EI_REPLACE(0x006ABB50, FillItemRecords<WeaponRecord>);
EI_REPLACE(0x006ABCD0, CopyItemRecords<ArmorRecord>);
EI_REPLACE(0x006ABE20, FillItemRecords<ArmorRecord>);
EI_REPLACE(0x006ABF60, CopyItemRecords<QuickItemRecord>);
EI_REPLACE(0x006AC0D0, FillItemRecords<QuickItemRecord>);
EI_REPLACE(0x006AC240, CopyItemRecords<QuestItemRecord>);
EI_REPLACE(0x006AC3B0, FillItemRecords<QuestItemRecord>);
EI_REPLACE(0x006AC520, CopyItemRecords<LootItemRecord>);
EI_REPLACE(0x006AC640, FillItemRecords<LootItemRecord>);
