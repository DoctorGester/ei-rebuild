// Checks for the item database records' copies (item_db_records.cpp). They construct strings and
// string arrays in place, so ours runs alone and the wrapper compares each new record with its
// source: the kind's vtable, the head's copied fields (strings and the +0x20 array by contents)
// and the kind's own copied bytes; the result is the end of the destination.
// Scenario: every run (the item databases grow their vectors while they load at startup).
#include "verify.h"

#include "../../src/game/item_db.h"

#include <cstddef>
#include <cstring>

using namespace itemdb;

namespace {
void* s_unused;

bool SameBytes(const void* a, const void* b, std::size_t from, std::size_t to) {
    return std::memcmp(static_cast<const char*>(a) + from, static_cast<const char*>(b) + from, to - from) == 0;
}

bool SameHead(const ItemRecordHead* a, const ItemRecordHead* b) {
    bool ok = std::strcmp(a->text04, b->text04) == 0 && std::strcmp(a->text08, b->text08) == 0 &&
              std::strcmp(a->materialClass, b->materialClass) == 0 && a->type == b->type &&
              SameBytes(a, b, offsetof(ItemRecordHead, unknown_14), offsetof(ItemRecordHead, list20)) &&
              SameBytes(a, b, offsetof(ItemRecordHead, price), offsetof(ItemRecordHead, list50)) &&
              a->unknown_68 == b->unknown_68 && a->list20.GetSize() == b->list20.GetSize();
    for (int i = 0; ok && i < a->list20.GetSize(); ++i) ok = std::strcmp(a->list20[i], b->list20[i]) == 0;
    return ok;
}

// The kinds' own copied bytes.
bool SameOwn(const WeaponRecord* a, const WeaponRecord* b) { return SameBytes(a, b, 0x6C, 0x83) && SameBytes(a, b, 0x8C, 0xB4); }
bool SameOwn(const ArmorRecord* a, const ArmorRecord* b) { return SameBytes(a, b, 0x6C, 0xB4); }
bool SameOwn(const QuickItemRecord* a, const QuickItemRecord* b) {
    return a->value70 == b->value70 && SameBytes(a, b, 0x80, 0x8B) && a->text7C.GetLength() == 0;
}
bool SameOwn(const QuestItemRecord* a, const QuestItemRecord* b) { return a->category == b->category && !a->zonesBegin && !a->zonesEnd; }
bool SameOwn(const LootItemRecord* a, const LootItemRecord* b) { return SameBytes(a, b, 0x6C, 0x78); }

template <typename R>
bool Copied(const R* record, const R* source) {
    return reinterpret_cast<std::uintptr_t>(record->vtable) == R::kVtableAddress && SameHead(record, source) &&
           SameOwn(record, source);
}

template <typename R>
R* __fastcall CopyVerify(const R* first, const R* last, R* dest, int tag) {
    R* end = CopyItemRecords(first, last, dest, tag);
    int count = static_cast<int>(last - first);
    bool ok = end == dest + count;
    for (int i = 0; ok && dest && i < count; ++i) ok = Copied(dest + i, first + i);
    ei::verify::Check("item record uninitialized_copy", true, ok, "%d records of 0x%X bytes", count, static_cast<unsigned>(sizeof(R)));
    return end;
}

template <typename R>
R* __fastcall FillVerify(R* dest, int count, const R* value, int tag) {
    R* end = FillItemRecords(dest, count, value, tag);
    bool ok = end == dest + count;
    for (int i = 0; ok && dest && i < count; ++i) ok = Copied(dest + i, value);
    ei::verify::Check("item record uninitialized_fill_n", true, ok, "%d records of 0x%X bytes", count, static_cast<unsigned>(sizeof(R)));
    return end;
}
} // namespace

// 006A8F20 (stolen 0): the head's copied fields equal the source's; returns this.
static ItemRecordHead* __fastcall AssignVerify(ItemRecordHead* self, void*, const ItemRecordHead* source) {
    ItemRecordHead* result = self->Assign(source);
    ei::verify::Check("ItemRecordHead::Assign", true, result == self && SameHead(self, source), "type %d number %d",
                      source->type, source->number);
    return result;
}
EI_VERIFY(0x006A8F20, AssignVerify, 0, s_unused);

// 006A8FB0 (stolen 0): the weapon record's operator=: head and own bytes equal the source's;
// returns this. Scenario: only STLport inserts into the weapon vector in the middle (the database
// load appends, so it may not run).
static WeaponRecord* __fastcall AssignWeaponVerify(WeaponRecord* self, void*, const WeaponRecord* source) {
    WeaponRecord* result = self->AssignWeapon(source);
    ei::verify::Check("WeaponRecord::operator=", true, result == self && SameHead(self, source) && SameOwn(self, source),
                      "number %d", source->number);
    return result;
}
EI_VERIFY(0x006A8FB0, AssignWeaponVerify, 0, s_unused);

EI_VERIFY(0x006AB9C0, CopyVerify<WeaponRecord>, 0, s_unused);
EI_VERIFY(0x006ABB50, FillVerify<WeaponRecord>, 0, s_unused);
EI_VERIFY(0x006ABCD0, CopyVerify<ArmorRecord>, 0, s_unused);
EI_VERIFY(0x006ABE20, FillVerify<ArmorRecord>, 0, s_unused);
EI_VERIFY(0x006ABF60, CopyVerify<QuickItemRecord>, 0, s_unused);
EI_VERIFY(0x006AC0D0, FillVerify<QuickItemRecord>, 0, s_unused);
EI_VERIFY(0x006AC240, CopyVerify<QuestItemRecord>, 0, s_unused);
EI_VERIFY(0x006AC3B0, FillVerify<QuestItemRecord>, 0, s_unused);
EI_VERIFY(0x006AC520, CopyVerify<LootItemRecord>, 0, s_unused);
EI_VERIFY(0x006AC640, FillVerify<LootItemRecord>, 0, s_unused);

// A-H25. The records' slot 0 (MOV EAX,imm32: 5 bytes) against the original. Reached by every item
// built from a record (CItemManager::CreateItem's BeginCreateItem, the trader stock).
namespace {
template <int(__fastcall* Ours)(ItemRecordHead*)>
struct KindCheck {
    static inline int(__fastcall* original)(ItemRecordHead*);
    static int __fastcall Verify(ItemRecordHead* self) {
        int expected = original(self);
        ei::verify::Check("item record kind slot", expected, Ours(self), "%p", static_cast<void*>(self));
        return expected;
    }
};
} // namespace
// Plain names for the wrappers, so tools/check_stolen.py reads the registrations.
static constexpr auto& WeaponKindVerify = KindCheck<itemdb::WeaponRecordKind>::Verify;
static constexpr auto& ArmorKindVerify = KindCheck<itemdb::ArmorRecordKind>::Verify;
static constexpr auto& QuickKindVerify = KindCheck<itemdb::QuickItemRecordKind>::Verify;
static constexpr auto& QuestItemRecordKindVerify = KindCheck<itemdb::QuestItemRecordKind>::Verify;
static constexpr auto& LootItemRecordKindVerify = KindCheck<itemdb::LootItemRecordKind>::Verify;
EI_VERIFY(0x006A6430, WeaponKindVerify, 5, KindCheck<itemdb::WeaponRecordKind>::original);
EI_VERIFY(0x006A6440, ArmorKindVerify, 5, KindCheck<itemdb::ArmorRecordKind>::original);
EI_VERIFY(0x006A6450, QuickKindVerify, 5, KindCheck<itemdb::QuickItemRecordKind>::original);
EI_VERIFY(0x006A6460, QuestItemRecordKindVerify, 5, KindCheck<itemdb::QuestItemRecordKind>::original);
EI_VERIFY(0x006A6470, LootItemRecordKindVerify, 5, KindCheck<itemdb::LootItemRecordKind>::original);

// A-H25. The other kinds' operator= (stolen 0, as the weapon's: only a vector insert in the middle
// reaches them), the base record's constructor and the material's copy constructor (the vectors'
// resize and growth while the databases load: every run), RecordTable::At out of line (no caller
// left: 006A3CD0 is rewritten).
namespace {
template <typename R, R* (R::*Assign)(const R*)>
struct AssignCheck {
    static R* __fastcall Verify(R* self, void*, const R* source) {
        // What the kind's copy leaves alone must stay: the quick item's string, the quest item's zones.
        R* result = (self->*Assign)(source);
        ei::verify::Check("item record operator=", true, result == self && SameHead(self, source) && Own(self, source),
                          "0x%X bytes, number %d", static_cast<unsigned>(sizeof(R)), source->number);
        return result;
    }
    static bool Own(const ArmorRecord* a, const ArmorRecord* b) { return SameOwn(a, b); }
    static bool Own(const LootItemRecord* a, const LootItemRecord* b) { return SameOwn(a, b); }
    static bool Own(const QuickItemRecord* a, const QuickItemRecord* b) {
        return a->value70 == b->value70 && SameBytes(a, b, 0x80, 0x8B);
    }
    static bool Own(const QuestItemRecord* a, const QuestItemRecord* b) { return a->category == b->category; }
};
using AssignArmorCheck = AssignCheck<ArmorRecord, &ArmorRecord::AssignArmor>;
using AssignQuickCheck = AssignCheck<QuickItemRecord, &QuickItemRecord::AssignQuick>;
using AssignQuestCheck = AssignCheck<QuestItemRecord, &QuestItemRecord::AssignQuest>;
using AssignLootCheck = AssignCheck<LootItemRecord, &LootItemRecord::AssignLoot>;

// 006A63B0: PUSH -1; PUSH imm32 (7). The original on a scratch copy of the memory, ours on the
// record; every byte compared (nothing is allocated: empty strings and arrays).
ItemRecordHead*(__fastcall* s_constructBase)(ItemRecordHead*);
ItemRecordHead* __fastcall ConstructBaseVerify(ItemRecordHead* self) {
    alignas(4) unsigned char scratch[sizeof(ItemRecordHead)];
    std::memcpy(scratch, self, sizeof(scratch));
    auto* expected = s_constructBase(reinterpret_cast<ItemRecordHead*>(scratch));
    ItemRecordHead* result = self->ConstructBase();
    ei::verify::Check("ItemRecordHead::ConstructBase", true,
                      result == self && expected == reinterpret_cast<ItemRecordHead*>(scratch) &&
                          std::memcmp(scratch, self, sizeof(scratch)) == 0,
                      "record %p", static_cast<void*>(self));
    return result;
}

// 006A9E40: PUSH -1; PUSH imm32 (7). The original into a scratch record (its three strings
// destroyed afterwards), ours into the real one: the strings' texts and the bytes from +0x0C.
MaterialRecordHead*(__fastcall* s_materialCopy)(MaterialRecordHead*, void*, const MaterialRecordHead*);
MaterialRecordHead* __fastcall MaterialCopyVerify(MaterialRecordHead* self, void*, const MaterialRecordHead* source) {
    alignas(4) unsigned char raw[sizeof(MaterialRecordHead)];
    auto* scratch = reinterpret_cast<MaterialRecordHead*>(raw);
    auto* expected = s_materialCopy(scratch, nullptr, source);
    MaterialRecordHead* result = self->CopyConstruct(source);
    const bool same = result == self && expected == scratch && std::strcmp(scratch->key, self->key) == 0 &&
                      std::strcmp(scratch->soundName, self->soundName) == 0 && std::strcmp(scratch->name, self->name) == 0 &&
                      SameBytes(scratch, self, offsetof(MaterialRecordHead, number), sizeof(MaterialRecordHead));
    ei::verify::Check("MaterialRecordHead::CopyConstruct", true, same, "material '%s'", source->key);
    for (const char** text : {&scratch->key, &scratch->soundName, &scratch->name})
        reinterpret_cast<mfc::CString*>(text)->~CString();
    return result;
}

// 006A5950..006A5A10: PUSH ESI; MOV ESI,[ECX]; MOV ECX,[ECX+4] (6). Pure.
template <int Stride>
struct TableAtCheck {
    static inline char*(__fastcall* original)(const RecordTable*, void*, std::uint32_t);
    static char* __fastcall Verify(const RecordTable* table, void*, std::uint32_t index) {
        char* expected = original(table, nullptr, index);
        ei::verify::Check("RecordTable::At", true, expected == RecordTableAt<Stride>(table, nullptr, index),
                          "stride 0x%X index %u", static_cast<unsigned>(Stride), index);
        return expected;
    }
};
using ArmorAtCheck = TableAtCheck<kArmorSize>;
using QuickAtCheck = TableAtCheck<kQuickItemSize>;
using QuestAtCheck = TableAtCheck<kQuestItemSize>;
using LootAtCheck = TableAtCheck<kLootSize>;
} // namespace
EI_VERIFY(0x006A9370, AssignArmorCheck::Verify, 0, s_unused);
EI_VERIFY(0x006A9770, AssignQuickCheck::Verify, 0, s_unused);
EI_VERIFY(0x006AAE40, AssignQuestCheck::Verify, 0, s_unused);
EI_VERIFY(0x006A9DA0, AssignLootCheck::Verify, 0, s_unused);
EI_VERIFY(0x006A63B0, ConstructBaseVerify, 7, s_constructBase);
EI_VERIFY(0x006A9E40, MaterialCopyVerify, 7, s_materialCopy);
static constexpr auto& ArmorAtVerify = ArmorAtCheck::Verify;
static constexpr auto& QuickAtVerify = QuickAtCheck::Verify;
static constexpr auto& QuestAtVerify = QuestAtCheck::Verify;
static constexpr auto& LootAtVerify = LootAtCheck::Verify;
EI_VERIFY(0x006A5950, ArmorAtVerify, 6, ArmorAtCheck::original);
EI_VERIFY(0x006A5990, QuickAtVerify, 6, QuickAtCheck::original);
EI_VERIFY(0x006A59D0, QuestAtVerify, 6, QuestAtCheck::original);
EI_VERIFY(0x006A5A10, LootAtVerify, 6, LootAtCheck::original);
