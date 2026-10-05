// Checks for party_lifetime.cpp. Reached by every new game: the party record reset adds the hero's
// record (006628D0: a record built on the stack, the party's record array grown, the record
// destroyed); party_records and ui_camp add and remove more.
// - PartyUnitRecord::Construct and ConstructPartyUnitRecords: the original on a scratch copy of the
//   memory (seeded with the real bytes), ours on the real one; all bytes compared (a fresh record's
//   lists are empty, so nothing is allocated or released).
// - Destruct, ClearItems, the record array's SetSize and the four AlignedPtrArray::SetSize copies
//   release items or allocate: ours only, the state afterwards checked.
#include "verify.h"

#include "../../src/game/party.h"
#include "../../src/game/unit.h"
#include "../../src/lib/crt.h"

#include <cstring>

namespace {

void* s_unused;

bool ItemsCleared(const PartyUnitRecord& r) {
    return r.weapons == nullptr && r.weaponCount == 0 && r.weaponCapacity == 0 && r.quickItems == nullptr &&
           r.quickItemCount == 0 && r.quickItemCapacity == 0 && r.armor == nullptr && r.armorCount == 0 &&
           r.armorCapacity == 0 && r.spells == nullptr && r.spellCount == 0 && r.spellCapacity == 0;
}

bool Defaults(const PartyUnitRecord& record) {
    return record.unknown_770 == 2 && record.weaponIndex == 0 && record.unknown_778 == 0 && record.unknown_77C == -1 &&
           record.unknown_780 == 1;
}

int FirstDifference(const void* a, const void* b, std::size_t size) {
    auto* x = static_cast<const unsigned char*>(a);
    auto* y = static_cast<const unsigned char*>(b);
    for (std::size_t i = 0; i < size; ++i)
        if (x[i] != y[i]) return static_cast<int>(i);
    return -1;
}

// 0065F5F0: PUSH -1; PUSH imm32 (7).
PartyUnitRecord*(__fastcall* s_construct)(PartyUnitRecord*);
PartyUnitRecord* __fastcall ConstructVerify(PartyUnitRecord* self) {
    auto* scratch = static_cast<PartyUnitRecord*>(gamecrt::operator_new(sizeof(PartyUnitRecord)));
    std::memcpy(scratch, self, sizeof(PartyUnitRecord));
    PartyUnitRecord* expected = s_construct(scratch);
    PartyUnitRecord* actual = self->Construct();
    int diff = FirstDifference(scratch, self, sizeof(PartyUnitRecord));
    ei::verify::Check("PartyUnitRecord::Construct", true, diff < 0 && expected == scratch && actual == self,
                      "record %p: first difference at +0x%X", self, diff);
    gamecrt::operator_delete(scratch);
    return actual;
}

// 00664880: MOV EAX,FS:[0] (6).
void(__stdcall* s_constructRecords)(PartyUnitRecord*, int);
void __stdcall ConstructRecordsVerify(PartyUnitRecord* records, int count) {
    std::size_t bytes = static_cast<std::size_t>(count) * sizeof(PartyUnitRecord);
    void* scratch = gamecrt::operator_new(bytes);
    std::memcpy(scratch, records, bytes);
    s_constructRecords(static_cast<PartyUnitRecord*>(scratch), count);
    ConstructPartyUnitRecords(records, count);
    int diff = FirstDifference(scratch, records, bytes);
    ei::verify::Check("ConstructPartyUnitRecords", true, diff < 0, "%d records at %p: first difference at +0x%X", count,
                      records, diff);
    gamecrt::operator_delete(scratch);
}

void __fastcall DestructVerify(PartyUnitRecord* self) {
    self->Destruct();
    // The lists' storage is freed (ClearItems already emptied them); the strings are gone.
    ei::verify::Check("PartyUnitRecord::Destruct", true, ItemsCleared(*self) && Defaults(*self), "record %p", self);
}

void __fastcall ClearItemsVerify(PartyUnitRecord* self) {
    self->ClearItems();
    ei::verify::Check("PartyUnitRecord::ClearItems", true, ItemsCleared(*self), "record %p", self);
}

void __fastcall MemberSetSizeVerify(PartyMemberArray* self, void*, int newSize, int newGrowBy) {
    int before = self->size;
    self->SetSize(newSize, newGrowBy);
    bool ok = self->size == newSize && self->capacity >= newSize && (newSize == 0) == (self->data == nullptr) &&
              (newGrowBy == -1 || self->growBy == newGrowBy);
    for (int i = before; ok && i < newSize; ++i) ok = ItemsCleared(self->data[i]) && Defaults(self->data[i]);
    ei::verify::Check("PartyMemberArray::SetSize", true, ok, "%d -> %d records (capacity %d)", before, newSize,
                      self->capacity);
}

void __fastcall ListSetSizeVerify(AlignedPtrArray* self, void*, int size, int growBy) {
    self->SetSize(size, growBy);
    bool ok = self->size == size && self->capacity >= size && (size == 0) == (self->data == nullptr) &&
              (reinterpret_cast<std::uintptr_t>(self->data) & 0x1F) == 0;
    ei::verify::Check("AlignedPtrArray::SetSize (record list copy)", true, ok, "size %d", size);
}

} // namespace

EI_VERIFY(0x0065F5F0, ConstructVerify, 7, s_construct);
EI_VERIFY(0x00664880, ConstructRecordsVerify, 6, s_constructRecords);
EI_VERIFY(0x0065F740, DestructVerify, 0, s_unused);
EI_VERIFY(0x0065F860, ClearItemsVerify, 0, s_unused);
EI_VERIFY(0x00663FA0, MemberSetSizeVerify, 0, s_unused);
EI_VERIFY(0x00663960, ListSetSizeVerify, 0, s_unused);
EI_VERIFY(0x00663AD0, ListSetSizeVerify, 0, s_unused);
EI_VERIFY(0x00663C00, ListSetSizeVerify, 0, s_unused);
EI_VERIFY(0x00663D30, ListSetSizeVerify, 0, s_unused);
