// The party manager's save and load and the archive chunks they write: the parties by key, the entries, the
// class templates and the named records. See party_manager.h and docs/notes/script.md ("Leftovers").
#include "item_stock.h"
#include "party_manager.h"

#include "chunk_archive.h"
#include "core_refcount.h"
#include "core_streams.h"
#include "party.h"
#include "resource_local.h"   // FillStream


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
    if (ar->loading_) {
        objects->clear();
        count = ar->CountChunks(1);
        objects->resize(static_cast<std::size_t>(count));
    } else {
        count = static_cast<int>(objects->size());
    }
    for (int i = 0; i < count; ++i) {
        ar->Current()->occurrence = i + 1;
        HeldRef<T>& slot = (*objects)[static_cast<std::size_t>(i)];
        if (ar->loading_) slot = static_cast<T*>(ar->ObjectRef(1));
        else ar->SaveObjectRef(1, slot);
    }
    ar->LeaveChunk();
}

// `to` becomes a copy of `from` (with variables) through a memory stream, as CParty::Assign without its Clear
// inlines it.
void CopyParty(CParty* to, CParty* from) {
    CMemoryStream local;
    from->SaveParty(&local, 1);
    local.CMemoryStream::Seek(0);
    to->LoadParty(&local, 1);
}


// The archive's contents, the same calls for both directions. The containers' serialisers get an unused third
// argument (a stack leftover).
void ArchiveManager(PartyManager* manager, ChunkArchive* ar) {
    ar->SerializeParties(1, &manager->parties);
    ar->SerializePartyEntries(2, &manager->entries);
    ar->Field(3, &manager->unreadField);
    if (ar->EnterChunk(4)) {
        ar->SerializeClassTemplates(1, &manager->templates);
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
    if (loading_) {
        parties->clear();
        int count = CountChunks(1);
        for (int i = 1; i <= count; ++i) {
            int key = 0;
            Current()->occurrence = i;
            if (EnterChunk(1)) {
                Field(1, &key);
                LeaveChunk();
            }
            CParty* party = PartyAt(*parties, key);
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
            if (ar->loading_) {
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

void ChunkArchive::SerializeClassTemplates(int id, TemplateTable* templates) {
    ObjectRefFields(this, id, templates);
}

void NamedPartyRecords::Serialize(ChunkArchive* ar) {
    ObjectRefFields(ar, 1, &records);
    ar->StringField(2, &requestedName);
    ar->StringField(3, &currentName);
}

// ---- The archived objects (Serialize) ---------------------------------------------------

void PartyRecord::Serialize(ChunkArchive* ar) {
    CMemoryStream local;
    ar->StringField(1, &name);
    CParty* p = Party();
    if (!ar->loading_) {
        p->SaveParty(&local, 1);
        ar->StreamField(2, &local);
    } else {
        ar->StreamField(2, &local);
        local.version_ = static_cast<std::uint32_t>(ar->version_);
        p->LoadParty(&local, 1);
    }
}

void ClassTemplate::Serialize(ChunkArchive* ar) {
    ar->Field(1, &type);
    ar->Field(2, &sellsItems);
    ar->Field(3, &sellsSpells);
    ar->Field(4, weights, sizeof(weights) + sizeof(weightsTail));
    CMemoryStream local;
    CParty* itemsParty = ItemsParty();
    CParty* spellsParty = SpellsParty();
    if (!ar->loading_) {
        itemsParty->SaveParty(&local, 0);
        spellsParty->SaveParty(&local, 0);
        allowed.SaveLists(&local);
        magic.SaveLists(&local);
        ar->StreamField(5, &local);
    } else {
        ar->StreamField(5, &local);
        local.version_ = static_cast<std::uint32_t>(ar->version_);
        itemsParty->LoadParty(&local, 0);
        spellsParty->LoadParty(&local, 0);
        allowed.LoadLists(&local);
        magic.LoadLists(&local);
    }
    ar->Field(6, &restockDue);
}

void NamedPartyRecords::BeginRedeploy(CParty* party) {
    if (CParty* found = Find(currentName)) CopyParty(found, party);
    NamedFloatTable variables(party->variables_);
    std::string zone = party->zone_;
    for (PartyRecord* record : records) {
        CParty* target = record->Party();
        target->variables_ = variables;
        target->zone_ = zone;
        target->entrance_ = party->entrance_;
    }
}

void NamedPartyRecords::EndRedeploy(CParty* party) {
    if (CParty* found = Find(requestedName)) CopyParty(party, found);
    else party->Clear(1, 0);
    currentName = requestedName;
}

// ---- Load and save --------------------------------------------------------------------------------

void PartyManager::Load(CMemoryStream* from) {
    CMemoryStream local;
    from->ReadSized(&local);
    local.CMemoryStream::Seek(0);
    ChunkArchive archive;
    archive.Open(&local, 1, static_cast<int>(from->version_));
    ArchiveManager(this, &archive);
    archive.Close(&local);
}

void PartyManager::Save(CMemoryStream* to) {
    CMemoryStream local;
    ChunkArchive archive;
    archive.Open(&local, 0, static_cast<int>(to->version_));
    ArchiveManager(this, &archive);
    archive.Close(&local);
    to->WriteSized(&local);
}
