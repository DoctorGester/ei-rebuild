// The item databases' records as values: the head's assignment and each record kind's
// copy constructor as STLport's uninitialized_copy / uninitialized_fill_n inline it (the vectors'
// growth while the databases load). See item_db.h and docs/notes/items_spells.md ("Databases").
#include "item_db.h"

#include "console.h"
#include "game_data.h"

#include "record_lifetime.h"

#include <cstddef>
#include <cstring>
#include <memory>
#include <new>

using namespace itemdb;

namespace {

// Bytes [from, to) of `source` copied into `target` (dword, word and byte moves).
template <typename R>
void CopyBytes(R* target, const R* source, std::size_t from, std::size_t to) {
    std::memcpy(reinterpret_cast<char*>(target) + from, reinterpret_cast<const char*>(source) + from, to - from);
}

} // namespace

int WeaponRecord::Kind() { return 0x3004; }
int ArmorRecord::Kind() { return 0x3005; }
int QuickItemRecord::Kind() { return 0x3006; }
int QuestItemRecord::Kind() { return 0x3009; }
int LootItemRecord::Kind() { return 0x3007; }

void* ItemRecordHead::operator new(std::size_t size) noexcept { return gamecrt::operator_new(size); }
void ItemRecordHead::operator delete(void* block) { gamecrt::operator_delete(block); }

ItemRecordHead* ItemRecordHead::Assign(const ItemRecordHead* source) {
    text04 = source->text04;
    text08 = source->text08;
    type = source->type;
    materialClass = source->materialClass;
    CopyBytes(this, source, offsetof(ItemRecordHead, field4), offsetof(ItemRecordHead, list20));
    CopyBytes(this, source, offsetof(ItemRecordHead, price), offsetof(ItemRecordHead, list50));
    buildable = source->buildable;
    list20 = source->list20;
    return this;
}

void WeaponRecord::CopyOwn(const WeaponRecord* source) {
    CopyBytes(this, source, offsetof(WeaponRecord, category), offsetof(WeaponRecord, attackBonus) - 1);
    CopyBytes(this, source, offsetof(WeaponRecord, reach), sizeof(WeaponRecord));
}

void ArmorRecord::CopyOwn(const ArmorRecord* source) {
    CopyBytes(this, source, offsetof(ArmorRecord, category), offsetof(ArmorRecord, showsWounds));
}

MaterialRecordHead* MaterialRecordHead::CopyConstruct(const MaterialRecordHead* source) {
    std::construct_at(&key, source->key);
    std::construct_at(&soundName, source->soundName);
    std::construct_at(&name, source->name);
    CopyBytes(this, source, offsetof(MaterialRecordHead, number), sizeof(MaterialRecordHead));
    return this;
}

void MaterialRecordHead::AssignMaterial(const MaterialRecordHead* source) {
    key = source->key;
    soundName = source->soundName;
    name = source->name;
    CopyBytes(this, source, offsetof(MaterialRecordHead, number), sizeof(MaterialRecordHead));
}

void MaterialRecordHead::ConstructDefault() {
    std::construct_at(&key);
    std::construct_at(&soundName);
    std::construct_at(&name);
}

// ---- The records' destructors ------------------------------------------------------------------

ItemRecordHead::~ItemRecordHead() = default;

QuickItemRecord::~QuickItemRecord() = default;

QuestItemRecord::~QuestItemRecord() {
}

void MaterialRecordHead::Destruct() {
    std::destroy_at(&name);
    std::destroy_at(&soundName);
    std::destroy_at(&key);
}

void SpellRecord::Destruct() {
    std::destroy_at(&list54);
    std::destroy_at(&list48);
    std::destroy_at(&text08);
    std::destroy_at(&code);
    std::destroy_at(&name);
}

void SpellModifierRecord::Destruct() {
    std::destroy_at(&text1C);
    std::destroy_at(&name);
}

void SpellListRecord::Destruct() {
    std::destroy_at(&repeated);
    std::destroy_at(&optional);
    std::destroy_at(&required);
    std::destroy_at(&spell);
}

// ---- The spell records as vector elements -------------------------------------

namespace {
using IdElement = stlp::PlainElement<int>;
} // namespace

void SpellRecord::CopyConstruct(const SpellRecord* source) {
    std::construct_at(&name, source->name);
    std::construct_at(&code, source->code);
    std::construct_at(&text08, source->text08);
    CopyBytes(this, source, offsetof(SpellRecord, price), offsetof(SpellRecord, list48));
    std::construct_at(&list48, source->list48);
    std::construct_at(&list54, source->list54);
    CopyBytes(this, source, offsetof(SpellRecord, icon), offsetof(SpellRecord, unused_7D));
    classMask = source->classMask;
}

void SpellRecord::AssignSpell(const SpellRecord* source) {
    name = source->name;
    code = source->code;
    text08 = source->text08;
    CopyBytes(this, source, offsetof(SpellRecord, price), offsetof(SpellRecord, list48));
    list48 = source->list48;
    list54 = source->list54;
    CopyBytes(this, source, offsetof(SpellRecord, icon), offsetof(SpellRecord, unused_7D));
    classMask = source->classMask;
}

void SpellRecord::ConstructDefault() {
    std::construct_at(&name);
    std::construct_at(&code);
    std::construct_at(&text08);
    std::construct_at(&list48);
    std::construct_at(&list54);
}

void SpellModifierRecord::CopyConstruct(const SpellModifierRecord* source) {
    std::construct_at(&name, source->name);
    CopyBytes(this, source, offsetof(SpellModifierRecord, code), offsetof(SpellModifierRecord, text1C));
    std::construct_at(&text1C, source->text1C);
    classMask = source->classMask;
}

void SpellModifierRecord::AssignModifier(const SpellModifierRecord* source) {
    name = source->name;
    CopyBytes(this, source, offsetof(SpellModifierRecord, code), offsetof(SpellModifierRecord, text1C));
    text1C = source->text1C;
    classMask = source->classMask;
}

void SpellModifierRecord::ConstructDefault() {
    std::construct_at(&name);
    std::construct_at(&text1C);
}

void SpellListRecord::CopyConstruct(const SpellListRecord* source) {
    std::construct_at(&spell, source->spell);
    std::construct_at(&required, source->required);
    std::construct_at(&optional, source->optional);
    std::construct_at(&repeated, source->repeated);
    classMask = source->classMask;
}

void SpellListRecord::AssignList(const SpellListRecord* source) {
    spell = source->spell;
    required = source->required;
    optional = source->optional;
    repeated = source->repeated;
    classMask = source->classMask;
}

void SpellListRecord::ConstructDefault() {
    std::construct_at(&spell);
    std::construct_at(&required);
    std::construct_at(&optional);
    std::construct_at(&repeated);
}

void QuickItemRecord::CopyOwn(const QuickItemRecord* source) {
    value70 = source->value70;
    CopyBytes(this, source, offsetof(QuickItemRecord, attributeBonus), offsetof(QuickItemRecord, unused_8B));
}

void QuestItemRecord::CopyOwn(const QuestItemRecord* source) {
    category = source->category;
}

void LootItemRecord::CopyOwn(const LootItemRecord* source) {
    CopyBytes(this, source, offsetof(LootItemRecord, category), sizeof(LootItemRecord));
}

// The kinds' default and copy constructors (the extra members constructed between the head and the Assign).
WeaponRecord::WeaponRecord() { std::memset(field1B, 0, offsetof(WeaponRecord, attackBonus) - 1 - offsetof(WeaponRecord, field1B)); }
QuickItemRecord::QuickItemRecord() { std::memset(attributeBonus, 0, offsetof(QuickItemRecord, unused_8B) - offsetof(QuickItemRecord, attributeBonus)); }

WeaponRecord::WeaponRecord(const WeaponRecord& from) {
    Assign(&from);
    CopyOwn(&from);
}
ArmorRecord::ArmorRecord(const ArmorRecord& from) {
    Assign(&from);
    CopyOwn(&from);
}
QuickItemRecord::QuickItemRecord(const QuickItemRecord& from) {
    Assign(&from);
    CopyOwn(&from);
}
QuestItemRecord::QuestItemRecord(const QuestItemRecord& from) {
    Assign(&from);
    CopyOwn(&from);
}
LootItemRecord::LootItemRecord(const LootItemRecord& from) {
    Assign(&from);
    CopyOwn(&from);
}

namespace itemdb {


// uninitialized_copy(first, last, dest) and uninitialized_fill_n(dest, n, x) (the last argument is STLport's
// unused type tag). Null destinations are skipped, as placement new does. Both return the end of the destination.
template <typename R>
R* __fastcall CopyItemRecords(const R* first, const R* last, R* dest, int /*tag*/) {
    for (; first != last; ++first, ++dest) {
        if (dest) new (dest) R(*first);
    }
    return dest;
}

template <typename R>
R* __fastcall FillItemRecords(R* dest, int count, const R* value, int /*tag*/) {
    for (; count != 0; --count, ++dest) {
        if (dest) new (dest) R(*value);
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
