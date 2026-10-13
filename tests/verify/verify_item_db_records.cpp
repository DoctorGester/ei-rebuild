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
              std::strcmp(a->text10, b->text10) == 0 && a->type == b->type &&
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
EI_VERIFY(0x006A6430, KindCheck<itemdb::WeaponRecordKind>::Verify, 5, KindCheck<itemdb::WeaponRecordKind>::original);
EI_VERIFY(0x006A6440, KindCheck<itemdb::ArmorRecordKind>::Verify, 5, KindCheck<itemdb::ArmorRecordKind>::original);
EI_VERIFY(0x006A6450, KindCheck<itemdb::QuickItemRecordKind>::Verify, 5, KindCheck<itemdb::QuickItemRecordKind>::original);
EI_VERIFY(0x006A6460, KindCheck<itemdb::Record7CKind>::Verify, 5, KindCheck<itemdb::Record7CKind>::original);
EI_VERIFY(0x006A6470, KindCheck<itemdb::Record78Kind>::Verify, 5, KindCheck<itemdb::Record78Kind>::original);
