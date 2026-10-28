#pragma once

// The tagged-chunk serialisation archive the databases of database.res are stored in. Reading and writing go
// through the same Serialize functions; loading_ selects the direction. A chunk's content is a run of
// children, each an id byte, a length byte (len << 1) or, when bit 0 is set, a dword (len << 1 | 1), and len
// bytes. A stream holding an archive has three top-level chunks of the same form: 0 the index (per object:
// its class id and the key its record is stored under), 1 the data of the archive's root chunk, 2 the object
// records. See docs/notes/core.md.

#include "core_streams.h"

#include "lib/stlp.h"
#include "game_data.h"

#include <cstddef>
#include <cstdint>
#include <string>

class ClassTemplate;
struct LeverRecord;
namespace gamehash { struct PartyTable; }
namespace itemdb {
struct QuickItemRecord;
struct QuestItemRecord;
struct LootItemRecord;
struct MaterialRecordHead;
struct WeaponRecord;
struct ArmorRecord;
struct SpellRecord;
struct SpellModifierRecord;
struct SpellListRecord;
}
struct PartyManagerEntry;
struct PrintRecord;
struct PrintRecord2;
struct SpeechRecord;
struct QuestBriefingRecord;
struct QuestRecord;
struct SkillLevelRecord;
struct SkillRecord;
struct UnitLevelRecord;
struct UnitModelRecord;
struct UnitNpcRecord;
struct UnitPrototypeRecord;
namespace mfc {
class CString;
}
namespace acks {
struct UnitAcks;
struct ModelAcks;
struct ExtraAcks;
struct Speeches;
struct EnvironmentSpeeches;
}

class ChunkArchive {
public:
    // A chunk being read or written: where its content is in the data stream, and which
    // occurrence of a child id the next search for it takes.
    struct Ref {
        std::uint8_t id;
        std::uint8_t unused_01[3];
        std::int32_t offset;     // of the content in dataStream_
        std::int32_t size;
        std::int32_t occurrence;

        // {0xFF, 0, 0, occurrence 1}.
        void Reset();
    };
    static_assert(sizeof(Ref) == 0x10);

    // The chunk stack's element policy: T() is Reset's (the three unused bytes are what the stack held).
    struct RefElement {
        static void Copy(Ref* to, const Ref& from) { *to = from; }
        static void Destroy(Ref*) {}
        static void Default(Ref* p) { p->Reset(); }
    };
    using Stack = stlp::list<Ref>;
    static_assert(sizeof(Stack::Node) == 0x18);
    using SavedList = stlp::list<CRefCounted*>;

    // While loading: key -> the object the index names (by the key its record is stored under).
    using LoadedObjects = stlp::hash_map<std::uint32_t, CRefCounted*, stlp::IntHash>;
    // While saving: the objects already listed -> their number by first save (1, 2, ...), written as the key
    // instead of the object's address.
    using SavedNumbers = stlp::hash_map<const CRefCounted*, std::uint32_t, stlp::PointerHash>;

    // The classes an archive can hold: Close writes an object's class id (found by its vtable) to the index,
    // Open creates the object with the class's factory.
    using Factory = CRefCounted*(__cdecl*)();
    struct ClassRegistry {
        using ClassIdMap = stlp::hash_map<std::uint32_t, std::int32_t, stlp::IntHash>;
        ClassIdMap classIds;     // vtable -> class id
        stlp::hash_map<std::uint32_t, Factory, stlp::IntHash> factories;    // class id -> factory
    };
    static ClassRegistry& Registry();

    Ref* Current() { return &stack_.End()->prev->data; }
    void PushChunk() { Stack::InsertDefault<RefElement>(stack_.End()); }
    std::uint8_t* Data() { return dataStream_.data_; }
    // Drops the current chunk, as LeaveChunk does after a load.
    void PopChunk();

    bool NextChild(const Ref* parent, std::int32_t* cursor, Ref* out);
    bool AppendChild(Ref* chunk, int id, const void* data, std::uint32_t size);
    // The `occurrence`-th child `id` of `parent` into `out`.
    bool FindChild(const Ref* parent, int id, Ref* out, int occurrence);
    int CountChildrenOf(const Ref* parent, int id);
    void ResizeToCurrent();

    // Chunks and fields of the current chunk. Writing appends; reading takes the current chunk's
    // occurrence of the child (missing: zero bytes, empty text, zero key).
    bool EnterChunk(int id);
    void LeaveChunk();
    int CountChunks(int id);
    void Field(int id, void* data, unsigned size);
    // A text field (mfc::CString or std::string), written with its terminator.
    template <typename Text>
    void CStringField(int id, Text* s);
    void StringField(int id, stlp::string* s);
    void StreamField(int id, CMemoryStream* s);       // chunk `id` {1: size, 2: bytes}
    void SaveObjectRef(int id, CRefCounted* object);   // object's number, object kept for Close
    CRefCounted* ObjectRef(int id);                    // the object a saved key stands for

    // Starts a load (`loading`, a byte: reads the index chunk 0, data chunk 2 and the
    // object records from `stream`) or a save (`version` kept for Close).
    void Open(CStream* stream, int loading, int version);
    // Ends a save: writes the saved objects' records, the version (field 2) and the index
    // and data chunks to `stream`; or, after a load, drops the loaded objects. Clears everything.
    void Close(CStream* stream);

    // Database arrays. Each is a vector of records serialised as chunk `id` holding one chunk 1 per record
    // (`occurrence` selects the record); loading replaces the vector's contents. The third argument (the
    // callers pass the database file's data) is unused.
    void SerializePrints(int id, stlp::vector<PrintRecord>* records, void* unused);
    void SerializePrints2(int id, stlp::vector<PrintRecord2>* records, void* unused);
    void SerializeAcks(int id, stlp::vector<acks::UnitAcks>* records, void* unused);
    void SerializeAcks2(int id, stlp::vector<acks::ModelAcks>* records, void* unused);
    void SerializeAcks3(int id, stlp::vector<acks::ExtraAcks>* records, void* unused);

    // The party manager's chunks. The third argument is unused but popped. Loading empties the container
    // first and takes one record per chunk 1. Hash_map<int, CParty>: per party a chunk 1 (field 1: the key)
    // and a chunk 2 (CParty::ArchiveParty); loading inserts each key (operator[]).
    void SerializeParties(int id, gamehash::PartyTable* parties, void* unused);
    // Vector<PartyManagerEntry>: per entry a chunk 1 with string fields 1 and 2 and a
    // chunk 3 holding one chunk 1 (field 1) per id.
    void SerializePartyEntries(int id, stlp::vector<PartyManagerEntry>* entries, void* unused);
    // The class templates (counted pointers) as object references (field 1 per chunk 1).
    void SerializeClassTemplates(int id, stlp::vector<ClassTemplate*>* templates, void* unused);
    void IntField(int id, int* value);

    void SerializeSpeech(int id, SpeechRecord* speech);
    // As chunk `id`; the third argument (the archive again) is unused.
    void SerializeSpeechList(int id, acks::Speeches* list, void* unused);
    void SerializeEnvironmentSpeeches(int id, acks::EnvironmentSpeeches* list, void* unused);
    // The lever records as chunk `id`, one chunk 1 per
    // record (emptied and resized to the number of chunks when loading). The last argument is not read.
    void SerializeLevers(int id, stlp::vector<LeverRecord>* list, int unused);

    // The item database's vectors, one chunk 1 per record (the third argument unused).
    void SerializeQuickItems(int id, stlp::vector<itemdb::QuickItemRecord>* records, void* unused);
    void SerializeLoot(int id, stlp::vector<itemdb::QuestItemRecord>* records, void* unused);
    void SerializeQuestItems(int id, stlp::vector<itemdb::LootItemRecord>* records, void* unused);
    void SerializeMaterials(int id, stlp::vector<itemdb::MaterialRecordHead>* records, void* unused);
    void SerializeWeapons(int id, stlp::vector<itemdb::WeaponRecord>* records, void* unused);
    void SerializeArmors(int id, stlp::vector<itemdb::ArmorRecord>* records, void* unused);

    // The spell database's vectors, as the item database's.
    void SerializeSpells(int id, stlp::vector<itemdb::SpellRecord>* records, void* unused);
    void SerializeSpellModifiers(int id, stlp::vector<itemdb::SpellModifierRecord>* records, void* unused);
    void SerializeSpellLists(int id, stlp::vector<itemdb::SpellListRecord>* records, void* unused);
    // vector<int> as chunk `id`: one field 1 per
    // value; loading empties the vector and refills it with as many zeros as there are fields.
    void IntVectorField(int id, stlp::vector<int>* values, void* unused);

    // The quest database's vectors: one chunk 1 per record (its Serialize); loading destroys the records and
    // resizes the vector to the record chunks first.
    void SerializeQuests(int id, stlp::vector<QuestRecord>* quests, void* unused);
    void SerializeBriefings(int id, stlp::vector<QuestBriefingRecord>* briefings, void* unused);

    // The skill database's vectors, the same shape.
    void SerializeSkills(int id, stlp::vector<SkillRecord>* skills, void* unused);
    void SerializeSkillLevels(int id, stlp::vector<SkillLevelRecord>* levels, void* unused);
    void SkillLevelChunk(int id, SkillLevelRecord* record);
    // The unit database's vectors, the same shape.
    void SerializePrototypes(int id, stlp::vector<UnitPrototypeRecord>* records, void* unused);
    void SerializeNpcs(int id, stlp::vector<UnitNpcRecord>* records, void* unused);
    void SerializeModels(int id, stlp::vector<UnitModelRecord>* records, void* unused);
    void SerializeUnitLevels(int id, stlp::vector<UnitLevelRecord>* records, void* unused);
    // A vector<CString> as chunk `id` (InlineStrings).
    void SerializeCStrings(int id, stlp::vector<mfc::CString>* strings, void* unused);

    ChunkArchive();
    ~ChunkArchive();
    ChunkArchive(const ChunkArchive&) = delete;
    ChunkArchive& operator=(const ChunkArchive&) = delete;

    CMemoryStream indexStream_;    // chunk 0
    CMemoryStream dataStream_;     // the chunk data (chunk 2 while loading the records)
    Stack stack_;                  // the last chunk is the current one
    std::int32_t version_;         // 1 when constructed; field 2 of the root chunk
    std::uint8_t loading_;         // Open stores the low byte of its argument
    LoadedObjects loaded_;
    SavedNumbers numbers_;
    SavedList saved_;              // the objects whose records Close writes
};
static_assert(sizeof(ChunkArchive::ClassRegistry) == 0x28);

// Whole chunks of a stream (the archive's top-level chunks). Reads one chunk header and its bytes into `to` (at
// most 10,000,000). True when all of it was read.
bool __fastcall ReadStreamChunk(CStream* in, char* id, CMemoryStream* to);
// From the start of the stream, the `occurrence`-th chunk `id` into `to`; on a read error or the end, `to` is
// cleared and false.
bool __fastcall FindStreamChunk(CStream* in, int id, CMemoryStream* to, int occurrence);
bool __fastcall WriteStreamChunk(CStream* out, int id, CMemoryStream* from);
