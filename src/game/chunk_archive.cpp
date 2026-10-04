// The tagged-chunk archive (model.h, ChunkArchive): the chunk primitives, entering and leaving
// chunks, fields. 00409070-0040A000. See docs/notes/unmapped_410_470.md ("Fourth round").
#include "model.h"
#include "object.h"

#include "ei/hook.h"

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
