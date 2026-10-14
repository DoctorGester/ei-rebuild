// Checks for party_save.cpp. Every new game copies the party into the named party records
// (004A8800 -> CParty::Assign: SaveParty and LoadParty through a memory stream, each record's Save
// and Load); saving a game (scenarios ui_saveload, party_records) writes the parties, loading reads them.
// - PartyUnitRecord::Save and CParty::SaveParty: into a memory stream, the original into a
//   scratch memory stream first and ours into the real one; the bytes compared. SaveParty draws
//   one ISAAC key: the generator is put back to its state before ours runs. Other streams, and a
//   party without managers (Create runs), run ours only.
// - PartyUnitRecord::Load: ours, then (memory stream, version 0x6B or later) the original record
//   Save into a scratch stream must give back the bytes Load consumed.
// - CParty::LoadParty: ours only; every item of the party and of its records is registered in
//   the party's item manager under its id, and the loading mark is cleared.
// - CParty::Assign and CopyConstruct: ours only; the copy has the source's zone, +0x50, +0x6C,
//   item ids, variable count and records (texts, stats, values and item ids).
#include "verify.h"

#include "../../src/game/core_streams.h"
#include "../../src/game/party.h"
#include "../../src/game/random.h"
#include "../../src/game/resource_manager.h"
#include "../../src/game/unit.h"
#include "../../src/lib/crt.h"

#include <cstdint>
#include <cstring>

namespace {

void* s_unused;

std::uintptr_t VtableOf(const void* object) {
    std::uintptr_t vtable;
    std::memcpy(&vtable, object, sizeof(vtable));
    return vtable;
}

// The second memory stream vtable (the network queue's streams; the same slots).
constexpr std::uintptr_t kQueueStreamVtable = std::uintptr_t(0x0073C960);

CMemoryStream* AsMemoryStream(CStream* stream) {
    std::uintptr_t vtable = VtableOf(stream);
    if (vtable != CMemoryStream::kVtableAddress && vtable != kQueueStreamVtable) return nullptr;
    return static_cast<CMemoryStream*>(stream);
}

CMemoryStream* NewScratchStream(const CStream* like) {
    auto* scratch = static_cast<CMemoryStream*>(gamecrt::operator_new(sizeof(CMemoryStream)));
    scratch->Construct();
    scratch->version_ = like->version_;
    return scratch;
}

void DeleteScratchStream(CMemoryStream* scratch) {
    scratch->Destruct();
    gamecrt::operator_delete(scratch);
}

// The bytes `real` got from `start` on equal the scratch stream's.
bool SameBytes(const CMemoryStream* real, std::uint32_t start, const CMemoryStream* scratch) {
    std::uint32_t written = real->position_ - start;
    return written == scratch->size_ && (written == 0 || std::memcmp(real->data_ + start, scratch->data_, written) == 0);
}

// 0065FDE0: PUSH ECX; PUSH EBX; MOV EBX,ECX; PUSH EBP; PUSH ESI (6).
void(__fastcall* s_recordSave)(PartyUnitRecord*, void*, CStream*);
void __fastcall RecordSaveVerify(PartyUnitRecord* self, void*, CStream* stream) {
    CMemoryStream* real = AsMemoryStream(stream);
    if (real == nullptr) {
        self->Save(stream);
        return;
    }
    CMemoryStream* scratch = NewScratchStream(stream);
    s_recordSave(self, nullptr, scratch);
    std::uint32_t start = real->position_;
    self->Save(stream);
    ei::verify::Check("PartyUnitRecord::Save", true, SameBytes(real, start, scratch), "record %p: %u bytes, original %u",
                      self, real->position_ - start, scratch->size_);
    DeleteScratchStream(scratch);
}

void __fastcall RecordLoadVerify(PartyUnitRecord* self, void*, CStream* stream) {
    CMemoryStream* real = AsMemoryStream(stream);
    std::uint32_t start = real != nullptr ? real->position_ : 0;
    self->Load(stream);
    if (real == nullptr || static_cast<int>(stream->version_) < 0x6B || !stream->ok_) return;
    CMemoryStream* scratch = NewScratchStream(stream);
    s_recordSave(self, nullptr, scratch);
    ei::verify::Check("PartyUnitRecord::Load", true, SameBytes(real, start, scratch),
                      "record %p: %u bytes read, saved again %u", self, real->position_ - start, scratch->size_);
    DeleteScratchStream(scratch);
}

// 00661C10: PUSH ECX; PUSH EBX; MOV EBX,ECX; PUSH ESI; PUSH EDI (6).
void(__fastcall* s_partySave)(CParty*, void*, CStream*, int);
void __fastcall PartySaveVerify(CParty* self, void*, CStream* stream, int saveVariables) {
    CMemoryStream* real = AsMemoryStream(stream);
    if (real == nullptr || self->itemManager_ == nullptr) {
        self->SaveParty(stream, saveVariables);
        return;
    }
    auto* random = static_cast<IsaacRandom*>(gamecrt::operator_new(sizeof(IsaacRandom)));
    std::memcpy(random, &IsaacRandom::Global(), sizeof(IsaacRandom));
    CMemoryStream* scratch = NewScratchStream(stream);
    s_partySave(self, nullptr, scratch, saveVariables);
    std::memcpy(&IsaacRandom::Global(), random, sizeof(IsaacRandom));
    std::uint32_t start = real->position_;
    self->SaveParty(stream, saveVariables);
    ei::verify::Check("CParty::SaveParty", true, SameBytes(real, start, scratch), "party %p: %u bytes, original %u", self,
                      real->position_ - start, scratch->size_);
    DeleteScratchStream(scratch);
    gamecrt::operator_delete(random);
}

// Every entry of `list` is the item manager's object under its id.
bool Registered(const AlignedPtrArray& list, const CBaseManager* manager) {
    for (int i = 0; i < list.size; ++i) {
        auto* item = static_cast<CEngineObject*>(list.data[i]);
        if (item != nullptr && manager->FindObject(static_cast<unsigned>(item->id_)) != item) return false;
    }
    return true;
}

bool RecordItemsRegistered(PartyUnitRecord& record, const CBaseManager* manager) {
    return Registered(ItemList(record.weapons), manager) && Registered(ItemList(record.quickItems), manager) &&
           Registered(ItemList(record.armor), manager) && Registered(ItemList(record.spells), manager);
}

void __fastcall PartyLoadVerify(CParty* self, void*, CStream* stream, int loadVariables) {
    self->LoadParty(stream, loadVariables);
    bool ok = LoadingParty() == nullptr && self->itemManager_ != nullptr && Registered(self->items_, self->itemManager_);
    for (int i = 0; ok && i < self->members_.size; ++i) ok = RecordItemsRegistered(self->members_.data[i], self->itemManager_);
    ei::verify::Check("CParty::LoadParty", true, ok, "party %p: %d items, %d records", self, self->items_.size,
                      self->members_.size);
}

bool SameIds(const AlignedPtrArray& a, const AlignedPtrArray& b) {
    if (a.size != b.size) return false;
    for (int i = 0; i < a.size; ++i) {
        auto* x = static_cast<const CEngineObject*>(a.data[i]);
        auto* y = static_cast<const CEngineObject*>(b.data[i]);
        if ((x != nullptr ? x->id_ : -1) != (y != nullptr ? y->id_ : -1)) return false;
    }
    return true;
}

bool SameRecord(PartyUnitRecord& a, PartyUnitRecord& b) {
    for (int i = 0; i < 7; ++i)
        if (std::strcmp(a.texts[i], b.texts[i]) != 0) return false;
    return a.unitId == b.unitId && std::memcmp(a.stats, b.stats, sizeof(a.stats)) == 0 &&
           std::memcmp(&a.scaleX, &b.scaleX, 0xC) == 0 && SameIds(ItemList(a.weapons), ItemList(b.weapons)) &&
           SameIds(ItemList(a.quickItems), ItemList(b.quickItems)) && SameIds(ItemList(a.armor), ItemList(b.armor)) &&
           SameIds(ItemList(a.spells), ItemList(b.spells)) && a.moveMode2 == b.moveMode2 &&
           a.weaponIndex == b.weaponIndex && a.selected == b.selected && a.leaderId == b.leaderId &&
           a.fightAtWill == b.fightAtWill;
}

bool SameParty(CParty* copy, CParty* source) {
    bool ok = std::strcmp(copy->zone_, source->zone_) == 0 && copy->money_ == source->money_ &&
              copy->entrance_ == source->entrance_ && SameIds(copy->items_, source->items_) &&
              copy->variables_.Size() == source->variables_.Size() && copy->members_.size == source->members_.size;
    for (int i = 0; ok && i < copy->members_.size; ++i) ok = SameRecord(copy->members_.data[i], source->members_.data[i]);
    return ok;
}

CParty* __fastcall AssignVerify(CParty* self, void*, CParty* source) {
    CParty* result = self->Assign(source);
    ei::verify::Check("CParty::Assign", true, result == self && (source == self || SameParty(self, source)),
                      "party %p from %p", self, source);
    return result;
}

CParty* __fastcall CopyConstructVerify(CParty* self, void*, CParty* source) {
    CParty* result = self->CopyConstruct(source);
    ei::verify::Check("CParty::CopyConstruct", true, result == self && VtableOf(self) == CParty::kVtableAddress &&
                                                          SameParty(self, source),
                      "party %p from %p", self, source);
    return result;
}

} // namespace

EI_VERIFY(0x0065FDE0, RecordSaveVerify, 6, s_recordSave);
EI_VERIFY(0x0065F9B0, RecordLoadVerify, 0, s_unused);
EI_VERIFY(0x00661C10, PartySaveVerify, 6, s_partySave);
EI_VERIFY(0x006618C0, PartyLoadVerify, 0, s_unused);
EI_VERIFY(0x00661300, AssignVerify, 0, s_unused);
EI_VERIFY(0x006613C0, CopyConstructVerify, 0, s_unused);

// --- 006617F0 (A-H25): CParty::ArchiveParty -----------------------------------------------------------
// It writes a field into the archive or loads the party from one (saving and loading a game: the
// save scenarios), and SaveParty draws a fresh key each time, so ours only: the archive keeps its
// direction and version.
#include "../../src/game/chunk_archive.h"

static void __fastcall ArchivePartyVerify(CParty* self, void*, ChunkArchive* ar) {
    const std::uint8_t loading = ar->loading_;
    const std::int32_t version = ar->version_;
    self->ArchiveParty(ar);
    ei::verify::Check("CParty::ArchiveParty", true, ar->loading_ == loading && ar->version_ == version, "loading %d",
                      loading);
}
static void* s_archivePartyUnused;
EI_VERIFY(0x006617F0, ArchivePartyVerify, 0, s_archivePartyUnused);
