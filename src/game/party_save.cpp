// Saving and loading parties and their unit records, and the party copies made through a memory stream
// (operator= and the copy constructor). See party.h and docs/notes/items_spells.md ("Party").
#include "item_manager.h"
#include "party.h"

#include "chunk_archive.h"
#include "core_streams.h"
#include "object_tables.h"
#include "random.h"
#include "resource_manager.h"
#include "spell.h"
#include "unit.h"

#include "lib/containers.h"
#include "lib/crt.h"
#include "save_versions.h"

#include <cstdint>
#include <cstring>
#include <string>

namespace {

// A list's count, then each item's id (-1 for an empty entry).
void WriteItemIds(ByteStream* stream, const std::vector<Item*>& list) {
    stream->WriteValue(std::ssize(list));
    for (int i = 0; i < std::ssize(list); ++i) {
        const Item* item = list[i];
        stream->WriteValue(item != nullptr ? item->id : -1);
    }
}

// Releases what the list holds, then reads a count and that many ids, each resolved in `manager` and counted.
void ReadItemIds(ByteStream* stream, std::vector<Item*>& list, BaseManager* manager) {
    for (int i = 0; i < std::ssize(list); ++i) list[i]->Release();
    int count;
    stream->ReadValue(count);
    list.resize(count);
    for (int i = 0; i < std::ssize(list); ++i) {
        unsigned id;
        stream->ReadValue(id);
        Item* item = FindItem(manager, id);
        list[i] = item;
        if (item != nullptr) item->AddRef();
    }
}

// Versions of the save format that added fields (compared signed).
constexpr int kVersionRecord77C = 0x5B;
constexpr int kVersionRecord780 = 0x61;
constexpr int kVersionRecordText6 = 0x6B;

int Version(const ByteStream* stream) { return static_cast<int>(stream->version); }

void CopyThrough(CParty* target, CParty* source) {
    // Through an empty memory stream, freed afterwards.
    MemoryStream copy;
    source->SaveParty(&copy, 1);
    copy.MemoryStream::Seek(0);
    target->LoadParty(&copy, 1);
}

} // namespace


void PartyUnitRecord::Load(ByteStream* stream) {
    ClearItems();
    weaponIndex = 0;
    moveMode2 = 2;
    leaderId = -1;
    selected = 0;
    fightAtWill = 1;
    texts[0] = stream->ReadString();
    texts[2] = stream->ReadString();
    texts[3] = stream->ReadString();
    texts[4] = stream->ReadString();
    texts[5] = stream->ReadString();
    texts[6] = texts[5];
    stream->ReadValue(unitId);
    stats.LoadFrom(stream);
    stream->Read(&scaleX, 0xC);   // +0x728 and the height
    ReadItemIds(stream, weapons, g_loadingParty->ItemFactory());
    ReadItemIds(stream, quickItems, g_loadingParty->ItemFactory());
    ReadItemIds(stream, armor, g_loadingParty->ItemFactory());
    ReadItemIds(stream, spells, g_loadingParty->ItemFactory());
    texts[1] = stream->ReadString();
    if (Version(stream) < kVersionRecordFlags) return;
    stream->ReadValue(moveMode2);
    stream->ReadValue(weaponIndex);
    stream->ReadValue(selected);
    if (Version(stream) < kVersionRecord77C) return;
    stream->ReadValue(leaderId);
    if (Version(stream) < kVersionRecord780) return;
    stream->ReadValue(fightAtWill);
    if (Version(stream) < kVersionRecordText6) return;
    texts[6] = stream->ReadString();
}

void PartyUnitRecord::Save(ByteStream* stream) {
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

void CParty::LoadParty(ByteStream* stream, int loadVariables) {
    if (itemManager == nullptr) Create();
    g_loadingParty = this;
    Clear(loadVariables, 0);
    zone = stream->ReadString();
    stream->ReadValue(entrance);
    itemManager->Load(stream);
    spellManager->Load(stream);
    itemManager->LoadObjects(stream);
    spellManager->LoadObjects(stream);
    ReadItemIds(stream, items, ItemFactory());
    int records;
    stream->ReadValue(records);
    members.resize(records);
    for (int i = 0; i < records; ++i) members[i].Load(stream);
    if (loadVariables != 0) variables.Load(stream);
    g_loadingParty = nullptr;
    money = 0;
    stream->ReadValue(money);
    if (Version(stream) > kVersionPartyKey) {
        std::uint32_t key;
        stream->ReadValue(key);
        money ^= key;
    }
    itemManager->ReleaseUnused();
    spellManager->ReleaseUnused();
}

void CParty::SaveParty(ByteStream* stream, int saveVariables) {
    if (itemManager == nullptr) Create();
    stream->WriteString(zone);
    stream->WriteValue(entrance);
    itemManager->Save(stream);
    spellManager->Save(stream);
    itemManager->SaveObjects(stream);
    spellManager->SaveObjects(stream);
    WriteItemIds(stream, items);
    int records = lib::Count(members);
    stream->WriteValue(records);
    for (int i = 0; i < records; ++i) members[i].Save(stream);
    if (saveVariables != 0) variables.Save(stream);
    std::uint32_t key = g_random.Next();
    stream->WriteValue(money ^ key);
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
    MemoryStream held;
    if (ar->loading) {
        ar->StreamField(kStreamField, &held);
        held.version = static_cast<std::uint32_t>(ar->version);
        LoadParty(&held, 1);
    } else {
        SaveParty(&held, 1);
        ar->StreamField(kStreamField, &held);
    }
}
