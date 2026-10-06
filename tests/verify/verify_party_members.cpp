// Checks for the party's unit records (party_members.cpp). Everything creates, copies or releases
// counted items or moves records, so ours runs alone and the wrapper checks the result.
// Scenarios: party_records (AddUnitToParty 006628D0 -> 006621F0 / 00664950 and the SetAtGrow
// copies, CopyItems 00660500, RemoveUnitFromParty 006620F0); every new game adds the hero
// (006628D0).
#include "verify.h"

#include "../../src/game/item.h"
#include "../../src/game/party.h"
#include "../../src/game/unit.h"

#include <cstddef>
#include <cstring>

namespace {
void* s_unused;

const AlignedPtrArray& List(CEngineObject** const& data) { return *reinterpret_cast<const AlignedPtrArray*>(&data); }

// `list` holds copies of `source`'s items: same count, kinds and counts (+0x48), new objects.
bool CopiedList(const AlignedPtrArray& list, const AlignedPtrArray& source) {
    if (list.size != source.size) return false;
    for (int i = 0; i < list.size; ++i) {
        auto* copy = static_cast<const CItem*>(list.data[i]);
        auto* item = static_cast<const CItem*>(source.data[i]);
        if (!copy || copy == item || copy->type_ != item->type_ || copy->unknown_48 != item->unknown_48) return false;
    }
    return true;
}

bool SameTexts(const PartyUnitRecord& a, const PartyUnitRecord& b) {
    for (int i = 0; i < 7; ++i)
        if (std::strcmp(a.texts[i], b.texts[i]) != 0) return false;
    return true;
}
} // namespace

// 00660500 (stolen 0): every list a copy of the source's.
static void __fastcall CopyItemsFromVerify(PartyUnitRecord* self, void*, const PartyUnitRecord* source, CParty* party) {
    self->CopyItemsFrom(source, party);
    bool ok = CopiedList(List(self->weapons), List(source->weapons)) && CopiedList(List(self->quickItems), List(source->quickItems)) &&
              CopiedList(List(self->armor), List(source->armor)) && CopiedList(List(self->spells), List(source->spells));
    ei::verify::Check("PartyUnitRecord::CopyItemsFrom", true, ok, "%u weapons, %u quick, %u armour, %u spells",
                      source->weaponCount, source->quickItemCount, source->armorCount, source->spellCount);
}
EI_VERIFY(0x00660500, CopyItemsFromVerify, 0, s_unused);

// 00664950 (stolen 0): the strings equal, the bytes from +0x1C to +0x779 and +0x77C..+0x784 equal.
static PartyUnitRecord* __fastcall AssignVerify(PartyUnitRecord* self, void*, const PartyUnitRecord* source) {
    PartyUnitRecord* result = self->Assign(source);
    auto* x = reinterpret_cast<const unsigned char*>(self);
    auto* y = reinterpret_cast<const unsigned char*>(source);
    bool ok = result == self && SameTexts(*self, *source) &&
              std::memcmp(x + 0x1C, y + 0x1C, offsetof(PartyUnitRecord, unknown_779) - 0x1C) == 0 &&
              std::memcmp(x + offsetof(PartyUnitRecord, unknown_77C), y + offsetof(PartyUnitRecord, unknown_77C), 8) == 0;
    ei::verify::Check("PartyUnitRecord::Assign", true, ok, "record %p from %p", self, source);
    return result;
}
EI_VERIFY(0x00664950, AssignVerify, 0, s_unused);

// 00664490, 006645D0, 00664710 (stolen 0): the value at `index`, the array at least index + 1 long
// and still aligned.
static void __fastcall SetAtGrowVerify(AlignedPtrArray* self, void*, int index, void* value) {
    int before = self->size;
    self->SetAtGrow(index, value);
    bool ok = self->data[index] == value && self->size == (index >= before ? index + 1 : before) &&
              (reinterpret_cast<std::uintptr_t>(self->data) & 0x1F) == 0;
    ei::verify::Check("AlignedPtrArray::SetAtGrow (record list copy)", true, ok, "index %d of %d", index, before);
}
EI_VERIFY(0x00664490, SetAtGrowVerify, 0, s_unused);
EI_VERIFY(0x006645D0, SetAtGrowVerify, 0, s_unused);
EI_VERIFY(0x00664710, SetAtGrowVerify, 0, s_unused);

// 006628D0 (stolen 0): one more record, the last, with the names and key.
static PartyUnitRecord* __fastcall AddMemberVerify(CParty* self, void*, const char* name, const char* prototype, const char* extra) {
    // Copied first: the arguments may be strings of a record the array moves.
    mfc::CString nameCopy(name), prototypeCopy(prototype), extraCopy(extra);
    int before = self->members_.size;
    PartyUnitRecord* record = self->AddMember(name, prototype, extra);
    bool ok = self->members_.size == before + 1 && record == &self->members_.data[before] &&
              std::strcmp(record->texts[1], nameCopy) == 0 && std::strcmp(record->texts[5], prototypeCopy) == 0 &&
              std::strcmp(record->texts[6], prototypeCopy) == 0 && std::strcmp(record->texts[0], extraCopy) == 0 &&
              (nameCopy.GetLength() != 0 || record->unknown_01C == 0xFFFFFFFFu);
    ei::verify::Check("CParty::AddMember", true, ok, "\"%s\" (%s): %d records", static_cast<const char*>(nameCopy),
                      static_cast<const char*>(prototypeCopy), self->members_.size);
    return record;
}
EI_VERIFY(0x006628D0, AddMemberVerify, 0, s_unused);

// 006621F0 (stolen 0): 1; the lists only grew; text 6 is text 5.
static BOOL __fastcall FillFromPrototypeVerify(CParty* self, void*, PartyUnitRecord* record) {
    std::uint32_t counts[4] = {record->weaponCount, record->quickItemCount, record->armorCount, record->spellCount};
    BOOL result = self->FillFromPrototype(record);
    bool ok = result == 1 && record->weaponCount >= counts[0] && record->quickItemCount == counts[1] &&
              record->armorCount >= counts[2] && record->spellCount >= counts[3] &&
              std::strcmp(record->texts[6], record->texts[5]) == 0;
    ei::verify::Check("CParty::FillFromPrototype", true, ok, "%s: %u weapons, %u armour, %u spells",
                      static_cast<const char*>(record->texts[5]), record->weaponCount, record->armorCount, record->spellCount);
    return result;
}
EI_VERIFY(0x006621F0, FillFromPrototypeVerify, 0, s_unused);

// 006620F0 (stolen 0): 1 and one record fewer, none of the earlier ones with the key, when one had
// it; else 0 and the array unchanged.
static BOOL __fastcall RemoveMemberVerify(CParty* self, void*, int key) {
    int before = self->members_.size;
    int found = -1;
    for (int i = 0; i < before && found < 0; ++i)
        if (static_cast<int>(self->members_.data[i].unknown_01C) == key) found = i;
    BOOL result = self->RemoveMember(key);
    bool ok = found < 0 ? result == 0 && self->members_.size == before : result == 1 && self->members_.size == before - 1;
    for (int i = 0; ok && found >= 0 && i < found; ++i) ok = static_cast<int>(self->members_.data[i].unknown_01C) != key;
    ei::verify::Check("CParty::RemoveMember", true, ok, "key %08X: %d -> %d records", key, before, self->members_.size);
    return result;
}
EI_VERIFY(0x006620F0, RemoveMemberVerify, 0, s_unused);
