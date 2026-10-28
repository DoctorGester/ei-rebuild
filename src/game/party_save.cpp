// Saving and loading parties and their unit records, and the party copies made through a memory stream
// (operator= and the copy constructor). See party.h and docs/notes/items_spells.md ("Party").
#include "party.h"

#include "chunk_archive.h"
#include "core_streams.h"
#include "object_tables.h"
#include "random.h"
#include "resource_manager.h"
#include "spell.h"
#include "unit.h"

#include "lib/crt.h"

#include <cstdint>
#include <cstring>
#include <string>

namespace {

template <typename Text>
void ReadText(CStream* stream, Text& text) {
    stream->ReadInto(&text);
}

// A list's count, then each item's id (-1 for an empty entry).
void WriteItemIds(CStream* stream, const std::vector<CItem*>& list) {
    stream->WriteValue(std::ssize(list));
    for (int i = 0; i < std::ssize(list); ++i) {
        const CItem* item = list[i];
        stream->WriteValue(item != nullptr ? item->id_ : -1);
    }
}

// Releases what the list holds, then reads a count and that many ids, each resolved in `manager` and counted.
void ReadItemIds(CStream* stream, std::vector<CItem*>& list, CBaseManager* manager) {
    for (int i = 0; i < std::ssize(list); ++i) list[i]->Release();
    int count;
    stream->ReadValue(count);
    list.resize(count);
    for (int i = 0; i < std::ssize(list); ++i) {
        unsigned id;
        stream->ReadValue(id);
        CItem* item = FindItem(manager, id);
        list[i] = item;
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

void CopyThrough(CParty* target, CParty* source) {
    // Through an empty memory stream, freed afterwards.
    CMemoryStream copy;
    source->SaveParty(&copy, 1);
    copy.CMemoryStream::Seek(0);
    target->LoadParty(&copy, 1);
}

} // namespace


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
    stream->ReadValue(unitId);
    stats.LoadFrom(stream);
    stream->Read(&scaleX, 0xC);   // +0x728 and the height
    ReadItemIds(stream, weapons, g_loadingParty->ItemFactory());
    ReadItemIds(stream, quickItems, g_loadingParty->ItemFactory());
    ReadItemIds(stream, armor, g_loadingParty->ItemFactory());
    ReadItemIds(stream, spells, g_loadingParty->ItemFactory());
    ReadText(stream, texts[1]);
    if (Version(stream) < kVersionRecordFlags) return;
    stream->ReadValue(moveMode2);
    stream->ReadValue(weaponIndex);
    stream->ReadValue(selected);
    if (Version(stream) < kVersionRecord77C) return;
    stream->ReadValue(leaderId);
    if (Version(stream) < kVersionRecord780) return;
    stream->ReadValue(fightAtWill);
    if (Version(stream) < kVersionRecordText6) return;
    ReadText(stream, texts[6]);
}

void PartyUnitRecord::Save(CStream* stream) {
    stream->WriteString(texts[0]);
    stream->WriteString(texts[2]);
    stream->WriteString(texts[3]);
    stream->WriteString(texts[4]);
    stream->WriteString(texts[5]);
    stream->WriteValue(unitId);
    stats.SaveTo(stream);
    stream->Write(&scaleX, 0xC);
    WriteItemIds(stream, weapons);
    WriteItemIds(stream, quickItems);
    WriteItemIds(stream, armor);
    WriteItemIds(stream, spells);
    stream->WriteString(texts[1]);
    stream->WriteValue(moveMode2);
    stream->WriteValue(weaponIndex);
    stream->WriteValue(selected);
    stream->WriteValue(leaderId);
    stream->WriteValue(fightAtWill);
    stream->WriteString(texts[6]);
}

void CParty::LoadParty(CStream* stream, int loadVariables) {
    if (itemManager_ == nullptr) Create();
    g_loadingParty = this;
    Clear(loadVariables, 0);
    ReadText(stream, zone_);
    stream->ReadValue(entrance_);
    itemManager_->Load(stream);
    spellManager_->Load(stream);
    itemManager_->LoadObjects(stream);
    spellManager_->LoadObjects(stream);
    ReadItemIds(stream, items_, ItemFactory());
    int records;
    stream->ReadValue(records);
    members_.resize(records);
    for (int i = 0; i < records; ++i) members_[i].Load(stream);
    if (loadVariables != 0) variables_.Load(stream);
    g_loadingParty = nullptr;
    money_ = 0;
    stream->ReadValue(money_);
    if (Version(stream) > kVersionPartyKey) {
        std::uint32_t key;
        stream->ReadValue(key);
        money_ ^= key;
    }
    itemManager_->ReleaseUnused();
    spellManager_->ReleaseUnused();
}

void CParty::SaveParty(CStream* stream, int saveVariables) {
    if (itemManager_ == nullptr) Create();
    stream->WriteString(zone_);
    stream->WriteValue(entrance_);
    itemManager_->Save(stream);
    spellManager_->Save(stream);
    itemManager_->SaveObjects(stream);
    spellManager_->SaveObjects(stream);
    WriteItemIds(stream, items_);
    int records = std::ssize(members_);
    stream->WriteValue(records);
    for (int i = 0; i < records; ++i) members_[i].Save(stream);
    if (saveVariables != 0) variables_.Save(stream);
    std::uint32_t key = g_random.Next();
    stream->WriteValue(money_ ^ key);
    stream->WriteValue(key);
}

CParty* CParty::Assign(CParty* source) {
    Clear(1, 0);
    CopyThrough(this, source);
    return this;
}

CParty::CParty(CParty& source) : CParty() { CopyThrough(this, &source); }

// --- The party as a stream field of an archive --------------------------------------

void CParty::ArchiveParty(ChunkArchive* ar) {
    constexpr int kStreamField = 1;
    CMemoryStream held;
    if (ar->loading_) {
        ar->StreamField(kStreamField, &held);
        held.version_ = static_cast<std::uint32_t>(ar->version_);
        LoadParty(&held, 1);
    } else {
        SaveParty(&held, 1);
        ar->StreamField(kStreamField, &held);
    }
}
