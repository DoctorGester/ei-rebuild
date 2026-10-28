// The party manager's save and load and the archive chunks they write: the parties by key, the entries, the
// class templates and the named records. See party_manager.h and docs/notes/script.md ("Leftovers").
#include "item_stock.h"
#include "party_manager.h"

#include "chunk_archive.h"
#include "core_refcount.h"
#include "core_streams.h"
#include "party.h"
#include "resource_local.h"   // FillStream

#include "record_vectors.h"
#include "game_hashes.h"
#include "lib/stlp_vector_refs.h"

#include <memory>
#include <string>


namespace {

// The records and templates are reference-counted objects held by counted pointers, whose release is inlined: one
// reference less, the object deleted at zero.
void Release(CRefCounted* counted) {
    if (counted && --counted->refs_ == 0) delete counted;
}

// Counted assignment (RefPtr::Assign, inlined for the templates): the new object gains a
// reference before the old one is released.
template <typename T>
void AssignCounted(T*& slot, CRefCounted* object) {
    T* old = slot;
    slot = static_cast<T*>(object);
    if (object) ++object->refs_;
    Release(old);
}

} // namespace

void PartyManagerEntry::Destruct() {
    ids.FreeStorage();
    std::destroy_at(&text);
    std::destroy_at(&name);
}

// ---- Archive chunks -------------------------------------------------------------------------------

void ChunkArchive::IntField(int id, int* value) { Field(id, value, 4); }

void ChunkArchive::SerializeParties(int id, PartyMap* parties, void*) {
    if (!EnterChunk(id)) return;
    if (loading_) {
        gamehash::PartyAbi::Clear(parties);
        int count = CountChunks(1);
        for (int i = 1; i <= count; ++i) {
            int key = 0;
            Current()->occurrence = i;
            if (EnterChunk(1)) {
                Field(1, &key, 4);
                LeaveChunk();
            }
            CParty* party = parties->At(&key)->Get();
            if (EnterChunk(2)) {
                party->ArchiveParty(this);
                LeaveChunk();
            }
        }
    } else {
        for (PartyMap::Node* node = parties->First(); node; node = parties->Next(node)) {
            int key = static_cast<int>(node->value.key);
            if (EnterChunk(1)) {
                Field(1, &key, 4);
                LeaveChunk();
            }
            if (EnterChunk(2)) {
                node->value.mapped.Get()->ArchiveParty(this);
                LeaveChunk();
            }
        }
    }
    LeaveChunk();
}

void ChunkArchive::SerializePartyEntries(int id, stlp::vector<PartyManagerEntry>* entries, void*) {
    if (!EnterChunk(id)) return;
    int count;
    if (loading_) {
        for (PartyManagerEntry* e = entries->begin(); e != entries->end(); ++e) e->Destruct();
        entries->Finish() = entries->Start();
        count = CountChunks(1);
        entries->ResizeDefault<recordvec::PartyEntryVector::Element>(static_cast<unsigned>(count));
    } else {
        count = static_cast<int>(entries->size());
    }
    for (int i = 0; i < count; ++i) {
        Current()->occurrence = i + 1;
        PartyManagerEntry* entry = entries->begin() + i;
        if (!EnterChunk(1)) continue;
        CStringField(1, &entry->name);
        CStringField(2, &entry->text);
        if (EnterChunk(3)) {
            stlp::vector<int>& ids = entry->ids;
            int idCount;
            if (loading_) {
                ids.Finish() = ids.Start();
                idCount = CountChunks(1);
                const int zero = 0;
                ids.Resize<stlp::PlainElement<int>>(static_cast<unsigned>(idCount), zero);
            } else {
                idCount = static_cast<int>(ids.size());
            }
            for (int j = 0; j < idCount; ++j) {
                Current()->occurrence = j + 1;
                int* data = ids.begin();
                if (EnterChunk(1)) {
                    IntField(1, data + j);
                    LeaveChunk();
                }
            }
            LeaveChunk();
        }
        LeaveChunk();
    }
    LeaveChunk();
}

void ChunkArchive::SerializeClassTemplates(int id, stlp::vector<ClassTemplate*>* templates, void*) {
    if (!EnterChunk(id)) return;
    int count;
    if (loading_) {
        for (ClassTemplate** p = templates->begin(); p != templates->end(); ++p) Release(*p);
        templates->Finish() = templates->Start();
        count = CountChunks(1);
        templates->ResizeDefault<stlp::CountedPtrElement<ClassTemplate>>(static_cast<unsigned>(count));
    } else {
        count = static_cast<int>(templates->size());
    }
    for (int i = 0; i < count; ++i) {
        Current()->occurrence = i + 1;
        ClassTemplate*& slot = (*templates)[i];
        if (loading_) AssignCounted(slot, ObjectRef(1));
        else SaveObjectRef(1, slot);
    }
    LeaveChunk();
}

void NamedPartyRecords::Serialize(ChunkArchive* ar) {
    if (ar->EnterChunk(1)) {
        int count;
        if (ar->loading_) {
            // erase(begin, end): every record released in order.
            for (PartyRecord** p = records.Start(); p != records.Finish(); ++p) Release(*p);
            records.Finish() = records.Start();
            count = ar->CountChunks(1);
            records.ResizeDefault<stlp::CountedPtrElement<PartyRecord>>(static_cast<unsigned>(count));
        } else {
            count = static_cast<int>(records.size());
        }
        for (int i = 0; i < count; ++i) {
            ar->Current()->occurrence = i + 1;
            PartyRecord*& slot = records.Start()[i];
            if (ar->loading_) AssignCounted(slot, ar->ObjectRef(1));
            else ar->SaveObjectRef(1, slot);
        }
        ar->LeaveChunk();
    }
    ar->CStringField(2, &text0C);
    ar->CStringField(3, &text10);
}

// ---- The archived objects (Serialize) ---------------------------------------------------

void PartyRecord::Serialize(ChunkArchive* ar) {
    CMemoryStream local;
    ar->CStringField(1, &name);
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
    ar->Field(1, &type, 4);
    ar->Field(2, &flag0C, 1);
    ar->Field(3, &flag0D, 1);
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
    ar->Field(6, &flag38, 1);
}

namespace {

// `to` becomes a copy of `from` (with variables) through a memory stream, as CParty::Assign without its Clear
// inlines it.
void CopyParty(CParty* to, CParty* from) {
    CMemoryStream local;
    from->SaveParty(&local, 1);
    local.CMemoryStream::Seek(0);
    to->LoadParty(&local, 1);
}

CParty* RecordParty(PartyRecord* record) { return record->Party(); }

} // namespace

void NamedPartyRecords::BeginRedeploy(CParty* party) {
    if (CParty* found = Find(text10.c_str())) CopyParty(found, party);
    NamedFloatTable variables;
    variables.CopyConstruct(party->variables_);
    std::string zone = party->zone_;
    for (PartyRecord* record : records) {
        CParty* target = RecordParty(record);
        target->variables_.Assign(variables);
        target->zone_ = zone;
        target->entrance_ = party->entrance_;
    }
    variables.Destroy();
}

void NamedPartyRecords::EndRedeploy(CParty* party) {
    if (CParty* found = Find(text0C.c_str())) CopyParty(party, found);
    else party->Clear(1, 0);
    text10 = text0C;
}

// ---- Load and save --------------------------------------------------------------------------------

namespace {

// The archive's contents, the same calls for both directions. The containers' serialisers get an unused third
// argument (a stack leftover).
void ArchiveManager(PartyManager* manager, ChunkArchive* ar) {
    ar->SerializeParties(1, &manager->parties, nullptr);
    ar->SerializePartyEntries(2, &manager->entries, nullptr);
    ar->Field(3, &manager->field3, 4);
    if (ar->EnterChunk(4)) {
        ar->SerializeClassTemplates(1, &manager->templates, nullptr);
        ar->LeaveChunk();
    }
    if (ar->EnterChunk(5)) {
        manager->records.Serialize(ar);
        ar->LeaveChunk();
    }
}

} // namespace

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
