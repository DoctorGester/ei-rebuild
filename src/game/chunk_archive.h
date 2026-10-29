#pragma once

// The tagged-chunk serialisation archive the databases of database.res are stored in. Reading and writing go
// through the same Serialize functions; loading selects the direction. A chunk's content is a run of
// children, each an id byte, a length byte (len << 1) or, when bit 0 is set, a dword (len << 1 | 1), and len
// bytes. A stream holding an archive has three top-level chunks of the same form: 0 the index (per object:
// its class id and the key its record is stored under), 1 the data of the archive's root chunk, 2 the object
// records. See docs/notes/core.md.

#include "core_streams.h"

#include "lib/hash_map.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

struct LeverRecord;
class CParty;
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
        std::int32_t offset;     // of the content in dataStream
        std::int32_t size;
        std::int32_t occurrence;

        // {0xFF, 0, 0, occurrence 1}.
        void Reset();
    };

    // While loading: key -> the object the index names (by the key its record is stored under).
    // (walked in STLport's order by Close: the objects nobody took are deleted in that order)
    using LoadedObjects = lib::HashMap<std::uint32_t, RefCounted*>;

    // The classes an archive can hold: Close writes an object's class id (found by its vtable) to the index,
    // Open creates the object with the class's factory.
    using Factory = RefCounted*(*)();
    struct ClassRegistry {
        std::unordered_map<std::uint32_t, std::int32_t> classIds;     // vtable -> class id
        std::unordered_map<std::uint32_t, Factory> factories;    // class id -> factory
    };

    Ref* Current() { return &stack.back(); }
    // The current chunk's parent.
    Ref* Parent() { return &*std::prev(stack.end(), 2); }
    void PushChunk() { stack.emplace_back().Reset(); }
    // Drops the current chunk, as LeaveChunk does after a load.
    void PopChunk();

    bool NextChild(const Ref* parent, std::int32_t* cursor, Ref* out);
    void AppendChild(Ref* chunk, int id, const void* data, std::uint32_t size);
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
    // One value of sizeof(T) bytes: Field(id, &value, sizeof(T)).
    template <class T>
    void Field(int id, T* value) {
        static_assert(std::is_trivially_copyable_v<T>, "a field is plain bytes");
        Field(id, static_cast<void*>(value), sizeof(T));
    }
    void StringField(int id, std::string* s);
    void StreamField(int id, MemoryStream* s);       // chunk `id` {1: size, 2: bytes}
    void SaveObjectRef(int id, RefCounted* object);   // object's number, object kept for Close
    RefCounted* ObjectRef(int id);                    // the object a saved key stands for

    // Starts a load (`loading`, a byte: reads the index chunk 0, data chunk 2 and the
    // object records from `stream`) or a save (`version` kept for Close).
    void Open(ByteStream* stream, int loading, int version);
    // Ends a save: writes the saved objects' records, the version (field 2) and the index
    // and data chunks to `stream`; or, after a load, drops the loaded objects. Clears everything.
    void Close(ByteStream* stream);

    // Database arrays. Each is a vector of records serialised as chunk `id` holding one chunk 1 per record
    // (`occurrence` selects the record); loading replaces the vector's contents.
    void SerializePrints(int id, std::vector<PrintRecord>* records);
    void SerializePrints2(int id, std::vector<PrintRecord2>* records);
    void SerializeAcks(int id, std::vector<acks::UnitAcks>* records);
    void SerializeAcks2(int id, std::vector<acks::ModelAcks>* records);
    void SerializeAcks3(int id, std::vector<acks::ExtraAcks>* records);

    // The party manager's chunks. Loading empties the container
    // first and takes one record per chunk 1. The parties by key (party_manager.h, PartyMap): per party a chunk
    // 1 (field 1: the key) and a chunk 2 (CParty::ArchiveParty); loading inserts each key (PartyAt).
    void SerializeParties(int id, lib::HashMap<int, CParty>* parties);
    // Vector<PartyManagerEntry>: per entry a chunk 1 with string fields 1 and 2 and a
    // chunk 3 holding one chunk 1 (field 1) per id.
    void SerializePartyEntries(int id, std::vector<PartyManagerEntry>* entries);
    void IntField(int id, int* value);

    // As chunk `id`.
    void SerializeSpeechList(int id, acks::Speeches* list);
    void SerializeEnvironmentSpeeches(int id, acks::EnvironmentSpeeches* list);
    // The lever records as chunk `id`, one chunk 1 per
    // record (emptied and resized to the number of chunks when loading).
    void SerializeLevers(int id, std::vector<LeverRecord>* list);

    // The quest database's vectors: one chunk 1 per record (its Serialize); loading destroys the records and
    // resizes the vector to the record chunks first.
    void SerializeQuests(int id, std::vector<QuestRecord>* quests);
    void SerializeBriefings(int id, std::vector<QuestBriefingRecord>* briefings);

    // The skill database's vectors, the same shape.
    void SerializeSkills(int id, std::vector<SkillRecord>* skills);
    void SkillLevelChunk(int id, SkillLevelRecord* record);
    // The unit database's vectors, the same shape.
    void SerializePrototypes(int id, std::vector<UnitPrototypeRecord>* records);
    void SerializeNpcs(int id, std::vector<UnitNpcRecord>* records);
    void SerializeModels(int id, std::vector<UnitModelRecord>* records);
    void SerializeUnitLevels(int id, std::vector<UnitLevelRecord>* records);
    // A vector of strings as chunk `id` (InlineStrings).
    void SerializeStrings(int id, std::vector<std::string>* strings);

    ChunkArchive();
    ~ChunkArchive();
    ChunkArchive(const ChunkArchive&) = delete;
    ChunkArchive& operator=(const ChunkArchive&) = delete;

    MemoryStream indexStream;     // chunk 0
    MemoryStream dataStream;      // the chunk data (chunk 2 while loading the records)
    std::list<Ref> stack;                   // the last chunk is the current one
    std::int32_t version;          // 1 when constructed; field 2 of the root chunk
    std::uint8_t loading;          // Open stores the low byte of its argument
    LoadedObjects loaded;
    // While saving: the objects already listed -> their number by first save (1, 2, ...), written as the key
    // instead of the object's address.
    std::unordered_map<const RefCounted*, std::uint32_t> numbers;
    std::list<RefCounted*> saved;               // the objects whose records Close writes
};
extern ChunkArchive::ClassRegistry g_classRegistry;   // chunk_archive.cpp

// A vector of records as chunk `id`, one chunk 1 per record (the record's occurrence): loading empties the
// vector and resizes it to the number of chunks 1 first. `fields(record, archive)` (a record's member
// Serialize, or a function of both) reads or writes each record's chunk.
template <typename Record, typename Fields>
void RecordChunks(ChunkArchive* ar, int id, std::vector<Record>* records, Fields fields) {
    if (!ar->EnterChunk(id)) return;
    int count;
    if (ar->loading) {
        records->clear();
        count = ar->CountChunks(1);
        records->resize(static_cast<std::size_t>(count));
    } else {
        count = static_cast<int>(records->size());
    }
    for (int i = 0; i < count; ++i) {
        ar->Current()->occurrence = i + 1;
        if (ar->EnterChunk(1)) {
            std::invoke(fields, (*records)[static_cast<std::size_t>(i)], ar);
            ar->LeaveChunk();
        }
    }
    ar->LeaveChunk();
}

// Whole chunks of a stream (the archive's top-level chunks). Reads one chunk header and its bytes into `to` (at
// most 10,000,000). True when all of it was read.
bool ReadStreamChunk(ByteStream* in, char* id, MemoryStream* to);
// From the start of the stream, the `occurrence`-th chunk `id` into `to`; on a read error or the end, `to` is
// cleared and false.
bool FindStreamChunk(ByteStream* in, int id, MemoryStream* to);
void WriteStreamChunk(ByteStream* out, int id, MemoryStream* from);
