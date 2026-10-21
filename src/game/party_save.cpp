// Saving and loading parties and their unit records, and the party copies made through a memory
// stream (operator= and the copy constructor). See party.h and docs/notes/items_spells.md
// ("Party").
#include "party.h"

#include "chunk_archive.h"
#include "core_streams.h"
#include "object_tables.h"
#include "originals_engine.h"
#include "originals_units.h"
#include "random.h"
#include "resource_manager.h"
#include "spell.h"
#include "unit.h"

#include "lib/crt.h"

#include "ei/hook.h"

#include <cstdint>
#include <cstring>

namespace {

void WriteText(CStream* stream, const char* text) {
    stream->Write(text, static_cast<std::uint32_t>(std::strlen(text) + 1));
}

template <typename T>
void WriteValue(CStream* stream, T value) {
    stream->Write(&value, sizeof(value));
}

void ReadText(CStream* stream, mfc::CString& text) {
    stream->ReadInto(&text);
}

// A list's count, then each item's id (-1 for an empty entry).
void WriteItemIds(CStream* stream, const PartyItemArray& list) {
    WriteValue(stream, list.size);
    for (int i = 0; i < list.size; ++i) {
        const CItem* item = list.data[i];
        WriteValue(stream, item != nullptr ? item->id_ : -1);
    }
}

// Releases what the list holds, then reads a count and that many ids, each resolved in `manager`
// and counted.
void ReadItemIds(CStream* stream, PartyItemArray& list, CBaseManager* manager) {
    for (int i = 0; i < list.size; ++i) list.data[i]->Release();
    int count;
    stream->Read(&count, sizeof(count));
    list.SetSize(count, -1);
    for (int i = 0; i < list.size; ++i) {
        unsigned id;
        stream->Read(&id, sizeof(id));
        auto* item = static_cast<CItem*>(manager->FindObject(id));
        list.data[i] = item;
        if (item != nullptr) ++item->refCount_;
    }
}

// Versions of the save format that added fields (compared signed).
constexpr int kVersionRecordFlags = 0x51;
constexpr int kVersionRecord77C = 0x5B;
constexpr int kVersionRecord780 = 0x61;
constexpr int kVersionRecordText6 = 0x6B;
constexpr int kVersionPartyKey = 0x6B;   // the key follows +0x50 above this version

int Version(const CStream* stream) { return static_cast<int>(stream->version_); }

// The stream operator= and the copy constructor go through: an empty memory stream, freed
// afterwards (the original builds and destroys it inline).
struct CopyStream {
    CopyStream() { stream.Construct(); }
    ~CopyStream() { stream.Destruct(); }
    CMemoryStream stream;
};

void CopyThrough(CParty* target, CParty* source) {
    CopyStream copy;
    source->SaveParty(&copy.stream, 1);
    copy.stream.CMemoryStream::SeekImpl(0);
    target->LoadParty(&copy.stream, 1);
}

} // namespace

// --- PartyUnitRecord ----------------------------------------------------------------------------

void PartyUnitRecord::Load(CStream* stream) {
    ClearItems();
    weaponIndex = 0;
    moveMode2 = 2;
    leaderId = -1;
    selected = 0;
    fightAtWill = 1;
    ReadText(stream, texts[0]);
    ReadText(stream, texts[2]);
    ReadText(stream, texts[3]);
    ReadText(stream, texts[4]);
    ReadText(stream, texts[5]);
    texts[6] = texts[5];
    stream->Read(&unitId, sizeof(unitId));
    reinterpret_cast<UnitStats*>(stats)->LoadFrom(stream);
    stream->Read(&scaleX, 0xC);   // +0x724, +0x728 and the height
    ReadItemIds(stream, ItemList(weapons), LoadingParty()->ItemManager());
    ReadItemIds(stream, ItemList(quickItems), LoadingParty()->ItemManager());
    ReadItemIds(stream, ItemList(armor), LoadingParty()->ItemManager());
    ReadItemIds(stream, ItemList(spells), LoadingParty()->ItemManager());
    ReadText(stream, texts[1]);
    if (Version(stream) < kVersionRecordFlags) return;
    stream->Read(&moveMode2, 4);
    stream->Read(&weaponIndex, 4);
    stream->Read(&selected, 1);
    if (Version(stream) < kVersionRecord77C) return;
    stream->Read(&leaderId, 4);
    if (Version(stream) < kVersionRecord780) return;
    stream->Read(&fightAtWill, 4);
    if (Version(stream) < kVersionRecordText6) return;
    ReadText(stream, texts[6]);
}
EI_REPLACE_METHOD(0x0065F9B0, PartyUnitRecord::Load);

void PartyUnitRecord::Save(CStream* stream) {
    WriteText(stream, texts[0]);
    WriteText(stream, texts[2]);
    WriteText(stream, texts[3]);
    WriteText(stream, texts[4]);
    WriteText(stream, texts[5]);
    WriteValue(stream, unitId);
    reinterpret_cast<UnitStats*>(stats)->SaveTo(stream);
    stream->Write(&scaleX, 0xC);
    WriteItemIds(stream, ItemList(weapons));
    WriteItemIds(stream, ItemList(quickItems));
    WriteItemIds(stream, ItemList(armor));
    WriteItemIds(stream, ItemList(spells));
    WriteText(stream, texts[1]);
    stream->Write(&moveMode2, 4);
    WriteValue(stream, weaponIndex);
    WriteValue(stream, selected);
    WriteValue(stream, leaderId);
    stream->Write(&fightAtWill, 4);
    WriteText(stream, texts[6]);
}
EI_REPLACE_METHOD(0x0065FDE0, PartyUnitRecord::Save);

// --- CParty -------------------------------------------------------------------------------------

void CParty::LoadParty(CStream* stream, int loadVariables) {
    if (itemManager_ == nullptr) Create();
    LoadingParty() = this;
    Clear(loadVariables, 0);
    ReadText(stream, zone_);
    stream->Read(&entrance_, 4);
    itemManager_->Load(stream);
    spellManager_->Load(stream);
    itemManager_->LoadObjects(stream);
    spellManager_->LoadObjects(stream);
    ReadItemIds(stream, items_, ItemManager());
    int records;
    stream->Read(&records, sizeof(records));
    members_.SetSize(records, -1);
    for (int i = 0; i < records; ++i) members_.data[i].Load(stream);
    if (loadVariables != 0) variables_.Load(stream);
    LoadingParty() = nullptr;
    money_ = 0;
    stream->Read(&money_, 4);
    if (Version(stream) > kVersionPartyKey) {
        std::uint32_t key;
        stream->Read(&key, sizeof(key));
        money_ ^= key;
    }
    itemManager_->ReleaseUnused();
    spellManager_->ReleaseUnused();
}
EI_REPLACE_METHOD(0x006618C0, CParty::LoadParty);

void CParty::SaveParty(CStream* stream, int saveVariables) {
    if (itemManager_ == nullptr) Create();
    WriteText(stream, zone_);
    WriteValue(stream, entrance_);
    itemManager_->Save(stream);
    spellManager_->Save(stream);
    itemManager_->SaveObjects(stream);
    spellManager_->SaveObjects(stream);
    WriteItemIds(stream, items_);
    int records = members_.size;
    WriteValue(stream, records);
    for (int i = 0; i < records; ++i) members_.data[i].Save(stream);
    if (saveVariables != 0) variables_.Save(stream);
    std::uint32_t key = IsaacRandom::Global().Next();
    WriteValue(stream, money_ ^ key);
    WriteValue(stream, key);
}
EI_REPLACE_METHOD(0x00661C10, CParty::SaveParty);

CParty* CParty::Assign(CParty* source) {
    Clear(1, 0);
    CopyThrough(this, source);
    return this;
}
EI_REPLACE_METHOD(0x00661300, CParty::Assign);

CParty* CParty::CopyConstruct(CParty* source) {
    Construct();
    CopyThrough(this, source);
    return this;
}
EI_REPLACE_METHOD(0x006613C0, CParty::CopyConstruct);

// --- 006617F0 (A-H25): the party as a stream field of an archive --------------------------------------

void CParty::ArchiveParty(ChunkArchive* ar) {
    constexpr int kStreamField = 1;
    CopyStream held;
    if (ar->loading_) {
        ar->StreamField(kStreamField, &held.stream);
        held.stream.version_ = static_cast<std::uint32_t>(ar->version_);
        LoadParty(&held.stream, 1);
    } else {
        SaveParty(&held.stream, 1);
        ar->StreamField(kStreamField, &held.stream);
    }
}
EI_REPLACE_METHOD(0x006617F0, CParty::ArchiveParty);
