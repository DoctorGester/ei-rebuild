// The party manager's save and load (004A8560 / 004A8310) and the archive chunks they write: the
// parties by key (004ACEC0), the entries (004AD0C0), the class templates (004ACDC0) and the named
// records (004A64B0). See party_manager.h and docs/notes/script.md ("A-S9").
#include "party_manager.h"

#include "chunk_archive.h"
#include "core_refcount.h"
#include "core_streams.h"
#include "originals_gameplay.h"
#include "party.h"

#include "ei/hook.h"

#pragma warning(disable : 4740) // naked forwarders

static_assert(sizeof(PartyInPlace) == sizeof(CParty));

namespace {

// STLport members (library), called as the original does: the party map's clear(), resize(n) of
// the entry, record and template vectors (null pointers / empty entries appended), resize(n, x) of
// an entry's ids.
__declspec(naked) void __fastcall PartyMapClear(PartyMap* /*map*/) EI_ORIGINAL(0x004AD050)
__declspec(naked) void __fastcall EntriesResize(stlp::vector<PartyManagerEntry>* /*v*/, void*, unsigned /*n*/) EI_ORIGINAL(0x004ADAA0)
__declspec(naked) void __fastcall RecordsResize(stlp::vector<PartyRecord*>* /*v*/, void*, unsigned /*n*/) EI_ORIGINAL(0x004AD700)
__declspec(naked) void __fastcall TemplatesResize(ClassTemplateVector* /*v*/, void*, unsigned /*n*/) EI_ORIGINAL(0x004AD8D0)
__declspec(naked) void __fastcall IdsResize(stlp::vector<int>* /*v*/, void*, unsigned /*n*/, const int* /*value*/) EI_ORIGINAL(0x004AE3A0)

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

// The memory stream the archive is read from or written to, built and destroyed inline.
struct LocalStream {
    LocalStream() { stream.Construct(); }
    ~LocalStream() { stream.Destruct(); }
    CMemoryStream stream;
};

// The archive, built and destroyed inline.
struct LocalArchive {
    LocalArchive() { archive.ConstructLocal(); }
    ~LocalArchive() { archive.DestructLocal(); }
    ChunkArchive archive;
};

} // namespace

__declspec(naked) void CParty::ArchiveParty(ChunkArchive*) EI_ORIGINAL(0x006617F0)

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
        PartyMapClear(parties);
        int count = CountChunks(1);
        for (int i = 1; i <= count; ++i) {
            int key = 0;
            Current()->occurrence = i;
            if (EnterChunk(1)) {
                Field(1, &key, 4);
                LeaveChunk();
            }
            auto* party = static_cast<CParty*>(PartyMapAtOriginal(parties, nullptr, &key));
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
        EntriesResize(entries, nullptr, static_cast<unsigned>(count));
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
                IdsResize(&ids, nullptr, static_cast<unsigned>(idCount), &zero);
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

void ChunkArchive::SerializeClassTemplates(int id, ClassTemplateVector* templates, void*) {
    if (!EnterChunk(id)) return;
    int count;
    if (loading_) {
        for (ClassTemplate** p = templates->start; p != templates->finish; ++p) Release(*p);
        templates->finish = templates->start;
        count = CountChunks(1);
        TemplatesResize(templates, nullptr, static_cast<unsigned>(count));
    } else {
        count = static_cast<int>(templates->finish - templates->start);
    }
    for (int i = 0; i < count; ++i) {
        Current()->occurrence = i + 1;
        ClassTemplate*& slot = templates->start[i];
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
            RecordsResize(&records, nullptr, static_cast<unsigned>(count));
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

// ---- Load and save --------------------------------------------------------------------------------

namespace {

// The archive's contents, the same calls for both directions. The containers' serialisers get an
// unused third argument (the original passes a stack leftover).
void ArchiveManager(PartyManager* manager, ChunkArchive* ar) {
    ar->SerializeParties(1, &manager->parties, nullptr);
    ar->SerializePartyEntries(2, &manager->entries, nullptr);
    ar->Field(3, &manager->unknown_20, 4);
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
    local.stream.CMemoryStream::Seek(0);
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
