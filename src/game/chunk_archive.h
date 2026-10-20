#pragma once

// The tagged-chunk serialisation archive (00409070-0040A5A0) the databases of database.res are stored
// in. Constructed by 0053A7B0, opened by Open (00409AE0). Reading and writing go through the same
// Serialize functions; loading_ selects the direction. Names provisional; see
// docs/notes/core.md.
//
// A chunk's content is a run of children, each an id byte, a length byte (len << 1) or, when bit 0
// is set, a dword (len << 1 | 1), and len bytes. A stream holding an archive has three top-level
// chunks of the same form: 0 the index (per object: its class id and the key its record is stored
// under), 1 the data of the archive's root chunk, 2 the object records.

#include "core_streams.h"

#include "lib/stlp.h"

#include <cstddef>
#include <cstdint>

class CRefObject;
struct ClassTemplate;
struct PartyInPlace;
struct PartyManagerEntry;
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

        // 00409E80. {0xFF, 0, 0, occurrence 1}.
        void Reset();
    };
    static_assert(sizeof(Ref) == 0x10);

    // The chunk stack's element policy (STLport list<Ref>, 24-byte nodes from the node pool):
    // plain records; T() is Reset's {0xFF, 0, 0, occurrence 1} (the three unused bytes are what
    // the stack held).
    struct RefElement {
        static void Copy(Ref* to, const Ref& from) { *to = from; }
        static void Destroy(Ref*) {}
        static void Default(Ref* p) { p->Reset(); }
    };
    using Stack = stlp::list<Ref>;
    static_assert(sizeof(Stack::Node) == 0x18);
    // The objects to save (STLport list<CRefObject*>).
    using SavedList = stlp::list<CRefObject*>;

    // Key -> object: while loading, the objects the index names (by the key their records are
    // stored under); while saving, the objects already listed (value: the object's number by first
    // save, which ours writes as its key where the original wrote its address; SV7b2a).
    using ObjectMap = stlp::hash_map<std::uint32_t, CRefObject*, stlp::IntHash>;

    // The classes an archive can hold (a global, 0079B270; built by the static initialiser
    // 00408D30 (00408EA0), destroyed at exit by 00408D60): Close writes an object's class id
    // (found by its vtable) to the index, Open creates the object with the class's factory.
    using Factory = CRefObject*(__cdecl*)();
    struct ClassRegistry {
        using ClassIdMap = stlp::hash_map<std::uint32_t, std::int32_t, stlp::IntHash>;
        using FactoryMap = stlp::hash_map<std::uint32_t, Factory, stlp::IntHash>;
        ClassIdMap classIds;     // +0x00 vtable -> class id
        FactoryMap factories;    // +0x14 class id -> factory
    };
    static ClassRegistry& Registry() { return *reinterpret_cast<ClassRegistry*>(0x0079B270); }

    // The current chunk: the top of the stack.
    Ref* Current() { return &stack_.End()->prev->data; }
    // A default chunk pushed on the stack (push_back(), 0040A6B0; insert(end()), 0040A8D0).
    void PushChunk() { Stack::InsertDefault<RefElement>(stack_.End()); }
    std::uint8_t* Data() { return dataStream_.data_; }
    // Drops the current chunk (the stack's erase(--end()), STLport 0040A920), as LeaveChunk does
    // after a load.
    void PopChunk();

    // Chunk primitives.
    bool NextChild(const Ref* parent, std::int32_t* cursor, Ref* out);           // 00409070
    bool AppendChild(Ref* chunk, int id, const void* data, std::uint32_t size);  // 00409110
    // 004091C0. The `occurrence`-th child `id` of `parent` into `out`.
    bool FindChild(const Ref* parent, int id, Ref* out, int occurrence);
    int CountChildrenOf(const Ref* parent, int id);                              // 00409230
    void ResizeToCurrent();                                                      // 00409A40

    // Chunks and fields of the current chunk. Writing appends; reading takes the current chunk's
    // occurrence of the child (missing: zero bytes, empty text, zero key).
    bool EnterChunk(int id);                          // 004098D0
    void LeaveChunk();                                // 00409980
    int CountChunks(int id);                          // 00409A60
    void Field(int id, void* data, unsigned size);    // 004092B0
    void CStringField(int id, mfc::CString* s);       // 004093A0 (written with its terminator)
    void StringField(int id, stlp::string* s);        // 00409470
    void StreamField(int id, CMemoryStream* s);       // 00409640: chunk `id` {1: size, 2: bytes}
    void SaveObjectRef(int id, CRefObject* object);   // 0040A440: object's number, object kept for Close
    CRefObject* ObjectRef(int id);                    // 0040A5A0: the object a saved key stands for

    // 00409AE0. Starts a load (`loading`, a byte: reads the index chunk 0, data chunk 2 and the
    // object records from `stream`) or a save (`version` kept for Close).
    void Open(CStream* stream, int loading, int version);
    // 0040A000. Ends a save: writes the saved objects' records, the version (field 2) and the index
    // and data chunks to `stream`; or, after a load, drops the loaded objects. Clears everything.
    void Close(CStream* stream);

    // Database arrays (model_db.cpp). Each is a vector of records serialised as chunk `id` holding
    // one chunk 1 per record (`occurrence` selects the record); loading replaces the vector's
    // contents. The third argument (the callers pass the database file's data) is unused but
    // popped: RET 0xC.
    void SerializePrints(int id, void* records, void* unused);      // 006D23A0 vector<PrintRecord>
    void SerializePrints2(int id, void* records, void* unused);     // 006D25F0 vector<PrintRecord2>
    void SerializeAcks(int id, void* records, void* unused);        // 006DB430 (0x1C4-byte records)
    void SerializeAcks2(int id, void* records, void* unused);       // 006DB680 (0x5C-byte records)
    void SerializeAcks3(int id, void* records, void* unused);       // 006DB8D0 (0x38-byte records)

    // The party manager's chunks (party_manager.cpp, A-S9). The third argument is unused but
    // popped (RET 0xC). Loading empties the container first and takes one record per chunk 1.
    // 004ACEC0. hash_map<int, CParty>: per party a chunk 1 (field 1: the key) and a chunk 2
    // (CParty::ArchiveParty); loading inserts each key (operator[]).
    void SerializeParties(int id, stlp::hash_map<int, PartyInPlace, stlp::IntHash>* parties, void* unused);
    // 004AD0C0. vector<PartyManagerEntry>: per entry a chunk 1 with string fields 1 and 2 and a
    // chunk 3 holding one chunk 1 (field 1) per id.
    void SerializePartyEntries(int id, stlp::vector<PartyManagerEntry>* entries, void* unused);
    // 004ACDC0. The class templates (counted pointers) as object references (field 1 per chunk 1).
    void SerializeClassTemplates(int id, stlp::vector<ClassTemplate*>* templates, void* unused);
    // 004AD2C0 (RET 8). Field(id, value, 4).
    void IntField(int id, int* value);

    // The item database's vectors (item_db_load.cpp, A-H18; RET 0xC, the third
    // argument unused): 006A7C20 quick items (0x8C bytes), 006A7D20 loot (0x7C), 006A7E10 quest
    // items (0x78), one chunk 1 per record.
    void SerializeQuickItems(int id, void* records, void* unused);
    void SerializeLoot(int id, void* records, void* unused);
    void SerializeQuestItems(int id, void* records, void* unused);
    // A-H20, the same for 006A7940 materials (0x50), 006A7A30 weapons (0xB4) and 006A7B30 armor
    // (0xC8).
    void SerializeMaterials(int id, void* records, void* unused);
    void SerializeWeapons(int id, void* records, void* unused);
    void SerializeArmors(int id, void* records, void* unused);

    // The spell database's vectors (item_db_load.cpp, A-H21; RET 0xC, the third argument unused),
    // as the item database's: 00684EF0 the spells (0x84), 00684FE0 the modifiers (0x24), 00684C50
    // the three lists of 0x34-byte records.
    void SerializeSpells(int id, void* records, void* unused);
    void SerializeSpellModifiers(int id, void* records, void* unused);
    void SerializeSpellLists(int id, void* records, void* unused);
    // 00683EB0 (RET 0xC; the third argument is unused). vector<int> as chunk `id`: one field 1 per
    // value; loading empties the vector and refills it with as many zeros as there are fields.
    void IntVectorField(int id, stlp::vector<int>* values, void* unused);

    // The quest database's vectors (quest_records.cpp; RET 0xC, the third argument unused): one
    // chunk 1 per record (its Serialize); loading destroys the records and resizes the vector to
    // the record chunks first. 006B1F20 the quests, 006B2010 the briefings.
    void SerializeQuests(int id, stlp::vector<QuestRecord>* quests, void* unused);
    void SerializeBriefings(int id, stlp::vector<QuestBriefingRecord>* briefings, void* unused);

    // The skill database's vectors (unit_db.cpp, A-U10; RET 0xC, the third argument unused): one
    // chunk 1 per record (its Serialize); loading destroys the records (00520D20 / 00520DE0) and
    // resizes the vector to the record chunks (005209D0 / 00520B20) first. 005207D0 the skills,
    // 005208C0 the skill levels.
    void SerializeSkills(int id, stlp::vector<SkillRecord>* skills, void* unused);
    void SerializeSkillLevels(int id, stlp::vector<SkillLevelRecord>* levels, void* unused);
    // 00521480 (RET 8). Chunk `id` holding `record` (its Serialize).
    void SkillLevelChunk(int id, SkillLevelRecord* record);
    // The unit database's vectors (unit_db.cpp, A-U11; the same shape, called by the Rebirth-patched
    // loader 0053A200): 0053DC10 the prototypes (destroyed by 0053EBC0, resized by 0053E2A0), 0053DD00
    // the NPCs (0053ED30 / 0053E520), 0053DF50 the models (0053EE70 / 0053E730), 0053E1B0 the level
    // records (their name destroyed / 0053E9B0).
    void SerializePrototypes(int id, stlp::vector<UnitPrototypeRecord>* records, void* unused);
    void SerializeNpcs(int id, stlp::vector<UnitNpcRecord>* records, void* unused);
    void SerializeModels(int id, stlp::vector<UnitModelRecord>* records, void* unused);
    void SerializeUnitLevels(int id, stlp::vector<UnitLevelRecord>* records, void* unused);
    // 0052DF40 (RET 0xC; unit_db.cpp, A-U11). A vector<CString> as chunk `id` (InlineStrings).
    void SerializeCStrings(int id, stlp::vector<mfc::CString>* strings, void* unused);

    // An archive built and destroyed inline on a function's stack, as 004A8310 / 004A8560 do
    // (0053A7B0 is the out-of-line constructor): two empty memory streams, the containers'
    // constructors (004A94A0, 004639D0, 00463AA0), version 1; the destructors in reverse order.
    void ConstructLocal();
    void DestructLocal();
    // 0053A7B0 (A-U8). The out-of-line constructor (the database loaders' archives): the same
    // steps as ConstructLocal, the streams' constructors and the containers' inlined. Returns this.
    ChunkArchive* Construct();
    // 004645A0 (A-E1). The out-of-line destructor (the database loaders' archives): DestructLocal's
    // steps with the containers' destructors inlined.
    void Destruct();

    CMemoryStream indexStream_;    // +0x00 chunk 0
    CMemoryStream dataStream_;     // +0x28 the chunk data (chunk 2 while loading the records)
    Stack stack_;                  // +0x50 the last chunk is the current one
    std::int32_t version_;         // +0x54 1 when constructed; field 2 of the root chunk
    std::uint8_t loading_;         // +0x58 Open stores the low byte of its argument (+0x59..0x5B unused)
    ObjectMap objects_;            // +0x5C
    SavedList saved_;              // +0x70 the objects whose records Close writes
};
static_assert(offsetof(ChunkArchive, dataStream_) == 0x28);
static_assert(offsetof(ChunkArchive, stack_) == 0x50);
static_assert(offsetof(ChunkArchive, version_) == 0x54);
static_assert(offsetof(ChunkArchive, loading_) == 0x58);
static_assert(offsetof(ChunkArchive, objects_) == 0x5C);
static_assert(offsetof(ChunkArchive, saved_) == 0x70);
static_assert(sizeof(ChunkArchive) == 0x74);

// An archive built and destroyed inline (ConstructLocal / DestructLocal), as the savers and the
// address book do on their stacks.
struct LocalArchive {
    LocalArchive() { archive.ConstructLocal(); }
    ~LocalArchive() { archive.DestructLocal(); }
    LocalArchive(const LocalArchive&) = delete;
    LocalArchive& operator=(const LocalArchive&) = delete;
    ChunkArchive archive;
};
static_assert(sizeof(LocalArchive) == 0x74 && offsetof(LocalArchive, archive) == 0);
static_assert(sizeof(ChunkArchive::ClassRegistry) == 0x28);

// Whole chunks of a stream (the archive's top-level chunks).
// 00409EA0 (__fastcall in, id; one stack argument): reads one chunk header and its bytes into `to`
// (at most 10,000,000). True when all of it was read.
bool __fastcall ReadStreamChunk(CStream* in, char* id, CMemoryStream* to);
// 00409F30 (__fastcall in, id in DL; two stack arguments): from the start of the stream, the
// `occurrence`-th chunk `id` into `to`; on a read error or the end, `to` is cleared and false.
bool __fastcall FindStreamChunk(CStream* in, int id, CMemoryStream* to, int occurrence);
// 0040A3C0 (__fastcall out, id in DL; one stack argument): writes `from` as a chunk.
bool __fastcall WriteStreamChunk(CStream* out, int id, CMemoryStream* from);
