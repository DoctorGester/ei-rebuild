// The tagged-chunk archive: the chunk primitives, entering and leaving chunks,
// fields, whole archives over a stream. See docs/notes/core.md.
#include "chunk_archive.h"
#include "game_strings.h"

#include "core_refcount.h"

#include "hash_tables_400.h"
#include "lib/bytes.h"
#include "lib/static_object.h"
#include <cstdint>
#include <cstring>
#include <string>

#include <cstdlib>
#include <string_view>
#include <type_traits>

using LoadedObjects = ChunkArchive::LoadedObjects;

namespace {

// The archive's bytes at `p`: a field's value (unaligned), or the text stored with its terminator.
template <typename T>
T ValueAt(const std::uint8_t* p) {
    T value;
    std::memcpy(&value, p, sizeof(T));
    return value;
}
const char* TextAt(const std::uint8_t* p) { return reinterpret_cast<const char*>(p); }

// The record chunk the Open / Close loops push on the stack: child 1 of the current chunk.
ChunkArchive::Ref* PushRecordChunk(ChunkArchive* a) {
    ChunkArchive::Ref* parent = a->Current();
    a->PushChunk();
    ChunkArchive::Ref* fresh = a->Current();
    if (a->loading_) {
        a->FindChild(parent, 1, fresh, parent->occurrence);
    } else {
        fresh->id = 1;
        fresh->offset = parent->size + parent->offset + 5;
    }
    return a->Current();
}

// The current chunk's `occurrence`-th child `id` (reading), searched from a default Ref.
bool FindField(ChunkArchive* a, int id, ChunkArchive::Ref* child) {
    ChunkArchive::Ref* current = a->Current();
    *child = ChunkArchive::Ref{0xFF, 0, 0, 1};
    return a->FindChild(current, id, child, current->occurrence);
}


} // namespace

// ---- Chunk primitives ----------------------------------------------------------------------------

void ChunkArchive::Ref::Reset() {
    id = 0xFF;
    occurrence = 1;
    offset = 0;
    size = 0;
}

// The class registry, zero at the start: built by its static initialiser in the list's order and destroyed by
// the exit function it registers (docs/notes/standalone.md, "The static initialisers' objects").
constinit lib::StaticObject<ChunkArchive::ClassRegistry> g_classRegistry = /*0079B270*/ {};

// A second block: it uses g_classRegistry (defined above).
namespace {
void DestroyClassRegistry() { g_classRegistry.Destroy(); }
} // namespace

void ConstructClassRegistry() {
    g_classRegistry.Construct();
    std::atexit(DestroyClassRegistry);
}

void ChunkArchive::PopChunk() {
    stack_.pop_back();
}

bool ChunkArchive::NextChild(const ChunkArchive::Ref* parent, std::int32_t* cursor, ChunkArchive::Ref* out) {
    const std::uint8_t* base = dataStream_.data_ + parent->offset;
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

bool ChunkArchive::AppendChild(ChunkArchive::Ref* chunk, int id, const void* data, std::uint32_t size) {
    dataStream_.Resize(static_cast<std::uint32_t>(chunk->offset) + size + static_cast<std::uint32_t>(chunk->size) + 5);
    std::uint8_t* base = dataStream_.data_ + chunk->offset;
    base[chunk->size] = static_cast<std::uint8_t>(id);
    ++chunk->size;
    std::uint32_t length = size * 2;
    if (length >= 0x100) {
        length |= 1;
        lib::StoreBytes(base + chunk->size, length);
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

// The callers inline this search in several places (Field, EnterChunk, the field readers).
bool ChunkArchive::FindChild(const ChunkArchive::Ref* parent, int id, ChunkArchive::Ref* out, int occurrence) {
    std::int32_t cursor = 0;
    while (NextChild(parent, &cursor, out)) {
        if (out->id == static_cast<std::uint8_t>(id)) {
            if (occurrence == 1) return true;
            --occurrence;
        }
    }
    return false;
}

int ChunkArchive::CountChildrenOf(const ChunkArchive::Ref* parent, int id) {
    ChunkArchive::Ref child{0xFF, 0, 0, 1};
    std::int32_t cursor = 0;
    int count = 0;
    while (NextChild(parent, &cursor, &child))
        if (child.id == static_cast<std::uint8_t>(id)) ++count;
    return count;
}

int ChunkArchive::CountChunks(int id) { return CountChildrenOf(Current(), id); }

void ChunkArchive::ResizeToCurrent() {
    ChunkArchive::Ref* current = Current();
    dataStream_.Resize(static_cast<std::uint32_t>(current->size + current->offset));
}

// ---- Chunks and fields ---------------------------------------------------------------------------

void ChunkArchive::Field(int id, void* data, unsigned size) {
    if (!loading_) {
        AppendChild(Current(), id, data, size);
        return;
    }
    ChunkArchive::Ref child;
    if (FindField(this, id, &child)) std::memcpy(data, dataStream_.data_ + child.offset, size);
    else std::memset(data, 0, size);   // a missing field reads as zero
}

bool ChunkArchive::EnterChunk(int id) {
    ChunkArchive::Ref* parent = Current();
    PushChunk();
    ChunkArchive::Ref* fresh = Current();
    // A failed search leaves the new chunk pushed (the callers do not pop it).
    if (loading_) return FindChild(parent, id, fresh, parent->occurrence);
    fresh->id = static_cast<std::uint8_t>(id);
    fresh->offset = parent->size + parent->offset + 5;
    return true;
}

void ChunkArchive::LeaveChunk() {
    if (loading_) {
        PopChunk();
        return;
    }
    // Writing: the chunk becomes a child of its parent (its content was written right after the
    // parent's, 5 bytes on to leave room for the header), then the stream is cut back to the parent.
    ChunkArchive::Ref* chunk = Current();
    const std::uint8_t* content = dataStream_.data_ + chunk->offset;   // taken before the parent's resize
    AppendChild(Parent(), chunk->id, content, static_cast<std::uint32_t>(chunk->size));
    PopChunk();
    ChunkArchive::Ref* parent = Current();
    dataStream_.Resize(static_cast<std::uint32_t>(parent->size + parent->offset));
}

// A string field, written with its terminator; read through an inlined assign (the empty
// string when missing).
void ChunkArchive::StringField(int id, std::string* s) {
    if (!loading_) {
        AppendChild(Current(), id, s->data(), static_cast<std::uint32_t>(std::strlen(s->data()) + 1));
        return;
    }
    ChunkArchive::Ref child;
    // string::assign(text, text + strlen(text)), inlined: overwrite in place and
    // cut the rest, or overwrite the whole string and append the remainder.
    if (FindField(this, id, &child)) s->assign(TextAt(dataStream_.data_ + child.offset));
    else s->assign(game::kEmptyText);
}

// A memory stream as chunk `id` (EnterChunk, inlined) holding its size (field 1) and its bytes
// (field 2, inlined Field); loading clears the stream and sizes it first. The stream is rewound;
// then the chunk is left (inlined LeaveChunk). A chunk missing while loading stays pushed, as with
// EnterChunk.
void ChunkArchive::StreamField(int id, CMemoryStream* s) {
    if (!EnterChunk(id)) return;
    std::uint32_t size = s->Size();
    Field(1, &size);
    if (loading_) {
        s->Clear();
        s->SetSize(size);
    }
    Field(2, s->data_, size);
    s->Seek(0);
    LeaveChunk();
}

// The number a save gives an object: its place in the saved list (1, 2, ...), written as the key (the loader maps
// any key to the object it creates); numbers by first save keep the saves free of heap layout.
std::uint32_t SavedNumber(const ChunkArchive* a, const CRefCounted* object) {
    auto node = a->numbers_.find(object);
    return node != a->numbers_.end() ? node->second : 0;
}

// An object pointer field. An object not yet in the map joins the saved list (written by Close)
// and the numbers (also while loading, where nothing reads the number). Writing
// then stores the number; reading looks the field up and ignores it.
void ChunkArchive::SaveObjectRef(int id, CRefCounted* object) {
    if (!numbers_.contains(object)) {
        saved_.insert(saved_.end(), object);
        std::uint32_t& number = numbers_[object];
        number = static_cast<std::uint32_t>(numbers_.size());
    }
    if (!loading_) {
        std::uint32_t number = SavedNumber(this, object);
        AppendChild(Current(), id, &number, 4);
        return;
    }
    ChunkArchive::Ref child;
    FindField(this, id, &child);   // searches and drops the result
}

// An object reference field: the key read (0 when missing; writing stores 0) is looked up in the
// map (inlined find); returns the object, or null.
CRefCounted* ChunkArchive::ObjectRef(int id) {
    std::uint32_t key = 0;
    if (!loading_) {
        AppendChild(Current(), id, &key, 4);
    } else {
        ChunkArchive::Ref child;
        if (FindField(this, id, &child)) lib::LoadBytes(key, dataStream_.data_ + child.offset);
        else key = 0;
    }
    auto node = loaded_.find(key);
    return node != loaded_.end() ? node->second : nullptr;
}

// ---- An archive on the stack ----------------------------------------------------------------------

ChunkArchive::ChunkArchive() {
    version_ = 1;
}

ChunkArchive::~ChunkArchive() {
}

// ---- Whole archives over a CStream ---------------------------------------------------------------

bool ReadStreamChunk(CStream* in, char* id, CMemoryStream* to) {
    std::uint32_t length = 0;
    in->Read(id, 1);
    in->Read(&length, 1);
    if (length & 1) in->Read(reinterpret_cast<std::uint8_t*>(&length) + 1, 3);
    length >>= 1;
    if (!in->ok_) return false;
    if (length > 10000000) return false;
    to->SetSize(length);
    return in->Read(to->data_, length) == length;
}

// ReadStreamChunk is inlined here; Open inlines all of this for the index and the records.
bool FindStreamChunk(CStream* in, int id, CMemoryStream* to, int occurrence) {
    in->Rewind();
    char readId;
    while (ReadStreamChunk(in, &readId, to)) {
        if (readId == static_cast<char>(id)) {
            if (occurrence == 1) return true;
            --occurrence;
        }
    }
    to->Clear();
    return false;
}

bool WriteStreamChunk(CStream* out, int id, CMemoryStream* from) {
    char tag = static_cast<char>(id);
    out->WriteValue(tag);
    std::uint32_t length = from->Size() * 2;
    if (length >= 0x100) {
        length |= 1;
        out->WriteValue(length);
    } else {
        out->Write(&length, 1);
    }
    std::uint8_t* data = from->data_;
    out->Write(data, from->Size());
    return true;
}

void ChunkArchive::Open(CStream* stream, int loading, int version) {
    stack_.clear();
    CMemoryStream* index = &indexStream_;
    CMemoryStream* data = &dataStream_;
    index->Clear();
    data->Clear();
    PushChunk();
    loading_ = static_cast<std::uint8_t>(loading);
    if (!static_cast<std::uint8_t>(loading)) {
        version_ = version;
        return;
    }
    FindStreamChunk(stream, 0, index, 1);
    FindStreamChunk(stream, 2, data, 1);
    Current()->size = static_cast<std::int32_t>(data->Size());

    // The index: per object its class id and the key its record is stored under.
    while (index->Position() < index->Size()) {
        std::uint32_t classId = 0;
        std::uint32_t key = 0;
        index->ReadValue(classId);
        index->ReadValue(key);
        Factory factory = g_classRegistry->factories[classId];
        CRefCounted* object = factory ? factory() : nullptr;
        saved_.insert(saved_.end(), object);
        loaded_[key] = object;
    }
    if (version == 1) Field(2, &version_);
    else version_ = version;

    int count = CountChildrenOf(Current(), 1);
    for (int i = 1; i <= count; ++i) {
        std::uint32_t key = 0;
        Current()->occurrence = i;
        ChunkArchive::Ref* record = PushRecordChunk(this);
        if (loading_) {
            ChunkArchive::Ref field;
            field.Reset();
            key = FindChild(record, 0, &field, record->occurrence) ? ValueAt<std::uint32_t>(dataStream_.data_ + field.offset) : 0;
        } else {
            AppendChild(record, 0, &key, 4);
        }
        CRefCounted* object = loaded_[key];
        if (object && EnterChunk(1)) {
            object->Serialize(this);
            LeaveChunk();
        }
        LeaveChunk();
    }
    Current()->Reset();
    FindStreamChunk(stream, 1, data, 1);
    Current()->size = static_cast<std::int32_t>(data->Size());
}

void ChunkArchive::Close(CStream* stream) {
    CMemoryStream* index = &indexStream_;
    CMemoryStream* data = &dataStream_;
    if (loading_) {
        // After a load: each loaded object gets a reference and loses it again (one without other
        // owners is deleted).
        for (auto& node : loaded_) {
            CRefCounted* object = node.second;
            if (!object) continue;
            ++object->refs_;
            if (--object->refs_ == 0) delete object;
        }
    } else {
        ResizeToCurrent();
        WriteStreamChunk(stream, 1, data);
        data->Clear();
        Current()->Reset();
        while (!saved_.empty()) {
            CRefCounted* object = saved_.front();
            saved_.pop_front();
            std::uint32_t vtable;
            lib::LoadBytes(vtable, object);
            const std::unordered_map<std::uint32_t, std::int32_t>& classIds = g_classRegistry->classIds;
            auto found = classIds.find(vtable);
            std::int32_t classId = found != classIds.end() ? found->second : -1;
            std::uint32_t number = SavedNumber(this, object);   // the number is the key, not the address
            index->WriteValue(classId);
            index->WriteValue(number);
            ChunkArchive::Ref* record = PushRecordChunk(this);
            if (loading_) {
                ChunkArchive::Ref field;
                field.Reset();
                object = FindChild(record, 0, &field, record->occurrence) ? ValueAt<CRefCounted*>(dataStream_.data_ + field.offset) : nullptr;
            } else {
                AppendChild(record, 0, &number, 4);
            }
            if (EnterChunk(1)) {
                object->Serialize(this);
                LeaveChunk();
            }
            if (loading_) {
                PopChunk();
            } else {
                // LeaveChunk inlined, with pop_back and ResizeToCurrent.
                ChunkArchive::Ref* chunk = Current();
                const std::uint8_t* content = dataStream_.data_ + chunk->offset;
                AppendChild(Parent(), chunk->id, content, static_cast<std::uint32_t>(chunk->size));
                PopChunk();
                ResizeToCurrent();
            }
        }
        Field(2, &version_);
        WriteStreamChunk(stream, 0, index);
        ResizeToCurrent();
        WriteStreamChunk(stream, 2, data);
    }
    index->Clear();
    data->Clear();
    loaded_.clear();
    numbers_.clear();
    saved_.clear();
    stack_.clear();
}
