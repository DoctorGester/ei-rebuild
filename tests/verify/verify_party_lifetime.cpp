// Checks for party_lifetime.cpp. Reached by every new game: the party record reset adds the hero's
// record (006628D0: a record built on the stack, the party's record array grown, the record
// destroyed); party_records and ui_camp add and remove more.
// - PartyUnitRecord::Construct and ConstructPartyUnitRecords: the original on a scratch copy of the
//   memory (seeded with the real bytes), ours on the real one; all bytes compared (a fresh record's
//   lists are empty, so nothing is allocated or released).
// - Destruct, ClearItems, the record array's SetSize and the four AlignedPtrArray::SetSize copies
//   release items or allocate: ours only, the state afterwards checked.
// - CParty::Construct: the original on a scratch copy seeded with the real bytes; all bytes
//   compared except the variable map's (functor bytes from the stack, bucket storage), whose
//   bucket count and emptiness are compared instead. The item and spell constructors' constructors
//   likewise (whole object). The spell manager's constructor allocates: ours only.
// - CParty's destructor, slot 0, Create, Destroy and Clear make, release or delete objects: ours
//   only, the state afterwards checked (slot 0 runs without the free, which the wrapper does).
//   Parties are made, cleared and destroyed by every new game (the named party records, the player's
//   inventory), the camp's lists (ui_camp, trader_stock) and local parties of the server code.
#include "verify.h"

#include "../../src/game/item.h"
#include "../../src/game/party.h"
#include "../../src/game/spell.h"
#include "../../src/game/unit.h"
#include "../../src/lib/crt.h"

#include <cstdint>
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

// A-H24: the record's deleting destructor (party loading, 006618C0), DestructElements, the record
// array's destructor body and slot 0 (party records destroyed: SavedParties::Reload). Ours only.
PartyUnitRecord* __fastcall DeletingVerify(PartyUnitRecord* self, void*, unsigned flags) {
    PartyUnitRecord* result = self->DeletingDestruct(flags & ~1u);
    ei::verify::Check("PartyUnitRecord::DeletingDestruct", true, result == self && ItemsCleared(*self) && Defaults(*self),
                      "record %p", self);
    if (flags & 1) gamecrt::operator_delete(self);
    return result;
}

void __stdcall DestructRecordsVerify(PartyUnitRecord* records, int count) {
    DestructPartyUnitRecords(records, count);
    bool ok = true;
    for (int i = 0; ok && i < count; ++i) ok = ItemsCleared(records[i]) && Defaults(records[i]);
    ei::verify::Check("DestructPartyUnitRecords", true, ok, "%d records at %p", count, records);
}

void __fastcall MemberArrayDestructVerify(PartyMemberArray* self) {
    DestructPartyMemberArray(self);
    ei::verify::Check("DestructPartyMemberArray", true, reinterpret_cast<std::uintptr_t>(self->vtable) == kPartyMemberArrayVtable, "array %p", self);
}

PartyMemberArray* __fastcall MemberArrayDeleteVerify(PartyMemberArray* self, void*, unsigned flags) {
    PartyMemberArray* result = DeletePartyMemberArray(self, nullptr, flags & ~1u);
    ei::verify::Check("DeletePartyMemberArray", true, result == self && reinterpret_cast<std::uintptr_t>(self->vtable) == kPartyMemberArrayVtable,
                      "array %p", self);
    if (flags & 1) gamecrt::operator_delete(self);
    return result;
}

void __fastcall ClearItemsVerify(PartyUnitRecord* self) {
    self->ClearItems();
    ei::verify::Check("PartyUnitRecord::ClearItems", true, ItemsCleared(*self), "record %p", self);
}

void __fastcall MemberSetSizeVerify(PartyMemberArray* self, void*, int newSize, int newGrowBy) {
    int before = self->size;
    self->SetSize(newSize, newGrowBy);
    bool ok = self->size == newSize && self->maxSize >= newSize && (newSize == 0) == (self->data == nullptr) &&
              (newGrowBy == -1 || self->growBy == newGrowBy);
    for (int i = before; ok && i < newSize; ++i) ok = ItemsCleared(self->data[i]) && Defaults(self->data[i]);
    ei::verify::Check("PartyMemberArray::SetSize", true, ok, "%d -> %d records (capacity %d)", before, newSize,
                      self->maxSize);
}

void __fastcall ListSetSizeVerify(AlignedPtrArray* self, void*, int size, int growBy) {
    self->SetSize(size, growBy);
    bool ok = self->size == size && self->capacity >= size && (size == 0) == (self->data == nullptr) &&
              (reinterpret_cast<std::uintptr_t>(self->data) & 0x1F) == 0;
    ei::verify::Check("AlignedPtrArray::SetSize (record list copy)", true, ok, "size %d", size);
}

// --- CParty and the managers it makes -----------------------------------------------------------

std::uintptr_t VtableOf(const void* object) {
    std::uintptr_t vtable;
    std::memcpy(&vtable, object, sizeof(vtable));
    return vtable;
}

// An empty map with NextPrime(100) = 193 buckets.
template <typename Map>
bool EmptyMap(const Map& map) {
    bool ok = map.BucketCount() == 193 && map.Size() == 0;
    for (std::size_t b = 0; ok && b < map.BucketCount(); ++b) ok = map.Bucket(b) == nullptr;
    return ok;
}

// The original constructor on scratch memory seeded with the object's bytes, ours on the object;
// all bytes compared, except [skipFrom, skipTo).
template <typename T>
int ConstructBoth(T*(__fastcall* original)(T*), T* self, T** expected, T** actual, std::size_t skipFrom, std::size_t skipTo,
                  T** scratchOut) {
    auto* scratch = static_cast<T*>(gamecrt::operator_new(sizeof(T)));
    std::memcpy(scratch, self, sizeof(T));
    *expected = original(scratch);
    *actual = self->Construct();
    *scratchOut = scratch;
    auto* x = reinterpret_cast<const unsigned char*>(scratch);
    auto* y = reinterpret_cast<const unsigned char*>(self);
    for (std::size_t i = 0; i < sizeof(T); ++i)
        if ((i < skipFrom || i >= skipTo) && x[i] != y[i]) return static_cast<int>(i);
    return -1;
}

// 00661050, 006A5AB0 (MOV EAX,ECX; XOR ECX,ECX; MOV [EAX+4],ECX), 00683FF0: 7 bytes each.
CParty*(__fastcall* s_partyConstruct)(CParty*);
CItemConstructor*(__fastcall* s_itemConstructorConstruct)(CItemConstructor*);
CSpellConstructor*(__fastcall* s_spellConstructorConstruct)(CSpellConstructor*);

CParty* __fastcall PartyConstructVerify(CParty* self) {
    CParty *expected, *actual, *scratch;
    int diff = ConstructBoth(s_partyConstruct, self, &expected, &actual, offsetof(CParty, variables_),
                             offsetof(CParty, zone_), &scratch);
    bool ok = diff < 0 && expected == scratch && actual == self && EmptyMap(self->variables_) && EmptyMap(scratch->variables_);
    ei::verify::Check("CParty::Construct", true, ok, "party %p: first difference at +0x%X", self, diff);
    scratch->variables_.Destroy();
    gamecrt::operator_delete(scratch);
    return actual;
}

CItemConstructor* __fastcall ItemConstructorConstructVerify(CItemConstructor* self) {
    CItemConstructor *expected, *actual, *scratch;
    int diff = ConstructBoth(s_itemConstructorConstruct, self, &expected, &actual, 0, 0, &scratch);
    ei::verify::Check("CItemConstructor::Construct", true, diff < 0 && expected == scratch && actual == self,
                      "%p: first difference at +0x%X", self, diff);
    gamecrt::operator_delete(scratch);
    return actual;
}

CSpellConstructor* __fastcall SpellConstructorConstructVerify(CSpellConstructor* self) {
    CSpellConstructor *expected, *actual, *scratch;
    int diff = ConstructBoth(s_spellConstructorConstruct, self, &expected, &actual, 0, 0, &scratch);
    ei::verify::Check("CSpellConstructor::Construct", true, diff < 0 && expected == scratch && actual == self,
                      "%p: first difference at +0x%X", self, diff);
    gamecrt::operator_delete(scratch);
    return actual;
}

CSpellManager* __fastcall SpellManagerConstructVerify(CSpellManager* self) {
    CSpellManager* result = self->Construct();
    bool ok = result == self && VtableOf(self) == CSpellManager::kVtableAddress && EmptyMap(self->sequences_) &&
              self->maxSequenceKey_ == 0 && self->refCount_ == 0 && self->owner_ == nullptr;
    ei::verify::Check("CSpellManager::Construct", true, ok, "%p", self);
    return result;
}

bool ArraysEmpty(const CParty* party) {
    return party->items_.data == nullptr && party->items_.size == 0 && party->members_.data == nullptr &&
           party->members_.size == 0;
}

// What the destructor body leaves.
bool PartyDestructed(const CParty* party) {
    return VtableOf(party) == CEngineObject::kVtableAddress && party->owner_ == nullptr && ArraysEmpty(party) &&
           party->itemManager_ == nullptr && party->spellManager_ == nullptr && party->itemConstructor_ == nullptr &&
           party->spellConstructor_ == nullptr && party->variables_.Size() == 0 &&
           VtableOf(&party->members_) == kPartyMemberArrayVtable;
}

void __fastcall PartyDestructVerify(CParty* self) {
    self->DestructBody();
    ei::verify::Check("CParty::DestructBody", true, PartyDestructed(self), "party %p", self);
}

CParty* __fastcall PartyDeletingVerify(CParty* self, void*, unsigned flags) {
    CParty* result = self->DeletingDestruct(flags & ~1u);
    ei::verify::Check("CParty::DeletingDestruct", true, result == self && PartyDestructed(self), "party %p flags %u", self,
                      flags);
    if (flags & 1) gamecrt::operator_delete(self);
    return result;
}

bool Counted(const CEngineObject* object, std::uintptr_t vtable) {
    return object != nullptr && object->refCount_ >= 1 && VtableOf(object) == vtable;
}

BOOL __fastcall PartyCreateVerify(CParty* self) {
    BOOL result = self->CreateParty();
    bool ok = result == 1 && Counted(self->itemManager_, CItemManager::kVtableAddress) &&
              Counted(self->spellManager_, CSpellManager::kVtableAddress) &&
              Counted(self->itemConstructor_, CItemConstructor::kVtableAddress) &&
              Counted(self->spellConstructor_, CSpellConstructor::kVtableAddress) && ArraysEmpty(self) &&
              self->zone_.GetLength() == 0 && self->variables_.Size() == 0;
    ei::verify::Check("CParty::CreateParty", true, ok, "party %p", self);
    return result;
}

void __fastcall PartyDestroyVerify(CParty* self) {
    self->DestroyParty();
    bool ok = ArraysEmpty(self) && self->itemManager_ == nullptr && self->spellManager_ == nullptr &&
              self->itemConstructor_ == nullptr && self->spellConstructor_ == nullptr;
    ei::verify::Check("CParty::DestroyParty", true, ok, "party %p", self);
}

void __fastcall PartyClearVerify(CParty* self, void*, int clearVariables, int keepSpells) {
    std::size_t variables = self->variables_.Size();
    self->Clear(clearVariables, keepSpells);
    bool ok = ArraysEmpty(self) && self->zone_.GetLength() == 0 && self->unknown_50 == 0 && self->unknown_6C == 0 &&
              self->variables_.Size() == (clearVariables != 0 ? 0 : variables);
    ei::verify::Check("CParty::Clear", true, ok, "party %p (%d, %d)", self, clearVariables, keepSpells);
}

// A-H17. 006641D0 (no caller: slot 2 of the records' CArray vtable). Ours only (it moves the
// archive): storing keeps the size.
void __fastcall MemberSerializeVerify(PartyMemberArray* self, void*, mfc::CArchive* ar) {
    bool loading = ar->IsLoading();
    int size = self->size;
    self->Serialize(ar);
    ei::verify::Check("PartyMemberArray::Serialize", true, loading || self->size == size, "array %p: %d -> %d", self,
                      size, self->size);
}

} // namespace

EI_VERIFY(0x0065F5F0, ConstructVerify, 7, s_construct);
EI_VERIFY(0x00664880, ConstructRecordsVerify, 6, s_constructRecords);
EI_VERIFY(0x0065F740, DestructVerify, 0, s_unused);
EI_VERIFY(0x0065F860, ClearItemsVerify, 0, s_unused);
EI_VERIFY(0x00664AC0, DeletingVerify, 0, s_unused);
EI_VERIFY(0x00664850, DestructRecordsVerify, 0, s_unused);
EI_VERIFY(0x00664160, MemberArrayDestructVerify, 0, s_unused);
EI_VERIFY(0x00664470, MemberArrayDeleteVerify, 0, s_unused);
EI_VERIFY(0x00663FA0, MemberSetSizeVerify, 0, s_unused);
EI_VERIFY(0x006641D0, MemberSerializeVerify, 0, s_unused);
EI_VERIFY(0x00663960, ListSetSizeVerify, 0, s_unused);
EI_VERIFY(0x00663AD0, ListSetSizeVerify, 0, s_unused);
EI_VERIFY(0x00663C00, ListSetSizeVerify, 0, s_unused);
EI_VERIFY(0x00663D30, ListSetSizeVerify, 0, s_unused);
EI_VERIFY(0x00661050, PartyConstructVerify, 7, s_partyConstruct);
EI_VERIFY(0x006A5AB0, ItemConstructorConstructVerify, 7, s_itemConstructorConstruct);
EI_VERIFY(0x00683FF0, SpellConstructorConstructVerify, 7, s_spellConstructorConstruct);
EI_VERIFY(0x00688840, SpellManagerConstructVerify, 0, s_unused);
EI_VERIFY(0x006611D0, PartyDestructVerify, 0, s_unused);
EI_VERIFY(0x00661190, PartyDeletingVerify, 0, s_unused);
EI_VERIFY(0x006614F0, PartyCreateVerify, 0, s_unused);
EI_VERIFY(0x00661640, PartyDestroyVerify, 0, s_unused);
EI_VERIFY(0x00661700, PartyClearVerify, 0, s_unused);
