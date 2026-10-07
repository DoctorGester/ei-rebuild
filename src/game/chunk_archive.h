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
struct ClassTemplateVector;
struct PartyInPlace;
struct PartyManagerEntry;
namespace mfc {
class CString;
}

class ChunkArchive {
public:
    // A chunk being read or written: where its content is in the data stream, and which
    // occurrence of a child id the next search for it takes.
    struct Ref {
        std::uint8_t id;
        std::uint8_t unknown_01[3];
        std::int32_t offset;     // of the content in dataStream_
        std::int32_t size;
        std::int32_t occurrence;

        // 00409E80. {0xFF, 0, 0, occurrence 1}.
        void Reset();
    };
    static_assert(sizeof(Ref) == 0x10);

    // A node of the chunk stack (STLport list<Ref>, 24-byte nodes from the node pool).
    struct StackNode {
        StackNode* next;
        StackNode* prev;
        Ref ref;                 // +0x08
    };
    static_assert(sizeof(StackNode) == 0x18);

    // A node of the list of objects to save (STLport list<CRefObject*>).
    struct SavedNode {
        SavedNode* next;
        SavedNode* prev;
        CRefObject* object;      // +0x08
    };

    // Key -> object: while loading, the objects the index names (by the key their records are
    // stored under); while saving, the objects already listed (value: the archive itself).
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
    Ref* Current() { return &stack_->prev->ref; }
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
    void SaveObjectRef(int id, CRefObject* object);   // 0040A440: object pointer, object kept for Close
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
    void SerializeClassTemplates(int id, ClassTemplateVector* templates, void* unused);
    // 004AD2C0 (RET 8). Field(id, value, 4).
    void IntField(int id, int* value);

    // An archive built and destroyed inline on a function's stack, as 004A8310 / 004A8560 do
    // (0053A7B0 is the out-of-line constructor): two empty memory streams, the containers'
    // constructors (004A94A0, 004639D0, 00463AA0), version 1; the destructors in reverse order.
    void ConstructLocal();
    void DestructLocal();

    CMemoryStream indexStream_;    // +0x00 chunk 0
    CMemoryStream dataStream_;     // +0x28 the chunk data (chunk 2 while loading the records)
    StackNode* stack_;             // +0x50 list sentinel; stack_->prev is the current chunk
    std::int32_t version_;         // +0x54 1 when constructed; field 2 of the root chunk
    std::uint8_t loading_;         // +0x58 Open stores the low byte of its argument (+0x59..0x5B unused)
    ObjectMap objects_;            // +0x5C
    SavedNode* saved_;             // +0x70 list sentinel: the objects whose records Close writes
};
static_assert(offsetof(ChunkArchive, dataStream_) == 0x28);
static_assert(offsetof(ChunkArchive, stack_) == 0x50);
static_assert(offsetof(ChunkArchive, version_) == 0x54);
static_assert(offsetof(ChunkArchive, loading_) == 0x58);
static_assert(offsetof(ChunkArchive, objects_) == 0x5C);
static_assert(offsetof(ChunkArchive, saved_) == 0x70);
static_assert(sizeof(ChunkArchive) == 0x74);
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
