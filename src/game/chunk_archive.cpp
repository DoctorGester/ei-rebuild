// The tagged-chunk archive (chunk_archive.h): the chunk primitives, entering and leaving chunks,
// fields, whole archives over a stream. 00409070-0040A5A0. See docs/notes/core.md.
#include "chunk_archive.h"
#include "game_strings.h"

#include "core_refcount.h"

#include "ei/hook.h"
#include "lib/mfc.h"

#include <cstring>

using Ref = ChunkArchive::Ref;
using Stack = ChunkArchive::Stack;
using SavedList = ChunkArchive::SavedList;
using ObjectMap = ChunkArchive::ObjectMap;
using RefElement = ChunkArchive::RefElement;

namespace {

// The record chunk the Open / Close loops push on the stack: child 1 of the current chunk.
Ref* PushRecordChunk(ChunkArchive* a) {
    Ref* parent = a->Current();
    a->PushChunk();
    Ref* fresh = a->Current();
    if (a->loading_) {
        a->FindChild(parent, 1, fresh, parent->occurrence);
    } else {
        fresh->id = 1;
        fresh->offset = parent->size + parent->offset + 5;
    }
    return a->Current();
}

// The current chunk's `occurrence`-th child `id` (reading), searched from a default Ref.
bool FindField(ChunkArchive* a, int id, Ref* child) {
    Ref* current = a->Current();
    *child = Ref{0xFF, {}, 0, 0, 1};
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
EI_REWRITTEN(0x00409E80, ChunkArchive::Ref::Reset);

void ChunkArchive::PopChunk() {
    Stack::Erase<RefElement>(stack_.End()->prev);
}

bool ChunkArchive::NextChild(const Ref* parent, std::int32_t* cursor, Ref* out) {
    const std::uint8_t* base = Data() + parent->offset;
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
EI_REWRITTEN(0x00409070, ChunkArchive::NextChild);

bool ChunkArchive::AppendChild(Ref* chunk, int id, const void* data, std::uint32_t size) {
    dataStream_.Resize(static_cast<std::uint32_t>(chunk->offset) + size + static_cast<std::uint32_t>(chunk->size) + 5);
    std::uint8_t* base = Data() + chunk->offset;
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

// The callers inline this search in several places (Field, EnterChunk, the field readers).
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
EI_REWRITTEN(0x004091C0, ChunkArchive::FindChild);

int ChunkArchive::CountChildrenOf(const Ref* parent, int id) {
    Ref child{0xFF, {}, 0, 0, 1};
    std::int32_t cursor = 0;
    int count = 0;
    while (NextChild(parent, &cursor, &child))
        if (child.id == static_cast<std::uint8_t>(id)) ++count;
    return count;
}
EI_REWRITTEN(0x00409230, ChunkArchive::CountChildrenOf);

int ChunkArchive::CountChunks(int id) { return CountChildrenOf(Current(), id); }
EI_REWRITTEN(0x00409A60, ChunkArchive::CountChunks);

void ChunkArchive::ResizeToCurrent() {
    Ref* current = Current();
    dataStream_.Resize(static_cast<std::uint32_t>(current->size + current->offset));
}
EI_REWRITTEN(0x00409A40, ChunkArchive::ResizeToCurrent);

// ---- Chunks and fields ---------------------------------------------------------------------------

void ChunkArchive::Field(int id, void* data, unsigned size) {
    if (!loading_) {
        AppendChild(Current(), id, data, size);
        return;
    }
    Ref child;
    if (FindField(this, id, &child)) std::memcpy(data, Data() + child.offset, size);
    else std::memset(data, 0, size);   // a missing field reads as zero
}
EI_REWRITTEN(0x004092B0, ChunkArchive::Field);

bool ChunkArchive::EnterChunk(int id) {
    Ref* parent = Current();
    PushChunk();
    Ref* fresh = Current();
    // A failed search leaves the new chunk pushed (the callers do not pop it).
    if (loading_) return FindChild(parent, id, fresh, parent->occurrence);
    fresh->id = static_cast<std::uint8_t>(id);
    fresh->offset = parent->size + parent->offset + 5;
    return true;
}
EI_REWRITTEN(0x004098D0, ChunkArchive::EnterChunk);

void ChunkArchive::LeaveChunk() {
    if (loading_) {
        PopChunk();
        return;
    }
    // Writing: the chunk becomes a child of its parent (its content was written right after the
    // parent's, 5 bytes on to leave room for the header), then the stream is cut back to the parent.
    Stack::Node* node = stack_.End()->prev;
    const std::uint8_t* content = Data() + node->data.offset;   // taken before the parent's resize
    AppendChild(&node->prev->data, node->data.id, content, static_cast<std::uint32_t>(node->data.size));
    PopChunk();
    Ref* parent = Current();
    dataStream_.Resize(static_cast<std::uint32_t>(parent->size + parent->offset));
}
EI_REWRITTEN(0x00409980, ChunkArchive::LeaveChunk);

void ChunkArchive::CStringField(int id, mfc::CString* s) {
    if (!loading_) {
        const char* text = *s;
        AppendChild(Current(), id, text, static_cast<std::uint32_t>(s->GetLength() + 1));
        return;
    }
    Ref child;
    if (FindField(this, id, &child)) *s = reinterpret_cast<const char*>(Data() + child.offset);
    else *s = game::EmptyText();
}
EI_REWRITTEN(0x004093A0, ChunkArchive::CStringField);

// An STLport string field, written with its terminator; read through an inlined assign (the empty
// string when missing).
void ChunkArchive::StringField(int id, stlp::string* s) {
    if (!loading_) {
        AppendChild(Current(), id, s->data(), static_cast<std::uint32_t>(std::strlen(s->data()) + 1));
        return;
    }
    Ref child;
    // string::assign(text, text + strlen(text)), inlined by the original: overwrite in place and
    // cut the rest, or overwrite the whole string and append the remainder.
    if (FindField(this, id, &child)) s->Assign(reinterpret_cast<const char*>(Data() + child.offset));
    else s->Assign(game::EmptyText());
}
EI_REWRITTEN(0x00409470, ChunkArchive::StringField);

// A memory stream as chunk `id` (EnterChunk, inlined) holding its size (field 1) and its bytes
// (field 2, inlined Field); loading clears the stream and sizes it first. The stream is rewound;
// then the chunk is left (inlined LeaveChunk). A chunk missing while loading stays pushed, as with
// EnterChunk.
void ChunkArchive::StreamField(int id, CMemoryStream* s) {
    if (!EnterChunk(id)) return;
    std::uint32_t size = s->Size();
    Field(1, &size, 4);
    if (loading_) {
        s->Clear();
        s->SetSize(size);
    }
    Field(2, s->data_, size);
    s->Seek(0);
    LeaveChunk();
}
EI_REWRITTEN(0x00409640, ChunkArchive::StreamField);

// The number a save gives an object: its place in the saved list (1, 2, ...), kept as the map's
// value. The original wrote the object's address as its key (the loader maps any key to the object
// it creates); numbers by first save keep the saves free of heap layout (SV7b2a; the harness makes
// the original number them too, harness_save_leftovers.cpp).
std::uint32_t SavedNumber(const ChunkArchive* a, const CRefObject* object) {
    const ObjectMap::Node* node = a->objects_.Find(reinterpret_cast<std::uint32_t>(object));
    return node ? reinterpret_cast<std::uint32_t>(node->value) : 0;
}

// An object pointer field. An object not yet in the map joins the saved list (written by Close)
// and the map (value: its number while saving, the archive while loading, as the original). Writing
// then stores the number; reading looks the field up and ignores it.
void ChunkArchive::SaveObjectRef(int id, CRefObject* object) {
    std::uint32_t key = reinterpret_cast<std::uint32_t>(object);
    if (!objects_.Find(key)) {   // inlined find
        SavedList::Insert(saved_.End(), object);
        ObjectMap::Node* node = objects_.FindOrInsert({key, nullptr});
        node->value = loading_ ? reinterpret_cast<CRefObject*>(this)
                         : reinterpret_cast<CRefObject*>(static_cast<std::uintptr_t>(objects_.Size()));
    }
    if (!loading_) {
        std::uint32_t number = SavedNumber(this, object);
        AppendChild(Current(), id, &number, 4);
        return;
    }
    Ref child;
    FindField(this, id, &child);   // the original searches and drops the result
}
EI_REWRITTEN(0x0040A440, ChunkArchive::SaveObjectRef);

// An object reference field: the key read (0 when missing; writing stores 0) is looked up in the
// map (inlined find); returns the object, or null.
CRefObject* ChunkArchive::ObjectRef(int id) {
    std::uint32_t key = 0;
    if (!loading_) {
        AppendChild(Current(), id, &key, 4);
    } else {
        Ref child;
        if (FindField(this, id, &child)) std::memcpy(&key, Data() + child.offset, 4);
        else key = 0;
    }
    ObjectMap::Node* node = objects_.Find(key);
    return node ? node->value : nullptr;
}
EI_REWRITTEN(0x0040A5A0, ChunkArchive::ObjectRef);

// ---- An archive on the stack ----------------------------------------------------------------------

void ChunkArchive::ConstructLocal() {
    indexStream_.Construct();
    dataStream_.Construct();
    stack_.Construct();
    objects_.Construct(100);
    saved_.Construct();
    version_ = 1;
}

ChunkArchive* ChunkArchive::Construct() {
    ConstructLocal();
    return this;
}
EI_REWRITTEN(0x0053A7B0, ChunkArchive::Construct);

void ChunkArchive::DestructLocal() {
    saved_.Release();
    objects_.Destroy();
    stack_.Release<RefElement>();
    dataStream_.Destruct();
    indexStream_.Destruct();
}

void ChunkArchive::Destruct() { DestructLocal(); }
EI_REWRITTEN(0x004645A0, ChunkArchive::Destruct);

// ---- Whole archives over a CStream ---------------------------------------------------------------

bool __fastcall ReadStreamChunk(CStream* in, char* id, CMemoryStream* to) {
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
EI_REWRITTEN(0x00409EA0, ReadStreamChunk);

// The original inlines ReadStreamChunk here; Open inlines all of this for the index and the
// records.
bool __fastcall FindStreamChunk(CStream* in, int id, CMemoryStream* to, int occurrence) {
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
EI_REWRITTEN(0x00409F30, FindStreamChunk);

bool __fastcall WriteStreamChunk(CStream* out, int id, CMemoryStream* from) {
    char tag = static_cast<char>(id);
    out->Write(&tag, 1);
    std::uint32_t length = from->Size() * 2;
    if (length >= 0x100) {
        length |= 1;
        out->Write(&length, 4);
    } else {
        out->Write(&length, 1);
    }
    std::uint8_t* data = from->data_;
    out->Write(data, from->Size());
    return true;
}
EI_REPLACE(0x0040A3C0, WriteStreamChunk);

void ChunkArchive::Open(CStream* stream, int loading, int version) {
    stack_.ClearWith([](Ref*) {});
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
        index->Read(&classId, 4);
        index->Read(&key, 4);
        Factory factory = Registry().factories[classId];
        CRefObject* object = factory ? factory() : nullptr;
        SavedList::Insert(saved_.End(), object);
        objects_.FindOrInsert({key, nullptr})->value = object;
    }
    if (version == 1) Field(2, &version_, 4);
    else version_ = version;

    int count = CountChildrenOf(Current(), 1);
    for (int i = 1; i <= count; ++i) {
        std::uint32_t key = 0;
        Current()->occurrence = i;
        Ref* record = PushRecordChunk(this);
        if (loading_) {
            Ref field;
            field.Reset();
            key = FindChild(record, 0, &field, record->occurrence) ? *reinterpret_cast<std::uint32_t*>(Data() + field.offset) : 0;
        } else {
            AppendChild(record, 0, &key, 4);
        }
        CRefObject* object = objects_.FindOrInsert({key, nullptr})->value;
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
EI_REWRITTEN(0x00409AE0, ChunkArchive::Open);

void ChunkArchive::Close(CStream* stream) {
    CMemoryStream* index = &indexStream_;
    CMemoryStream* data = &dataStream_;
    if (loading_) {
        // After a load: each loaded object gets a reference and loses it again (one without other
        // owners is deleted).
        for (ObjectMap::Node* node = objects_.First(); node; node = objects_.After(node)) {
            CRefObject* counted;
            reinterpret_cast<RefPtr*>(&counted)->FromRawCounted(node->value);
            if (counted) counted->Release();
        }
    } else {
        ResizeToCurrent();   // inlined
        WriteStreamChunk(stream, 1, data);
        data->Clear();
        Current()->Reset();
        while (!saved_.Empty()) {
            CRefObject* object = saved_.First()->data;
            SavedList::Erase(saved_.First());
            std::uint32_t vtable;
            std::memcpy(&vtable, object, 4);
            const ClassRegistry::ClassIdMap::Node* found = Registry().classIds.Find(vtable);
            std::int32_t classId = found ? found->value : -1;
            std::uint32_t number = SavedNumber(this, object);   // the original wrote the address
            index->Write(&classId, 4);
            index->Write(&number, 4);
            Ref* record = PushRecordChunk(this);
            if (loading_) {
                Ref field;
                field.Reset();
                object = FindChild(record, 0, &field, record->occurrence) ? *reinterpret_cast<CRefObject**>(Data() + field.offset) : nullptr;
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
                Stack::Node* node = stack_.End()->prev;
                const std::uint8_t* content = Data() + node->data.offset;
                AppendChild(&node->prev->data, node->data.id, content, static_cast<std::uint32_t>(node->data.size));
                PopChunk();
                ResizeToCurrent();
            }
        }
        Field(2, &version_, 4);
        WriteStreamChunk(stream, 0, index);
        ResizeToCurrent();   // inlined
        WriteStreamChunk(stream, 2, data);
    }
    index->Clear();
    data->Clear();
    objects_.Clear();
    saved_.Clear();
    stack_.Clear<RefElement>();
}
EI_REWRITTEN(0x0040A000, ChunkArchive::Close);
