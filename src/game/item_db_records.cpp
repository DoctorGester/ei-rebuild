// The item databases' records as values: the head's assignment and each record kind's copy constructor. See
// item_db.h and docs/notes/items_spells.md ("Databases").
#include "item_db.h"

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


ItemRecordHead* ItemRecordHead::Assign(const ItemRecordHead* source) {
    name = source->name;
    family = source->family;
    type = source->type;
    materialClass = source->materialClass;
    CopyBytes(this, source, offsetof(ItemRecordHead, unreadHeadField), offsetof(ItemRecordHead, classMask));
    buildable = source->buildable;
    return this;
}

void WeaponRecord::CopyOwn(const WeaponRecord* source) {
    CopyBytes(this, source, offsetof(WeaponRecord, category), offsetof(WeaponRecord, attackBonus) - 1);
    CopyBytes(this, source, offsetof(WeaponRecord, reach), sizeof(WeaponRecord));
}

void ArmorRecord::CopyOwn(const ArmorRecord* source) {
    CopyBytes(this, source, offsetof(ArmorRecord, category), offsetof(ArmorRecord, showsWounds));
}

// ---- The records' destructors ------------------------------------------------------------------

ItemRecordHead::~ItemRecordHead() = default;

QuickItemRecord::~QuickItemRecord() = default;

QuestItemRecord::~QuestItemRecord() {
}

void QuickItemRecord::CopyOwn(const QuickItemRecord* source) {
    actionValue = source->actionValue;
    std::memcpy(attributeBonus, source->attributeBonus, sizeof(attributeBonus));
}

void QuestItemRecord::CopyOwn(const QuestItemRecord* source) {
    category = source->category;
}

void LootItemRecord::CopyOwn(const LootItemRecord* source) {
    CopyBytes(this, source, offsetof(LootItemRecord, category), sizeof(LootItemRecord));
}

// The kinds' default and copy constructors (the extra members constructed between the head and the Assign).
WeaponRecord::WeaponRecord() { std::memset(skillBonus, 0, offsetof(WeaponRecord, attackBonus) - 1 - offsetof(WeaponRecord, skillBonus)); }
QuickItemRecord::QuickItemRecord() { std::memset(attributeBonus, 0, sizeof(attributeBonus)); }

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
