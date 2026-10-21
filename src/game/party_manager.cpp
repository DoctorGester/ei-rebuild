// The party manager's save and load (004A8560 / 004A8310) and the archive chunks they write: the
// parties by key (004ACEC0), the entries (004AD0C0), the class templates (004ACDC0) and the named
// records (004A64B0). See party_manager.h and docs/notes/script.md ("Leftovers").
#include "item_stock.h"
#include "party_manager.h"

#include "chunk_archive.h"
#include "core_refcount.h"
#include "core_streams.h"
#include "originals_gameplay.h"
#include "originals_library.h"
#include "party.h"
#include "resource_local.h"   // LocalStream

#include "ei/hook.h"
#include "record_vectors.h"
#include "game_hashes.h"
#include "lib/stlp_vector_refs.h"

#pragma warning(disable : 4740) // naked forwarders

static_assert(sizeof(PartyInPlace) == sizeof(CParty));

namespace {

// STLport members (library), called as the original does: the party map's clear() (004AD050,
// gamehash::PartyAbi's), resize(n) of
// the entry, record and template vectors (null pointers / empty entries appended), resize(n, x) of
// an entry's ids.
// (The record and template vectors' resize, 004AD700 / 004AD8D0: stlp::CountedRefVector's.)

// The records and templates are reference-counted objects (root vtable 0073BB68) held by counted
// pointers, whose release the original inlines (004A5D70 / 004A7EA0): one reference less, the
// object deleted (slot 0 with 1) at zero.
void Release(void* object) {
    auto* counted = static_cast<CRefCounted*>(object);
    if (counted && --counted->refs_ == 0) delete counted;
}

// Counted assignment (RefPtr::Assign, inlined for the templates): the new object gains a
// reference before the old one is released.
template <typename T>
void AssignCounted(T*& slot, CRefObject* object) {
    void* old = slot;
    slot = static_cast<T*>(static_cast<void*>(object));
    if (object) ++object->refs_;
    Release(old);
}

CRefObject* AsRefObject(void* object) { return static_cast<CRefObject*>(object); }

} // namespace

void PartyManagerEntry::Destruct() {
    ids.FreeStorage();
    text.~CString();
    name.~CString();
}

// ---- Archive chunks -------------------------------------------------------------------------------

void ChunkArchive::IntField(int id, int* value) { Field(id, value, 4); }
EI_REPLACE_METHOD(0x004AD2C0, ChunkArchive::IntField);

void ChunkArchive::SerializeParties(int id, PartyMap* parties, void*) {
    if (!EnterChunk(id)) return;
    if (loading_) {
        gamehash::PartyAbi::Clear(reinterpret_cast<gamehash::PartyTable*>(parties));
        int count = CountChunks(1);
        for (int i = 1; i <= count; ++i) {
            int key = 0;
            Current()->occurrence = i;
            if (EnterChunk(1)) {
                Field(1, &key, 4);
                LeaveChunk();
            }
            auto* party = reinterpret_cast<CParty*>(reinterpret_cast<gamehash::PartyTable*>(parties)->At(
                                                                      reinterpret_cast<const std::uint32_t*>(&key)));   // 004A91B0
            if (EnterChunk(2)) {
                party->ArchiveParty(this);
                LeaveChunk();
            }
        }
    } else {
        for (PartyMap::Node* node = parties->First(); node; node = parties->After(node)) {
            int key = node->key;
            if (EnterChunk(1)) {
                Field(1, &key, 4);
                LeaveChunk();
            }
            if (EnterChunk(2)) {
                node->value.Get()->ArchiveParty(this);
                LeaveChunk();
            }
        }
    }
    LeaveChunk();
}
EI_REPLACE_METHOD(0x004ACEC0, ChunkArchive::SerializeParties);

void ChunkArchive::SerializePartyEntries(int id, stlp::vector<PartyManagerEntry>* entries, void*) {
    if (!EnterChunk(id)) return;
    int count;
    if (loading_) {
        for (PartyManagerEntry* e = entries->begin(); e != entries->end(); ++e) e->Destruct();
        entries->Finish() = entries->Start();
        count = CountChunks(1);
        recordvec::PartyEntryVector::ResizeDefault(entries, nullptr, static_cast<unsigned>(count));
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
EI_REPLACE_METHOD(0x004AD0C0, ChunkArchive::SerializePartyEntries);

void ChunkArchive::SerializeClassTemplates(int id, stlp::vector<ClassTemplate*>* templates, void*) {
    if (!EnterChunk(id)) return;
    int count;
    if (loading_) {
        for (ClassTemplate** p = templates->begin(); p != templates->end(); ++p) Release(*p);
        templates->Finish() = templates->Start();
        count = CountChunks(1);
        stlp::CountedRefVector::ResizeDefault(&stlp::AsRefPtrs(*templates), nullptr, static_cast<unsigned>(count));
    } else {
        count = static_cast<int>(templates->size());
    }
    for (int i = 0; i < count; ++i) {
        Current()->occurrence = i + 1;
        ClassTemplate*& slot = (*templates)[i];
        if (loading_) AssignCounted(slot, ObjectRef(1));
        else SaveObjectRef(1, AsRefObject(slot));
    }
    LeaveChunk();
}
EI_REPLACE_METHOD(0x004ACDC0, ChunkArchive::SerializeClassTemplates);

void NamedPartyRecords::Serialize(ChunkArchive* ar) {
    if (ar->EnterChunk(1)) {
        int count;
        if (ar->loading_) {
            // erase(begin, end) (004A9910): every record released in order.
            for (PartyRecord** p = records.Start(); p != records.Finish(); ++p) Release(*p);
            records.Finish() = records.Start();
            count = ar->CountChunks(1);
            stlp::CountedRefVector::ResizeDefault(&stlp::AsRefPtrs(records), nullptr, static_cast<unsigned>(count));
        } else {
            count = static_cast<int>(records.size());
        }
        for (int i = 0; i < count; ++i) {
            ar->Current()->occurrence = i + 1;
            PartyRecord*& slot = records.Start()[i];
            if (ar->loading_) AssignCounted(slot, ar->ObjectRef(1));
            else ar->SaveObjectRef(1, AsRefObject(slot));
        }
        ar->LeaveChunk();
    }
    ar->CStringField(2, &text0C);
    ar->CStringField(3, &text10);
}
EI_REPLACE_METHOD(0x004A64B0, NamedPartyRecords::Serialize);

// ---- The archived objects (slot 1 Serialize) ---------------------------------------------------------

void PartyRecord::Serialize(ChunkArchive* ar) {
    LocalStream local;
    ar->CStringField(1, &name);
    CParty* p = reinterpret_cast<CParty*>(party);
    if (!ar->loading_) {
        p->SaveParty(&local.stream, 1);
        ar->StreamField(2, &local.stream);
    } else {
        ar->StreamField(2, &local.stream);
        local.stream.version_ = static_cast<std::uint32_t>(ar->version_);
        p->LoadParty(&local.stream, 1);
    }
}
EI_REPLACE_METHOD(0x004A5AF0, PartyRecord::Serialize);

void ClassTemplate::Serialize(ChunkArchive* ar) {
    ar->Field(1, &type, 4);
    ar->Field(2, &flag0C, 1);
    ar->Field(3, &flag0D, 1);
    ar->Field(4, weights, sizeof(weights) + sizeof(weightsTail));
    LocalStream local;
    auto* itemsParty = reinterpret_cast<CParty*>(items);
    auto* spellsParty = reinterpret_cast<CParty*>(spells);
    if (!ar->loading_) {
        itemsParty->SaveParty(&local.stream, 0);
        spellsParty->SaveParty(&local.stream, 0);
        allowed.SaveLists(&local.stream);
        magic.SaveLists(&local.stream);
        ar->StreamField(5, &local.stream);
    } else {
        ar->StreamField(5, &local.stream);
        local.stream.version_ = static_cast<std::uint32_t>(ar->version_);
        itemsParty->LoadParty(&local.stream, 0);
        spellsParty->LoadParty(&local.stream, 0);
        allowed.LoadLists(&local.stream);
        magic.LoadLists(&local.stream);
    }
    ar->Field(6, &flag38, 1);
}
EI_REPLACE_METHOD(0x004A6C40, ClassTemplate::Serialize);

// ---- Redeploy -------------------------------------------------------------------------------------

namespace {

// `to` becomes a copy of `from` (with variables) through a memory stream, as the original inlines
// it (CParty::Assign without its Clear).
void CopyParty(CParty* to, CParty* from) {
    LocalStream local;
    from->SaveParty(&local.stream, 1);
    local.stream.CMemoryStream::SeekImpl(0);
    to->LoadParty(&local.stream, 1);
}

CParty* RecordParty(PartyRecord* record) { return reinterpret_cast<CParty*>(record->party); }

} // namespace

void NamedPartyRecords::BeginRedeploy(CParty* party) {
    if (CParty* found = Find(text10)) CopyParty(found, party);
    NamedFloatTable variables;
    variables.CopyConstruct(party->variables_);
    mfc::CString zone(party->zone_);
    for (PartyRecord* record : records) {
        CParty* target = RecordParty(record);
        target->variables_.Assign(variables);
        target->zone_ = zone;
        target->entrance_ = party->entrance_;
    }
    variables.Destroy();
}
EI_REPLACE_METHOD(0x004A6050, NamedPartyRecords::BeginRedeploy);

void NamedPartyRecords::EndRedeploy(CParty* party) {
    if (CParty* found = Find(text0C)) CopyParty(party, found);
    else party->Clear(1, 0);
    text10 = text0C;
}
EI_REPLACE_METHOD(0x004A6360, NamedPartyRecords::EndRedeploy);

// ---- Load and save --------------------------------------------------------------------------------

namespace {

// The archive's contents, the same calls for both directions. The containers' serialisers get an
// unused third argument (the original passes a stack leftover).
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
    LocalStream local;
    from->ReadSized(&local.stream);
    local.stream.CMemoryStream::SeekImpl(0);
    LocalArchive archive;
    archive.archive.Open(&local.stream, 1, static_cast<int>(from->version_));
    ArchiveManager(this, &archive.archive);
    archive.archive.Close(&local.stream);
}
EI_REPLACE_METHOD(0x004A8310, PartyManager::Load);

void PartyManager::Save(CMemoryStream* to) {
    LocalStream local;
    LocalArchive archive;
    archive.archive.Open(&local.stream, 0, static_cast<int>(to->version_));
    ArchiveManager(this, &archive.archive);
    archive.archive.Close(&local.stream);
    to->WriteSized(&local.stream);
}
EI_REPLACE_METHOD(0x004A8560, PartyManager::Save);
