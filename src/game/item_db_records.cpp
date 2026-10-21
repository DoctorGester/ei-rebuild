// The item databases' records as values: the head's assignment (006A8F20) and each record kind's
// copy constructor as STLport's uninitialized_copy / uninitialized_fill_n inline it (the vectors'
// growth while the databases load). See item_db.h and docs/notes/items_spells.md ("Databases").
#include "item_db.h"

#include "console.h"
#include "game_data.h"

#include "ei/hook.h"
#include "originals_library.h"
#include "record_lifetime.h"

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
int __fastcall itemdb::QuestItemRecordKind(ItemRecordHead*) { return 0x3009; }
int __fastcall itemdb::LootItemRecordKind(ItemRecordHead*) { return 0x3007; }
EI_REPLACE(0x006A6430, itemdb::WeaponRecordKind);
EI_REPLACE(0x006A6440, itemdb::ArmorRecordKind);
EI_REPLACE(0x006A6450, itemdb::QuickItemRecordKind);
EI_REPLACE(0x006A6460, itemdb::QuestItemRecordKind);
EI_REPLACE(0x006A6470, itemdb::LootItemRecordKind);

ItemRecordHead* ItemRecordHead::Assign(const ItemRecordHead* source) {
    text04 = source->text04;
    text08 = source->text08;
    type = source->type;
    materialClass = source->materialClass;
    CopyBytes(this, source, offsetof(ItemRecordHead, field4), offsetof(ItemRecordHead, list20));
    CopyBytes(this, source, offsetof(ItemRecordHead, price), offsetof(ItemRecordHead, list50));
    field15 = source->field15;
    list20.Copy(source->list20);
    return this;
}
EI_REWRITTEN(0x006A8F20, ItemRecordHead::Assign);

void ItemRecordHead::ConstructHead(std::uintptr_t vtableAddress) {
    new (&text04) mfc::CString();
    new (&text08) mfc::CString();
    new (&materialClass) mfc::CString();
    new (&list20) console::CStringArray();
    new (&list50) console::CStringArray();
    field15 = 1;
    vtable = ei::GamePointer<void>(vtableAddress);
}

void WeaponRecord::CopyOwn(const WeaponRecord* source) {
    CopyBytes(this, source, offsetof(WeaponRecord, category), 0x83);
    CopyBytes(this, source, offsetof(WeaponRecord, reach), sizeof(WeaponRecord));
}

WeaponRecord* WeaponRecord::AssignWeapon(const WeaponRecord* source) {
    Assign(source);
    CopyOwn(source);
    return this;
}
EI_REPLACE_METHOD(0x006A8FB0, WeaponRecord::AssignWeapon);

ItemRecordHead* ItemRecordHead::ConstructBase() {
    ConstructHead(kBaseVtableAddress);
    return this;
}
EI_REPLACE_METHOD(0x006A63B0, ItemRecordHead::ConstructBase);

void ArmorRecord::CopyOwn(const ArmorRecord* source) {
    CopyBytes(this, source, offsetof(ArmorRecord, category), 0xB4);
}

// The other kinds' operator= (A-H25), as the weapon's.
ArmorRecord* ArmorRecord::AssignArmor(const ArmorRecord* source) {
    Assign(source);
    CopyOwn(source);
    return this;
}
EI_REPLACE_METHOD(0x006A9370, ArmorRecord::AssignArmor);

QuickItemRecord* QuickItemRecord::AssignQuick(const QuickItemRecord* source) {
    Assign(source);
    CopyOwn(source);
    return this;
}
EI_REPLACE_METHOD(0x006A9770, QuickItemRecord::AssignQuick);

QuestItemRecord* QuestItemRecord::AssignQuest(const QuestItemRecord* source) {
    Assign(source);
    CopyOwn(source);
    return this;
}
EI_REPLACE_METHOD(0x006AAE40, QuestItemRecord::AssignQuest);

LootItemRecord* LootItemRecord::AssignLoot(const LootItemRecord* source) {
    Assign(source);
    CopyOwn(source);
    return this;
}
EI_REPLACE_METHOD(0x006A9DA0, LootItemRecord::AssignLoot);

MaterialRecordHead* MaterialRecordHead::CopyConstruct(const MaterialRecordHead* source) {
    new (&key) mfc::CString(source->key);
    new (&soundName) mfc::CString(source->soundName);
    new (&name) mfc::CString(source->name);
    CopyBytes(this, source, offsetof(MaterialRecordHead, number), sizeof(MaterialRecordHead));
    return this;
}
EI_REWRITTEN(0x006A9E40, MaterialRecordHead::CopyConstruct);

void MaterialRecordHead::AssignMaterial(const MaterialRecordHead* source) {
    key = source->key;
    soundName = source->soundName;
    name = source->name;
    CopyBytes(this, source, offsetof(MaterialRecordHead, number), sizeof(MaterialRecordHead));
}

void MaterialRecordHead::ConstructDefault() {
    new (&key) mfc::CString();
    new (&soundName) mfc::CString();
    new (&name) mfc::CString();
}

// ---- S13: the records' destructors (the compiler's) ----------------------------------------------

void ItemRecordHead::DestructHead() {
    list50.Destruct();
    list20.Destruct();
    materialClass.~CString();
    text08.~CString();
    text04.~CString();
}
EI_REWRITTEN(0x006A8950, ItemRecordHead::DestructHead);   // weapons
EI_REPLACE_METHOD(0x006A89D0, ItemRecordHead::DestructHead);   // armours
EI_REPLACE_METHOD(0x006A8BD0, ItemRecordHead::DestructHead);   // loot

void QuickItemRecord::Destruct() {
    text7C.~CString();
    DestructHead();
}
EI_REWRITTEN(0x006A8A50, QuickItemRecord::Destruct);

void QuestItemRecord::Destruct() {
    for (mfc::CString* zone = zonesBegin; zone != zonesEnd; ++zone) zone->~CString();
    if (zonesBegin) stlp::deallocate(zonesBegin, static_cast<std::size_t>(zonesCapacity - zonesBegin) * sizeof(mfc::CString));
    DestructHead();
}
EI_REWRITTEN(0x006A8AE0, QuestItemRecord::Destruct);

void MaterialRecordHead::Destruct() {
    name.~CString();
    soundName.~CString();
    key.~CString();
}
EI_REWRITTEN(0x006A88F0, MaterialRecordHead::Destruct);

void SpellRecord::Destruct() {
    list54.FreeStorage();
    list48.FreeStorage();
    text08.~CString();
    code.~CString();
    name.~CString();
}
EI_REWRITTEN(0x00685AA0, SpellRecord::Destruct);

void SpellModifierRecord::Destruct() {
    text1C.~CString();
    name.~CString();
}
EI_REWRITTEN(0x00685B70, SpellModifierRecord::Destruct);

void SpellListRecord::Destruct() {
    text24.~string();
    records::DestroyVector(list18);
    records::DestroyVector(list0C);
    name.~string();
}
EI_REWRITTEN(0x00589860, SpellListRecord::Destruct);

// ---- S10a: the spell records as vector elements (record_vectors.h) -------------------------------

namespace {
using IdElement = stlp::PlainElement<int>;
} // namespace

void SpellRecord::CopyConstruct(const SpellRecord* source) {
    new (&name) mfc::CString(source->name);
    new (&code) mfc::CString(source->code);
    new (&text08) mfc::CString(source->text08);
    CopyBytes(this, source, offsetof(SpellRecord, price), offsetof(SpellRecord, list48));
    list48.CopyConstruct<IdElement>(source->list48);
    list54.CopyConstruct<IdElement>(source->list54);
    CopyBytes(this, source, offsetof(SpellRecord, icon), offsetof(SpellRecord, unused_7D));
    classMask = source->classMask;
}

void SpellRecord::AssignSpell(const SpellRecord* source) {
    name = source->name;
    code = source->code;
    text08 = source->text08;
    CopyBytes(this, source, offsetof(SpellRecord, price), offsetof(SpellRecord, list48));
    list48.AssignVector<IdElement>(source->list48);
    list54.AssignVector<IdElement>(source->list54);
    CopyBytes(this, source, offsetof(SpellRecord, icon), offsetof(SpellRecord, unused_7D));
    classMask = source->classMask;
}

void SpellRecord::ConstructDefault() {
    new (&name) mfc::CString();
    new (&code) mfc::CString();
    new (&text08) mfc::CString();
    list48.ConstructEmpty();
    list54.ConstructEmpty();
}

void SpellModifierRecord::CopyConstruct(const SpellModifierRecord* source) {
    new (&name) mfc::CString(source->name);
    CopyBytes(this, source, offsetof(SpellModifierRecord, code), offsetof(SpellModifierRecord, text1C));
    new (&text1C) mfc::CString(source->text1C);
    classMask = source->classMask;
}

void SpellModifierRecord::AssignModifier(const SpellModifierRecord* source) {
    name = source->name;
    CopyBytes(this, source, offsetof(SpellModifierRecord, code), offsetof(SpellModifierRecord, text1C));
    text1C = source->text1C;
    classMask = source->classMask;
}

void SpellModifierRecord::ConstructDefault() {
    new (&name) mfc::CString();
    new (&text1C) mfc::CString();
}

void SpellListRecord::CopyConstruct(const SpellListRecord* source) {
    new (&name) stlp::string(source->name);
    list0C.CopyConstruct<stlp::StringElement>(source->list0C);
    list18.CopyConstruct<stlp::StringElement>(source->list18);
    new (&text24) stlp::string(source->text24);
    value30 = source->value30;
}

void SpellListRecord::AssignList(const SpellListRecord* source) {
    name.AssignFrom(source->name);
    list0C.AssignVector<stlp::StringElement>(source->list0C);
    list18.AssignVector<stlp::StringElement>(source->list18);
    text24.AssignFrom(source->text24);
    value30 = source->value30;
}

void SpellListRecord::ConstructDefault() {
    stlp::StringElement::Default(&name);
    list0C.ConstructEmpty();
    list18.ConstructEmpty();
    stlp::StringElement::Default(&text24);
}

template <int Stride>
char* __fastcall itemdb::RecordTableAt(const RecordTable* table, void*, std::uint32_t index) {
    return table->RecordAt(index, Stride);
}
EI_REPLACE(0x006A5950, itemdb::RecordTableAt<kArmorSize>);
EI_REPLACE(0x006A5990, itemdb::RecordTableAt<kQuickItemSize>);
EI_REPLACE(0x006A59D0, itemdb::RecordTableAt<kQuestItemSize>);
EI_REPLACE(0x006A5A10, itemdb::RecordTableAt<kLootSize>);

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
void CopyConstructRecord(R* to, const R* from) {
    to->ConstructHead(R::kVtableAddress);
    ConstructExtra(to);
    to->Assign(from);
    to->CopyOwn(from);
}

// The resize(n) temporaries' fields beyond the members (zeroed bytes; S10a).
template <typename R>
void ZeroDefaultBytes(R*) {}
template <>
void ZeroDefaultBytes(WeaponRecord* record) {
    std::memset(record->field1B, 0, 0x83 - offsetof(WeaponRecord, field1B));
}
template <>
void ZeroDefaultBytes(QuickItemRecord* record) {
    std::memset(record->attributeBonus, 0, 0x8B - offsetof(QuickItemRecord, attributeBonus));
}

template <typename R>
void DefaultConstructRecord(R* to) {
    to->ConstructHead(R::kVtableAddress);
    ConstructExtra(to);
    ZeroDefaultBytes(to);
}

// uninitialized_copy(first, last, dest) and uninitialized_fill_n(dest, n, x) (__fastcall, RET 8:
// the second stack argument is STLport's unused type tag). Null destinations are skipped, as in
// the original's placement new. Both return the end of the destination.
template <typename R>
R* __fastcall CopyItemRecords(const R* first, const R* last, R* dest, int /*tag*/) {
    for (; first != last; ++first, ++dest) {
        if (dest) CopyConstructRecord(dest, first);
    }
    return dest;
}

template <typename R>
R* __fastcall FillItemRecords(R* dest, int count, const R* value, int /*tag*/) {
    for (; count != 0; --count, ++dest) {
        if (dest) CopyConstructRecord(dest, value);
    }
    return dest;
}

template void CopyConstructRecord(WeaponRecord*, const WeaponRecord*);
template void CopyConstructRecord(ArmorRecord*, const ArmorRecord*);
template void CopyConstructRecord(QuickItemRecord*, const QuickItemRecord*);
template void CopyConstructRecord(QuestItemRecord*, const QuestItemRecord*);
template void CopyConstructRecord(LootItemRecord*, const LootItemRecord*);
template void DefaultConstructRecord(WeaponRecord*);
template void DefaultConstructRecord(ArmorRecord*);
template void DefaultConstructRecord(QuickItemRecord*);
template void DefaultConstructRecord(QuestItemRecord*);
template void DefaultConstructRecord(LootItemRecord*);

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
