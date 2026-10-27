// The item databases' records as values: the head's assignment and each record kind's
// copy constructor as STLport's uninitialized_copy / uninitialized_fill_n inline it (the vectors'
// growth while the databases load). See item_db.h and docs/notes/items_spells.md ("Databases").
#include "item_db.h"

#include "console.h"
#include "game_data.h"

#include "ei/hook.h"
#include "record_lifetime.h"

#include <cstddef>
#include <cstring>
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
EI_REWRITTEN(0x006A6430, WeaponRecord::Kind);
EI_REWRITTEN(0x006A6440, ArmorRecord::Kind);
EI_REWRITTEN(0x006A6450, QuickItemRecord::Kind);
EI_REWRITTEN(0x006A6460, QuestItemRecord::Kind);
EI_REWRITTEN(0x006A6470, LootItemRecord::Kind);

EI_REWRITTEN_SPECIAL(0x006A63B0, ItemRecordHead::ItemRecordHead);

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
    list20.Copy(source->list20);
    return this;
}
EI_REWRITTEN(0x006A8F20, ItemRecordHead::Assign);

void WeaponRecord::CopyOwn(const WeaponRecord* source) {
    CopyBytes(this, source, offsetof(WeaponRecord, category), 0x83);
    CopyBytes(this, source, offsetof(WeaponRecord, reach), sizeof(WeaponRecord));
}

WeaponRecord* WeaponRecord::AssignWeapon(const WeaponRecord* source) {
    Assign(source);
    CopyOwn(source);
    return this;
}
EI_REWRITTEN(0x006A8FB0, WeaponRecord::AssignWeapon);

void ArmorRecord::CopyOwn(const ArmorRecord* source) {
    CopyBytes(this, source, offsetof(ArmorRecord, category), 0xB4);
}

// The other kinds' operator=, as the weapon's.
ArmorRecord* ArmorRecord::AssignArmor(const ArmorRecord* source) {
    Assign(source);
    CopyOwn(source);
    return this;
}
EI_REWRITTEN(0x006A9370, ArmorRecord::AssignArmor);

QuickItemRecord* QuickItemRecord::AssignQuick(const QuickItemRecord* source) {
    Assign(source);
    CopyOwn(source);
    return this;
}
EI_REWRITTEN(0x006A9770, QuickItemRecord::AssignQuick);

QuestItemRecord* QuestItemRecord::AssignQuest(const QuestItemRecord* source) {
    Assign(source);
    CopyOwn(source);
    return this;
}
EI_REWRITTEN(0x006AAE40, QuestItemRecord::AssignQuest);

LootItemRecord* LootItemRecord::AssignLoot(const LootItemRecord* source) {
    Assign(source);
    CopyOwn(source);
    return this;
}
EI_REWRITTEN(0x006A9DA0, LootItemRecord::AssignLoot);

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

// ---- The records' destructors ------------------------------------------------------------------

ItemRecordHead::~ItemRecordHead() = default;
EI_REWRITTEN_SPECIAL(0x006A8950, ItemRecordHead::~ItemRecordHead);   // weapons
EI_REWRITTEN_SPECIAL(0x006A89D0, ItemRecordHead::~ItemRecordHead);   // armours
EI_REWRITTEN_SPECIAL(0x006A8BD0, ItemRecordHead::~ItemRecordHead);   // loot

QuickItemRecord::~QuickItemRecord() = default;
EI_REWRITTEN_SPECIAL(0x006A8A50, QuickItemRecord::~QuickItemRecord);

QuestItemRecord::~QuestItemRecord() {
    for (mfc::CString& zone : zones) zone.~CString();
    zones.FreeStorage();
}
EI_REWRITTEN_SPECIAL(0x006A8AE0, QuestItemRecord::~QuestItemRecord);

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
    repeated.~string();
    records::DestroyVector(optional);
    records::DestroyVector(required);
    spell.~string();
}
EI_REWRITTEN(0x00589860, SpellListRecord::Destruct);

// ---- The spell records as vector elements -------------------------------------

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
    new (&spell) stlp::string(source->spell);
    required.CopyConstruct<stlp::StringElement>(source->required);
    optional.CopyConstruct<stlp::StringElement>(source->optional);
    new (&repeated) stlp::string(source->repeated);
    classMask = source->classMask;
}

void SpellListRecord::AssignList(const SpellListRecord* source) {
    spell.AssignFrom(source->spell);
    required.AssignVector<stlp::StringElement>(source->required);
    optional.AssignVector<stlp::StringElement>(source->optional);
    repeated.AssignFrom(source->repeated);
    classMask = source->classMask;
}

void SpellListRecord::ConstructDefault() {
    stlp::StringElement::Default(&spell);
    required.ConstructEmpty();
    optional.ConstructEmpty();
    stlp::StringElement::Default(&repeated);
}

// The material and weapon tables' At, as the unit skins and the client's maps read them (0x50- / 0xB4-byte
// records).
EI_REWRITTEN(0x0056E400, itemdb::MaterialTable::At);
EI_REWRITTEN(0x0056E440, itemdb::WeaponTable::At);
EI_REWRITTEN(0x006A5950, itemdb::ArmorTable::At);
EI_REWRITTEN(0x006A5990, itemdb::QuickItemTable::At);
EI_REWRITTEN(0x006A59D0, itemdb::QuestItemTable::At);
EI_REWRITTEN(0x006A5A10, itemdb::LootItemTable::At);

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

// The kinds' default and copy constructors (the extra members constructed between the head and the
// Assign).
WeaponRecord::WeaponRecord() { std::memset(field1B, 0, 0x83 - offsetof(WeaponRecord, field1B)); }
QuickItemRecord::QuickItemRecord() { std::memset(attributeBonus, 0, 0x8B - offsetof(QuickItemRecord, attributeBonus)); }

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
    zones.ConstructEmpty();
    Assign(&from);
    CopyOwn(&from);
}
LootItemRecord::LootItemRecord(const LootItemRecord& from) {
    Assign(&from);
    CopyOwn(&from);
}

namespace itemdb {


// uninitialized_copy(first, last, dest) and uninitialized_fill_n(dest, n, x) (__fastcall: the second stack argument
// is STLport's unused type tag). Null destinations are skipped, as placement new does. Both return the end of the
// destination.
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

EI_REWRITTEN(0x006AB9C0, CopyItemRecords<WeaponRecord>);
EI_REWRITTEN(0x006ABB50, FillItemRecords<WeaponRecord>);
EI_REWRITTEN(0x006ABCD0, CopyItemRecords<ArmorRecord>);
EI_REWRITTEN(0x006ABE20, FillItemRecords<ArmorRecord>);
EI_REWRITTEN(0x006ABF60, CopyItemRecords<QuickItemRecord>);
EI_REWRITTEN(0x006AC0D0, FillItemRecords<QuickItemRecord>);
EI_REWRITTEN(0x006AC240, CopyItemRecords<QuestItemRecord>);
EI_REWRITTEN(0x006AC3B0, FillItemRecords<QuestItemRecord>);
EI_REWRITTEN(0x006AC520, CopyItemRecords<LootItemRecord>);
EI_REWRITTEN(0x006AC640, FillItemRecords<LootItemRecord>);
