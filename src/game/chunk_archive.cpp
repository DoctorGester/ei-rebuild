// The tagged-chunk archive (model.h, ChunkArchive): the chunk primitives, entering and leaving
// chunks, fields. 00409070-0040A000. See docs/notes/unmapped_410_470.md ("Fourth round").
#include "model.h"
#include "object.h"

#include "core_refcount.h"
#include "ei/hook.h"
#include "ei/vcall.h"

#include <cstring>

namespace {

// 004A54D0: CMemoryStream resize (grows to 1.5x + 0x10 when needed; the position goes to 0).
__declspec(naked) void __fastcall StreamResize(CMemoryStream* /*stream*/, void*, std::uint32_t /*size*/) EI_ORIGINAL(0x004A54D0)
// The chunk stack (STLport list<Ref>, 24-byte nodes): insert a default Ref {0xFF, 0, 0, 1} before
// `pos`, erase.
__declspec(naked) void* __fastcall StackInsertDefault(void* /*list*/, void*, void** /*out*/, void* /*pos*/) EI_ORIGINAL(0x0040A8D0)
__declspec(naked) void* __fastcall StackErase(void* /*list*/, void*, void** /*out*/, void* /*pos*/) EI_ORIGINAL(0x0040A920)

using Ref = ChunkArchive::Ref;

Ref* RefOf(ChunkNode* node) { return reinterpret_cast<Ref*>(reinterpret_cast<std::uint8_t*>(node) + 8); }
Ref* Current(ChunkArchive* a) { return RefOf(a->stack_->prev); }
std::uint8_t* Data(ChunkArchive* a) { return *reinterpret_cast<std::uint8_t**>(a->streams_ + 0x40); }
CMemoryStream* DataStream(ChunkArchive* a) { return reinterpret_cast<CMemoryStream*>(a->streams_ + 0x28); }

} // namespace

bool ChunkArchive::NextChild(const Ref* parent, std::int32_t* cursor, Ref* out) {
    const std::uint8_t* base = Data(this) + parent->offset;
    std::uint32_t length = 0;
    if (*cursor + 2 > parent->size) return false;
    out->id = base[*cursor];
    ++*cursor;
    std::uint8_t first = base[*cursor];
    ++*cursor;
    length = first;
    if (first & 1) {
        std::memcpy(reinterpret_cast<std::uint8_t*>(&length) + 1, base + *cursor, 3);
        *cursor += 3;
    }
    length >>= 1;
    if (static_cast<std::uint32_t>(*cursor) + length > static_cast<std::uint32_t>(parent->size)) return false;
    out->size = static_cast<std::int32_t>(length);
    out->offset = parent->offset + *cursor;
    *cursor += static_cast<std::int32_t>(length);
    return true;
}
EI_REPLACE_METHOD(0x00409070, ChunkArchive::NextChild);

bool ChunkArchive::AppendChild(Ref* chunk, int id, const void* data, std::uint32_t size) {
    StreamResize(DataStream(this), nullptr, static_cast<std::uint32_t>(chunk->offset) + size + static_cast<std::uint32_t>(chunk->size) + 5);
    std::uint8_t* base = Data(this) + chunk->offset;
    base[chunk->size] = static_cast<std::uint8_t>(id);
    ++chunk->size;
    std::uint32_t length = size * 2;
    if (length >= 0x100) {
        length |= 1;
        std::memcpy(base + chunk->size, &length, 4);
        chunk->size += 4;
    } else {
        base[chunk->size] = static_cast<std::uint8_t>(length);
        chunk->size += 1;
    }
    std::uint8_t* to = base + chunk->size;
    // A forward copy: a child moves down over its own header when its chunk is closed (LeaveChunk).
    if (to != data) std::memmove(to, data, size);
    chunk->size += static_cast<std::int32_t>(size);
    return true;
}
EI_REPLACE_METHOD(0x00409110, ChunkArchive::AppendChild);

bool ChunkArchive::FindChild(const Ref* parent, int id, Ref* out, int occurrence) {
    std::int32_t cursor = 0;
    while (NextChild(parent, &cursor, out)) {
        if (out->id == static_cast<std::uint8_t>(id)) {
            if (occurrence == 1) return true;
            --occurrence;
        }
    }
    return false;
}
EI_REPLACE_METHOD(0x004091C0, ChunkArchive::FindChild);

int ChunkArchive::CountChildrenOf(const Ref* parent, int id) {
    Ref child{0xFF, {}, 0, 0, 1};
    std::int32_t cursor = 0;
    int count = 0;
    while (NextChild(parent, &cursor, &child))
        if (child.id == static_cast<std::uint8_t>(id)) ++count;
    return count;
}
EI_REPLACE_METHOD(0x00409230, ChunkArchive::CountChildrenOf);

int ChunkArchive::CountChunks(int id) { return CountChildrenOf(Current(this), id); }
EI_REPLACE_METHOD(0x00409A60, ChunkArchive::CountChunks);

void ChunkArchive::ResizeToCurrent() {
    Ref* current = Current(this);
    StreamResize(DataStream(this), nullptr, static_cast<std::uint32_t>(current->size + current->offset));
}
EI_REPLACE_METHOD(0x00409A40, ChunkArchive::ResizeToCurrent);

void ChunkArchive::Field(int id, void* data, unsigned size) {
    Ref* current = Current(this);
    if (!loading_) {
        AppendChild(current, id, data, size);
        return;
    }
    int occurrence = current->occurrence;
    Ref child{0xFF, {}, 0, 0, 1};
    std::int32_t cursor = 0;
    while (NextChild(current, &cursor, &child)) {
        if (child.id == static_cast<std::uint8_t>(id)) {
            if (occurrence == 1) {
                std::memcpy(data, Data(this) + child.offset, size);
                return;
            }
            --occurrence;
        }
    }
    std::memset(data, 0, size);   // a missing field reads as zero
}
EI_REPLACE_METHOD(0x004092B0, ChunkArchive::Field);

bool ChunkArchive::EnterChunk(int id) {
    Ref* parent = Current(this);
    void* inserted;
    StackInsertDefault(&stack_, nullptr, &inserted, stack_);
    Ref* fresh = Current(this);
    if (loading_) {
        // A failed search leaves the new chunk pushed (the callers do not pop it).
        int occurrence = parent->occurrence;
        std::int32_t cursor = 0;
        while (NextChild(parent, &cursor, fresh)) {
            if (fresh->id == static_cast<std::uint8_t>(id)) {
                if (occurrence == 1) return true;
                --occurrence;
            }
        }
        return false;
    }
    fresh->id = static_cast<std::uint8_t>(id);
    fresh->offset = parent->size + parent->offset + 5;
    return true;
}
EI_REPLACE_METHOD(0x004098D0, ChunkArchive::EnterChunk);

void ChunkArchive::LeaveChunk() {
    void* after;
    if (loading_) {
        StackErase(&stack_, nullptr, &after, stack_->prev);
        return;
    }
    // Writing: the chunk becomes a child of its parent (its content was written right after the
    // parent's, 5 bytes on to leave room for the header), then the stream is cut back to the parent.
    ChunkNode* node = stack_->prev;
    Ref* chunk = RefOf(node);
    const std::uint8_t* content = Data(this) + chunk->offset;   // taken before the parent's resize
    AppendChild(RefOf(node->prev), chunk->id, content, static_cast<std::uint32_t>(chunk->size));
    StackErase(&stack_, nullptr, &after, stack_->prev);
    Ref* parent = Current(this);
    StreamResize(DataStream(this), nullptr, static_cast<std::uint32_t>(parent->size + parent->offset));
}
EI_REPLACE_METHOD(0x00409980, ChunkArchive::LeaveChunk);

// ---- Whole archives over a CStream --------------------------------------------------------------

bool __fastcall ReadStreamChunk(void* in, char* id, CMemoryStream* to);

namespace {

// More STLport instantiations of the archive's containers (library).
__declspec(naked) void __fastcall StackDestroyNode(void* /*list*/, void*, void* /*node*/, int /*n*/) EI_ORIGINAL(0x0040ACE0)
__declspec(naked) void __fastcall StackPushDefault(void* /*list*/) EI_ORIGINAL(0x0040A6B0)
__declspec(naked) void __fastcall StackPopBack(void* /*list*/) EI_ORIGINAL(0x0040A6D0)
__declspec(naked) void __fastcall StackClear(void* /*list*/) EI_ORIGINAL(0x0040B290)
// The saved-object list at +0x70 (list<CRefObject*>): end, insert, begin, erase, clear.
__declspec(naked) void** __fastcall SavedEnd(void* /*list*/, void*, void** /*out*/) EI_ORIGINAL(0x0040AA00)
__declspec(naked) void* __fastcall SavedInsert(void* /*list*/, void*, void** /*out*/, void* /*pos*/, void* const* /*value*/) EI_ORIGINAL(0x0040AA10)
__declspec(naked) void** __fastcall SavedBegin(void* /*list*/, void*, void** /*out*/) EI_ORIGINAL(0x0040A9F0)
__declspec(naked) void* __fastcall SavedErase(void* /*list*/, void*, void** /*out*/, void* /*pos*/) EI_ORIGINAL(0x0040AA40)
__declspec(naked) void __fastcall SavedClear(void* /*list*/) EI_ORIGINAL(0x0040B390)
// The loaded-object map at +0x5C (hash_map<int, CRefObject*>): operator[] of a pair, begin,
// iterator constructor and increment, clear; the pair constructor.
__declspec(naked) std::uint32_t* __fastcall LoadedAt(void* /*map*/, void*, const std::uint32_t* /*pair*/) EI_ORIGINAL(0x0040B2C0)
__declspec(naked) void* __fastcall LoadedBegin(void* /*map*/, void*, void** /*out*/) EI_ORIGINAL(0x0040A970)
__declspec(naked) void* __fastcall HashIterator(void** /*it*/, void*, void* /*node*/, void* /*table*/) EI_ORIGINAL(0x0040AD20)
__declspec(naked) void* __fastcall LoadedNext(void** /*it*/, void*, void** /*out*/, int /*postfix*/) EI_ORIGINAL(0x0040A700)
__declspec(naked) void __fastcall LoadedClear(void* /*map*/) EI_ORIGINAL(0x0040B330)
__declspec(naked) std::uint32_t* __fastcall MakeIntPair(std::uint32_t* /*pair*/, void*, const std::uint32_t* /*key*/,
                                                        const std::uint32_t* /*value*/) EI_ORIGINAL(0x0040A9D0)
// 004A5570: CMemoryStream SetSize (grows to the exact size when needed; position 0).
__declspec(naked) void __fastcall StreamSetSize(CMemoryStream* /*stream*/, void*, std::uint32_t /*size*/) EI_ORIGINAL(0x004A5570)
// The class tables: 0079B284 class id -> factory (operator[]), 0079B270 vtable -> class id (find).
__declspec(naked) void** __fastcall FactoryAt(void* /*map*/, void*, const std::uint32_t* /*key*/) EI_ORIGINAL(0x0040A800)
__declspec(naked) void* __fastcall ClassFind(void* /*map*/, void*, void** /*out*/, const std::uint32_t* /*key*/) EI_ORIGINAL(0x0040B400)
// 0040A770: a counted pointer from a raw pointer (adds a reference).
__declspec(naked) void* __fastcall CountedFromRaw(void** /*p*/, void*, void* /*object*/) EI_ORIGINAL(0x0040A770)

void* Factories() { return reinterpret_cast<void*>(0x0079B284); }
void* ClassIds() { return reinterpret_cast<void*>(0x0079B270); }
void* Member(ChunkArchive* a, std::size_t offset) { return reinterpret_cast<std::uint8_t*>(a) + offset; }
CMemoryStream* IndexStream(ChunkArchive* a) { return reinterpret_cast<CMemoryStream*>(a->streams_); }
std::int32_t& Version(ChunkArchive* a) { return *reinterpret_cast<std::int32_t*>(&a->unknown_54); }

// CStream slots: 1 Read(buffer, n) -> count, 2 Write(buffer, n), 3 Seek(position), 4 Size,
// 5 Position, 6 Clear.
std::uint32_t StreamRead(void* s, void* to, std::uint32_t n) { return ei::VCall<std::uint32_t>(s, 1, to, n); }
void StreamWrite(void* s, const void* from, std::uint32_t n) { ei::VCall<void>(s, 2, from, n); }
std::uint32_t StreamSize(void* s) { return ei::VCall<std::uint32_t>(s, 4); }
std::uint32_t StreamPosition(void* s) { return ei::VCall<std::uint32_t>(s, 5); }
void StreamClear(void* s) { ei::VCall<void>(s, 6); }
void StreamRewind(void* s) {
    static_cast<std::uint8_t*>(s)[5] = 1;   // ok
    ei::VCall<void>(s, 3, 0);
    static_cast<std::uint8_t*>(s)[4] = 1;
}


// The chunk loop of Open: the `occurrence`-th chunk `id` of `in` read into `to`, else `to` cleared.
void LoadTopChunk(void* in, char wanted, CMemoryStream* to, void* clearTarget) {
    char id;
    int occurrence = 1;
    bool ok = ReadStreamChunk(in, &id, to);
    while (ok) {
        if (id == wanted) {
            if (occurrence == 1) return;
            --occurrence;
        }
        ok = ReadStreamChunk(in, &id, to);
    }
    StreamClear(clearTarget);
}

// The record chunk the Open / Close loops push on the stack: child 1 of the current chunk.
Ref* PushRecordChunk(ChunkArchive* a) {
    Ref* parent = Current(a);
    StackPushDefault(&a->stack_);
    Ref* fresh = Current(a);
    if (a->loading_) {
        a->FindChild(parent, 1, fresh, parent->occurrence);
    } else {
        fresh->id = 1;
        fresh->offset = parent->size + parent->offset + 5;
    }
    return Current(a);
}

} // namespace

bool __fastcall ReadStreamChunk(void* in, char* id, CMemoryStream* to);

// 00409E80: a chunk reference reset to {0xFF, 0, 0, occurrence 1}.
void __fastcall ChunkRefReset(Ref* r) {
    r->id = 0xFF;
    r->occurrence = 1;
    r->offset = 0;
    r->size = 0;
}
EI_REPLACE(0x00409E80, ChunkRefReset);

// 00409EA0 (__fastcall in, id; one stack argument): reads one chunk header from the stream and its
// bytes into `to` (at most 10,000,000). True when all of it was read.
bool __fastcall ReadStreamChunk(void* in, char* id, CMemoryStream* to) {
    std::uint32_t length = 0;
    StreamRead(in, id, 1);
    StreamRead(in, &length, 1);
    if (length & 1) StreamRead(in, reinterpret_cast<std::uint8_t*>(&length) + 1, 3);
    length >>= 1;
    if (!static_cast<std::uint8_t*>(in)[5]) return false;
    if (length > 10000000) return false;
    StreamSetSize(to, nullptr, length);
    return StreamRead(in, to->data_, length) == length;
}
EI_REPLACE(0x00409EA0, ReadStreamChunk);

// 00409F30 (__fastcall in, id in DL; two stack arguments): from the start of the stream, the
// `occurrence`-th chunk `id` into `to`; on a read error or the end, `to` is cleared and false.
bool __fastcall FindStreamChunk(void* in, int id, CMemoryStream* to, int occurrence) {
    StreamRewind(in);
    for (;;) {
        char readId;
        std::uint32_t length = 0;
        StreamRead(in, &readId, 1);
        StreamRead(in, &length, 1);
        if (length & 1) StreamRead(in, reinterpret_cast<std::uint8_t*>(&length) + 1, 3);
        length >>= 1;
        if (!static_cast<std::uint8_t*>(in)[5] || length > 10000000) break;
        StreamSetSize(to, nullptr, length);
        if (StreamRead(in, to->data_, length) != length) break;
        if (readId == static_cast<char>(id)) {
            if (occurrence == 1) return true;
            --occurrence;
        }
    }
    StreamClear(to);
    return false;
}
EI_REPLACE(0x00409F30, FindStreamChunk);

// 0040A3C0 (__fastcall out, id in DL; one stack argument): writes `from` as a chunk.
bool __fastcall WriteStreamChunk(void* out, int id, CMemoryStream* from) {
    char tag = static_cast<char>(id);
    StreamWrite(out, &tag, 1);
    std::uint32_t length = StreamSize(from) * 2;
    if (length >= 0x100) {
        length |= 1;
        StreamWrite(out, &length, 4);
    } else {
        StreamWrite(out, &length, 1);
    }
    std::uint8_t* data = from->data_;
    StreamWrite(out, data, StreamSize(from));
    return true;
}
EI_REPLACE(0x0040A3C0, WriteStreamChunk);

void ChunkArchive::Open(void* stream, int loading, int version) {
    ChunkNode* s = stack_;
    for (ChunkNode* n = s->next; n != stack_;) {
        ChunkNode* dead = n;
        n = n->next;
        StackDestroyNode(&stack_, nullptr, dead, 1);
    }
    stack_->next = stack_;
    stack_->prev = stack_;
    StreamClear(IndexStream(this));
    StreamClear(DataStream(this));
    void* inserted;
    StackInsertDefault(&stack_, nullptr, &inserted, stack_);
    *reinterpret_cast<std::uint8_t*>(&loading_) = static_cast<std::uint8_t>(loading);
    if (!static_cast<std::uint8_t>(loading)) {
        Version(this) = version;
        return;
    }
    StreamRewind(stream);
    LoadTopChunk(stream, 0, IndexStream(this), IndexStream(this));
    StreamRewind(stream);
    LoadTopChunk(stream, 2, DataStream(this), DataStream(this));
    Current(this)->size = static_cast<std::int32_t>(StreamSize(DataStream(this)));

    // The index: per object its class id and the key its record is stored under.
    while (StreamPosition(IndexStream(this)) < StreamSize(IndexStream(this))) {
        std::uint32_t classId = 0;
        std::uint32_t key = 0;
        StreamRead(IndexStream(this), &classId, 4);
        StreamRead(IndexStream(this), &key, 4);
        std::uint32_t lookup = classId;
        auto factory = reinterpret_cast<void* (__cdecl*)()>(*FactoryAt(Factories(), nullptr, &lookup));
        void* object = factory ? factory() : nullptr;
        void* end;
        SavedEnd(Member(this, 0x70), nullptr, &end);
        void* added;
        SavedInsert(Member(this, 0x70), nullptr, &added, end, &object);
        std::uint32_t zero = 0;
        std::uint32_t pair[2];
        MakeIntPair(pair, nullptr, &key, &zero);
        LoadedAt(Member(this, 0x5C), nullptr, pair)[1] = reinterpret_cast<std::uint32_t>(object);
    }
    if (version == 1) Field(2, &Version(this), 4);
    else Version(this) = version;

    int count = CountChildrenOf(Current(this), 1);
    for (int i = 1; i <= count; ++i) {
        std::uint32_t key = 0;
        Current(this)->occurrence = i;
        Ref* record = PushRecordChunk(this);
        if (loading_) {
            Ref field;
            ChunkRefReset(&field);
            key = FindChild(record, 0, &field, record->occurrence) ? *reinterpret_cast<std::uint32_t*>(Data(this) + field.offset) : 0;
        } else {
            AppendChild(record, 0, &key, 4);
        }
        std::uint32_t zero = 0;
        std::uint32_t pair[2];
        MakeIntPair(pair, nullptr, &key, &zero);
        void* object = reinterpret_cast<void*>(LoadedAt(Member(this, 0x5C), nullptr, pair)[1]);
        if (object && EnterChunk(1)) {
            ei::VCall<void>(object, 1, this);   // Serialize
            LeaveChunk();
        }
        LeaveChunk();
    }
    Ref* top = Current(this);
    top->occurrence = 1;
    top->offset = 0;
    top->size = 0;
    top->occurrence = 1;
    top->id = 0xFF;
    FindStreamChunk(stream, 1, DataStream(this), 1);
    Current(this)->size = static_cast<std::int32_t>(StreamSize(DataStream(this)));
}
EI_REPLACE_METHOD(0x00409AE0, ChunkArchive::Open);

void ChunkArchive::Close(void* stream) {
    if (loading_) {
        // After a load: each loaded object gets a reference and loses it again (one without other
        // owners is deleted).
        void* it[2];
        LoadedBegin(Member(this, 0x5C), nullptr, it);
        void* end[2];
        HashIterator(end, nullptr, nullptr, Member(this, 0x5C));
        while (it[0] != end[0]) {
            void* counted;
            CountedFromRaw(&counted, nullptr, *reinterpret_cast<void**>(static_cast<std::uint8_t*>(it[0]) + 8));
            if (counted) static_cast<CRefObject*>(counted)->Release();
            void* old[2];
            LoadedNext(it, nullptr, old, 0);
            HashIterator(end, nullptr, nullptr, Member(this, 0x5C));
        }
    } else {
        Ref* top = Current(this);
        StreamResize(DataStream(this), nullptr, static_cast<std::uint32_t>(top->size + top->offset));
        WriteStreamChunk(stream, 1, DataStream(this));
        StreamClear(DataStream(this));
        top = Current(this);
        top->id = 0xFF;
        top->occurrence = 1;
        top->offset = 0;
        top->size = 0;
        void* saved = Member(this, 0x70);
        while (**static_cast<void***>(saved) != *static_cast<void**>(saved)) {
            void* first;
            SavedBegin(saved, nullptr, &first);
            void* object = *reinterpret_cast<void**>(static_cast<std::uint8_t*>(first) + 8);
            void* position;
            SavedBegin(saved, nullptr, &position);
            void* after;
            SavedErase(saved, nullptr, &after, position);
            std::uint32_t vtable;
            std::memcpy(&vtable, object, 4);
            void* found[2];
            ClassFind(ClassIds(), nullptr, found, &vtable);
            void* end[2];
            HashIterator(end, nullptr, nullptr, ClassIds());
            std::int32_t classId = found[0] == end[0] ? -1 : *reinterpret_cast<std::int32_t*>(static_cast<std::uint8_t*>(found[0]) + 8);
            StreamWrite(IndexStream(this), &classId, 4);
            StreamWrite(IndexStream(this), &object, 4);
            Ref* record = PushRecordChunk(this);
            if (loading_) {
                Ref field;
                ChunkRefReset(&field);
                object = FindChild(record, 0, &field, record->occurrence) ? *reinterpret_cast<void**>(Data(this) + field.offset) : nullptr;
            } else {
                AppendChild(record, 0, &object, 4);
            }
            if (EnterChunk(1)) {
                ei::VCall<void>(object, 1, this);   // Serialize
                LeaveChunk();
            }
            if (loading_) {
                StackPopBack(&stack_);
            } else {
                ChunkNode* node = stack_->prev;
                Ref* chunk = RefOf(node);
                const std::uint8_t* content = Data(this) + chunk->offset;
                AppendChild(RefOf(node->prev), chunk->id, content, static_cast<std::uint32_t>(chunk->size));
                StackPopBack(&stack_);
                ResizeToCurrent();
            }
        }
        Field(2, &Version(this), 4);
        WriteStreamChunk(stream, 0, IndexStream(this));
        top = Current(this);
        StreamResize(DataStream(this), nullptr, static_cast<std::uint32_t>(top->size + top->offset));
        WriteStreamChunk(stream, 2, DataStream(this));
    }
    StreamClear(IndexStream(this));
    StreamClear(DataStream(this));
    LoadedClear(Member(this, 0x5C));
    SavedClear(Member(this, 0x70));
    StackClear(&stack_);
}
EI_REPLACE_METHOD(0x0040A000, ChunkArchive::Close);

// ---- String, stream and object fields ------------------------------------------------------------

namespace {

// 0079B298: the empty string the readers fall back to.
const char* EmptyText() { return reinterpret_cast<const char*>(0x0079B298); }

// STLport string pieces used by StringField's inlined assign: erase(first, last) and the
// range append (its third argument is the string again).
struct RawString {
    char* start;
    char* finish;
    char* eos;
};
static_assert(sizeof(RawString) == sizeof(stlp::string));
__declspec(naked) void __fastcall StringErase(RawString* /*s*/, void*, char* /*first*/, char* /*last*/) EI_ORIGINAL(0x0040B3C0)
__declspec(naked) void __fastcall StringAppendRange(RawString* /*s*/, void*, const char* /*first*/, const char* /*last*/,
                                                    RawString* /*same*/) EI_ORIGINAL(0x0040C2A0)

// string::assign(text, text + strlen(text)) as StringField inlines it: overwrite in place and cut
// the rest, or overwrite the whole string and append the remainder.
void AssignText(RawString* s, const char* text) {
    std::size_t length = std::strlen(text);
    std::size_t size = static_cast<std::size_t>(s->finish - s->start);
    if (length <= size) {
        std::memcpy(s->start, text, length);
        StringErase(s, nullptr, s->start + length, s->finish);
    } else {
        std::memcpy(s->start, text, size);
        StringAppendRange(s, nullptr, text + size, text + length, s);
    }
}

// The current chunk's `occurrence`-th child `id` (reading), as the field readers search it.
bool FindField(ChunkArchive* a, int id, Ref* child) {
    Ref* current = Current(a);
    int occurrence = current->occurrence;
    *child = Ref{0xFF, {}, 0, 0, 1};
    std::int32_t cursor = 0;
    while (a->NextChild(current, &cursor, child)) {
        if (child->id == static_cast<std::uint8_t>(id)) {
            if (occurrence == 1) return true;
            --occurrence;
        }
    }
    return false;
}

std::uint32_t& Word(ChunkArchive* a, std::size_t offset) {
    return *reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uint8_t*>(a) + offset);
}

// The object map at +0x5C (buckets +0x60..+0x64, nodes {next, key, value}): find, inlined.
void** FindObjectNode(ChunkArchive* a, std::uint32_t key) {
    auto** buckets = reinterpret_cast<void***>(Word(a, 0x60));
    std::uint32_t count = static_cast<std::uint32_t>(static_cast<std::int32_t>(Word(a, 0x64) - Word(a, 0x60)) >> 2);
    void** node = buckets[key % count];
    while (node && reinterpret_cast<std::uint32_t>(node[1]) != key) node = static_cast<void**>(node[0]);
    return node;
}

// CMemoryStream slots: 3 Seek, 4 Size, 6 Clear.
constexpr int kStreamSeek = 3;
constexpr int kStreamSize = 4;
constexpr int kStreamClear = 6;

} // namespace

void ChunkArchive::CStringField(int id, mfc::CString* s) {
    if (!loading_) {
        const char* text = *s;
        AppendChild(Current(this), id, text, static_cast<std::uint32_t>(reinterpret_cast<const int*>(text)[-2] + 1));
        return;
    }
    Ref child;
    if (FindField(this, id, &child)) *s = reinterpret_cast<const char*>(Data(this) + child.offset);
    else *s = EmptyText();
}
EI_REPLACE_METHOD(0x004093A0, ChunkArchive::CStringField);

// 00409470: an STLport string field, written with its terminator; read through an inlined assign
// (the empty string when missing).
void ChunkArchive::StringField(int id, stlp::string* s) {
    auto* raw = reinterpret_cast<RawString*>(s);
    if (!loading_) {
        AppendChild(Current(this), id, raw->start, static_cast<std::uint32_t>(std::strlen(raw->start) + 1));
        return;
    }
    Ref child;
    if (FindField(this, id, &child)) AssignText(raw, reinterpret_cast<const char*>(Data(this) + child.offset));
    else AssignText(raw, EmptyText());
}
EI_REPLACE_METHOD(0x00409470, ChunkArchive::StringField);

// 00409640: a memory stream as chunk `id` (EnterChunk, inlined) holding its size (field 1) and its
// bytes (field 2, inlined Field); loading clears the stream and sizes it first. The stream is
// rewound; then the chunk is left (inlined LeaveChunk). A chunk missing while loading stays
// pushed, as with EnterChunk.
void ChunkArchive::StreamField(int id, CMemoryStream* s) {
    if (!EnterChunk(id)) return;
    std::uint32_t size = ei::VCall<std::uint32_t>(s, kStreamSize);
    Field(1, &size, 4);
    if (loading_) {
        ei::VCall<void>(s, kStreamClear);
        StreamSetSize(s, nullptr, size);
    }
    Field(2, s->data_, size);
    ei::VCall<int>(s, kStreamSeek, 0);
    LeaveChunk();
}
EI_REPLACE_METHOD(0x00409640, ChunkArchive::StreamField);

// 0040A440: an object pointer field. An object not yet in the map at +0x5C joins the saved list
// (+0x70, written by Close) and the map (value: the archive). Writing then stores the pointer;
// reading looks the field up and ignores it.
void ChunkArchive::SaveObjectRef(int id, void* object) {
    std::uint32_t key = reinterpret_cast<std::uint32_t>(object);
    if (!FindObjectNode(this, key)) {
        void* saved = reinterpret_cast<std::uint8_t*>(this) + 0x70;
        void* end;
        SavedEnd(saved, nullptr, &end);
        void* added;
        SavedInsert(saved, nullptr, &added, end, &object);
        std::uint32_t pair[2] = {key, 0};
        LoadedAt(reinterpret_cast<std::uint8_t*>(this) + 0x5C, nullptr, pair)[1] = reinterpret_cast<std::uint32_t>(this);
    }
    if (!loading_) {
        AppendChild(Current(this), id, &object, 4);
        return;
    }
    Ref child;
    FindField(this, id, &child);   // the original searches and drops the result
}
EI_REPLACE_METHOD(0x0040A440, ChunkArchive::SaveObjectRef);

// 0040A5A0: an object reference field: the key read (0 when missing; writing stores 0) is looked
// up in the map at +0x5C; returns the object, or null.
void* ChunkArchive::ObjectRef(int id) {
    std::uint32_t key = 0;
    if (!loading_) {
        AppendChild(Current(this), id, &key, 4);
    } else {
        Ref child;
        if (FindField(this, id, &child)) std::memcpy(&key, Data(this) + child.offset, 4);
        else key = 0;
    }
    void** node = FindObjectNode(this, key);
    return node ? node[2] : nullptr;
}
EI_REPLACE_METHOD(0x0040A5A0, ChunkArchive::ObjectRef);
