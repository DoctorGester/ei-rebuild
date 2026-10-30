// The party manager's save and load and the archive chunks they write: the parties by key, the entries, the
// class templates and the named records. See party_manager.h and docs/notes/script.md ("Leftovers").
#include "item_stock.h"

#include "chunk_archive.h"
#include "class_templates.h"
#include "core_refcount.h"
#include "core_streams.h"
#include "game_singletons.h"
#include "game_strings.h"
#include "object_tables.h"
#include "party.h"
#include "party_manager.h"
#include "party_records.h"
#include "player_messages.h"
#include "resource_local.h"   // FillStream
#include "lib/str.h"

#include <bit>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace {

// Counted pointers as chunk `id`'s fields 1, object references (each the field's next occurrence): loading
// drops the old ones and resizes to the number of fields first.
template <typename T>
void ObjectRefFields(ChunkArchive* ar, int id, std::vector<HeldRef<T>>* objects) {
    if (!ar->EnterChunk(id)) return;
    int count;
    if (ar->loading) {
        objects->clear();
        count = ar->CountChunks(1);
        objects->resize(static_cast<std::size_t>(count));
    } else {
        count = static_cast<int>(objects->size());
    }
    for (int i = 0; i < count; ++i) {
        ar->Current()->occurrence = i + 1;
        HeldRef<T>& slot = (*objects)[static_cast<std::size_t>(i)];
        if (ar->loading) slot = static_cast<T*>(ar->ObjectRef(1));
        else ar->SaveObjectRef(1, slot);
    }
    ar->LeaveChunk();
}

// `to` becomes a copy of `from` (with variables) through a memory stream, as Party::Assign without its Clear
// inlines it.
void CopyParty(Party* to, Party* from) {
    MemoryStream local;
    from->SaveParty(&local, 1);
    local.MemoryStream::Seek(0);
    to->LoadParty(&local, 1);
}

// The archive's contents, the same calls for both directions.
void ArchiveManager(PartyManager* manager, ChunkArchive* ar) {
    ar->SerializeParties(1, &manager->parties);
    ar->SerializePartyEntries(2, &manager->entries);
    ar->Field(3, &manager->unreadField);
    if (ar->EnterChunk(4)) {
        manager->templates.Serialize(ar, 1);
        ar->LeaveChunk();
    }
    if (ar->EnterChunk(5)) {
        manager->records.Serialize(ar);
        ar->LeaveChunk();
    }
}

} // namespace

// ---- Archive chunks -------------------------------------------------------------------------------

void ChunkArchive::IntField(int id, int* value) { Field(id, value, 4); }

void ChunkArchive::SerializeParties(int id, PartyMap* parties) {
    if (!EnterChunk(id)) return;
    if (loading) {
        parties->clear();
        int count = CountChunks(1);
        for (int i = 1; i <= count; ++i) {
            int key = 0;
            Current()->occurrence = i;
            if (EnterChunk(1)) {
                Field(1, &key);
                LeaveChunk();
            }
            Party* party = PartyAt(*parties, key);
            if (EnterChunk(2)) {
                party->ArchiveParty(this);
                LeaveChunk();
            }
        }
    } else {
        for (auto& [stored, party] : *parties) {
            int key = stored;
            if (EnterChunk(1)) {
                Field(1, &key);
                LeaveChunk();
            }
            if (EnterChunk(2)) {
                party.ArchiveParty(this);
                LeaveChunk();
            }
        }
    }
    LeaveChunk();
}

void ChunkArchive::SerializePartyEntries(int id, std::vector<PartyManagerEntry>* entries) {
    RecordChunks(this, id, entries, [](PartyManagerEntry& entry, ChunkArchive* ar) {
        ar->StringField(1, &entry.name);
        ar->StringField(2, &entry.text);
        if (ar->EnterChunk(3)) {
            std::vector<int>& ids = entry.ids;
            int idCount;
            if (ar->loading) {
                ids.clear();
                idCount = ar->CountChunks(1);
                ids.resize(static_cast<unsigned>(idCount), 0);
            } else {
                idCount = static_cast<int>(ids.size());
            }
            for (int j = 0; j < idCount; ++j) {
                ar->Current()->occurrence = j + 1;
                if (ar->EnterChunk(1)) {
                    ar->IntField(1, &ids[j]);
                    ar->LeaveChunk();
                }
            }
            ar->LeaveChunk();
        }
    });
}

void TemplateTable::Serialize(ChunkArchive* ar, int id) {
    ObjectRefFields(ar, id, &templates);
}

void NamedPartyRecords::Serialize(ChunkArchive* ar) {
    ObjectRefFields(ar, 1, &records);
    ar->StringField(2, &requestedName);
    ar->StringField(3, &currentName);
}

// ---- The archived objects (Serialize) ---------------------------------------------------

void PartyRecord::Serialize(ChunkArchive* ar) {
    MemoryStream local;
    ar->StringField(1, &name);
    Party* p = GetParty();
    if (!ar->loading) {
        p->SaveParty(&local, 1);
        ar->StreamField(2, &local);
    } else {
        ar->StreamField(2, &local);
        local.version = static_cast<std::uint32_t>(ar->version);
        p->LoadParty(&local, 1);
    }
}

void ClassTemplate::Serialize(ChunkArchive* ar) {
    ar->Field(1, &type);
    ar->Field(2, &sellsItems);
    ar->Field(3, &sellsSpells);
    ar->Field(4, weights, sizeof(weights) + sizeof(weightsTail));
    MemoryStream local;
    Party* itemsParty = ItemsParty();
    Party* spellsParty = SpellsParty();
    if (!ar->loading) {
        itemsParty->SaveParty(&local, 0);
        spellsParty->SaveParty(&local, 0);
        allowed.SaveLists(&local);
        magic.SaveLists(&local);
        ar->StreamField(5, &local);
    } else {
        ar->StreamField(5, &local);
        local.version = static_cast<std::uint32_t>(ar->version);
        itemsParty->LoadParty(&local, 0);
        spellsParty->LoadParty(&local, 0);
        allowed.LoadLists(&local);
        magic.LoadLists(&local);
    }
    ar->Field(6, &restockDue);
}

void NamedPartyRecords::BeginRedeploy(Party* party) {
    if (Party* found = Find(currentName)) CopyParty(found, party);
    NamedFloatTable variables(party->variables);
    std::string zone = party->zone;
    for (PartyRecord* record : records) {
        Party* target = record->GetParty();
        target->variables = variables;
        target->zone = zone;
        target->entrance = party->entrance;
    }
}

void NamedPartyRecords::EndRedeploy(Party* party) {
    if (Party* found = Find(requestedName)) CopyParty(party, found);
    else party->Clear(1, 0);
    currentName = requestedName;
}

// ---- Load and save --------------------------------------------------------------------------------

void PartyManager::Load(MemoryStream* from) {
    MemoryStream local;
    from->ReadSized(&local);
    local.MemoryStream::Seek(0);
    ChunkArchive archive;
    archive.Open(&local, 1, static_cast<int>(from->version));
    ArchiveManager(this, &archive);
    archive.Close(&local);
}

void PartyManager::Save(MemoryStream* to) {
    MemoryStream local;
    ChunkArchive archive;
    archive.Open(&local, 0, static_cast<int>(to->version));
    ArchiveManager(this, &archive);
    archive.Close(&local);
    to->WriteSized(&local);
}

// --- The class registry's creators: id 1 a class template, id 2 a named party record (made when a saved
// party manager is loaded) --------------------------------------------------------------------------------

RefCounted* CreatePartyRecord() {
    auto* record = new PartyRecord();
    record->refs = 0;
    record->flags6 = 0;
    record->flags7 = 0;
    return record;
}

RefCounted* CreateClassTemplate() {
    auto* created = new ClassTemplate();
    created->refs = 0;
    created->flags6 = 0;
    created->flags7 = 0;
    return created;
}

// --- The party record ------------------------------------------------------------------------------

PartyRecord::PartyRecord() { party.Create(); }

PartyRecord::~PartyRecord() { party.Destroy(); }

// --- The manager (the C runtime runs its destructor at exit) -------------------------------------------

PartyManager g_partyManager;

// --- Party records -----------------------------------------------------------------------------------

// The named party records of the party manager: reset at new game, and the script commands' helpers to
// find, add and remove a record by name. See party_records.h and docs/notes/world.md ("Party records and
// class templates").

namespace {

const char* const kHero = "Hero";
const char* const kHumanHero = "Human Hero";
const char* const kFirstZone = "gz1g";
const char* const kGameTime = "gtime";
constexpr float kSix = 6.0f;

// A new record with no references.
PartyRecord* NewRecord() {
    auto* record = new PartyRecord();
    record->refs = 0;
    record->flags6 = 0;
    record->flags7 = 0;
    return record;
}

// The party stored under `key` (find, inlined), null if none. Not const: the parties' copies read their source
// non-const.
Party* FindParty(const PartyMap& parties, int key) {
    auto found = parties.find(key);
    return found != parties.end() ? const_cast<Party*>(&found->second) : nullptr;
}

} // namespace

void PartyRecordsReset(NamedPartyRecords* list) {
    list->records.clear();
    list->requestedName = game::kEmptyText;
    list->currentName = game::kEmptyText;

    list->records.emplace_back(NewRecord());

    Party* party = list->records.front()->GetParty();
    party->AddMember(kHero, kHumanHero, game::kEmptyText);
    party->zone = kFirstZone;
    party->entrance = 0;
    party->variables.Set(kGameTime, kSix);
}

Party* NamedPartyRecords::Find(std::string_view name) const {
    Party* unnamed = nullptr;
    for (PartyRecord* record : records) {
        if (str::Compare(record->name.c_str(), name) == 0) return record->GetParty();
        if (str::Compare(record->name.c_str(), game::kEmptyText) == 0) unnamed = record->GetParty();
    }
    return unnamed;
}

void NamedPartyRecords::Add(const char* name) {
    PartyRecord* record = NewRecord();
    record->name = name;
    records.emplace_back(record);
}

void NamedPartyRecords::Remove(const char* name) {
    for (unsigned i = 0; i < static_cast<unsigned>(records.size()); ++i) {
        if (str::Compare(records[i]->name.c_str(), name) != 0) continue;
        // The index still advances, so the record moved into slot i is not tested (original behaviour).
        records.erase(records.begin() + i);
    }
}

// ---- The party manager -----------------------------------------------------------------

Party* PartyAt(PartyMap& parties, int key) {
    Party made;
    Party pair(made);
    return &parties.try_emplace(key, pair).first->second;
}

void PartyManagerNewGame(PartyManager* manager) {
    PartyRecordsReset(&manager->records);
    manager->templates.Build(0);
}

void PartyManagerNewGameFour(PartyManager* manager) {
    PartyRecordsReset(&manager->records);
    manager->templates.Build(1);
}

void PartyManager::BeginRedeploy(Party* party) { records.BeginRedeploy(party); }

void PartyManager::EndRedeploy(Party* party) { records.EndRedeploy(party); }

int g_nextPartyId = /*0078B0E8*/ 1;

int PartyNextId() { return g_nextPartyId++; }

bool PartyManager::LoadParty(const int* key, Party* target) const {
    if (*key == 0) {
        target->Clear(1, 0);
        return true;
    }
    Party* stored = FindParty(parties, *key);
    if (!stored) return false;
    target->Assign(stored);
    return true;
}

void PartyManager::StoreParty(const int* key, Party* source) {
    Party* value = FindParty(parties, *key);
    if (!value) value = PartyAt(parties, *key);
    value->Assign(source);
}

// --- Party script ------------------------------------------------------------------------------------

// The party a script names (see party_records.h). Names are provisional.

namespace {

// The party manager's record list.
const NamedPartyRecords* RecordList() { return &g_partyManager.records; }

} // namespace

Party* PartyForScript(Player* player, std::string* name) {
    if (!player) return nullptr;
    std::string owner;
    int split = str::Find(*name, "::");
    if (split >= 0) {
        owner = str::Mid(*name, 0, split);
        *name = str::Mid(*name, split + 2, static_cast<int>(name->size()) - split - 2);
    }
    if (str::Compare(owner, RecordList()->currentName) == 0) return player->GetParty();
    return (RecordList())->Find(owner);
}
