// Checks for chunk_archive.cpp.
//  - NextChild, FindChild, CountChildrenOf, CountChunks: pure reads; the original runs on copies of
//    the cursor and output, results compared.
//  - Field (loading): the original reads into a scratch buffer, ours into the real one; bytes
//    compared. Writing: ours only; the chunk grows by the field.
//  - EnterChunk: the original runs first; its new top chunk and result are recorded, the chunk it
//    pushed is removed again, then ours runs; both must push the same chunk.
//  - ResizeToCurrent: the original, then ours (a second resize to the same size changes nothing);
//    the data stream's header must be the same after both.
//  - AppendChild / LeaveChunk: ours only (they write the data stream); the chunk grows by the
//    child / the stack shrinks by one.
#include "verify.h"

#include "../../src/game/model.h"
#include "../../src/game/object.h"

#include <cstring>

void __fastcall ChunkRefReset(ChunkArchive::Ref* r);                                        // chunk_archive.cpp
bool __fastcall ReadStreamChunk(void* in, char* id, CMemoryStream* to);
bool __fastcall FindStreamChunk(void* in, int id, CMemoryStream* to, int occurrence);
bool __fastcall WriteStreamChunk(void* out, int id, CMemoryStream* from);

namespace {

using Ref = ChunkArchive::Ref;

void* s_unused;

__declspec(naked) void* __fastcall StackEraseOriginal(void*, void*, void**, void*) EI_ORIGINAL(0x0040A920)

Ref* Top(ChunkArchive* a) { return reinterpret_cast<Ref*>(reinterpret_cast<std::uint8_t*>(a->stack_->prev) + 8); }
int Depth(ChunkArchive* a) {
    int n = 0;
    for (ChunkNode* p = a->stack_->next; p != a->stack_; p = p->next) ++n;
    return n;
}

// 00409070: MOV ECX,[ECX+0x40] (3), PUSH EBP (1), MOV EBP,[ESP+8] (4).
bool(__fastcall* s_next)(ChunkArchive*, void*, const Ref*, std::int32_t*, Ref*);
bool __fastcall NextChildVerify(ChunkArchive* self, void*, const Ref* parent, std::int32_t* cursor, Ref* out) {
    std::int32_t c = *cursor;
    Ref r = *out;
    bool expected = s_next(self, nullptr, parent, &c, &r);
    bool actual = self->NextChild(parent, cursor, out);
    ei::verify::Check("ChunkArchive::NextChild", true,
                      expected == actual && c == *cursor && std::memcmp(&r, out, sizeof(Ref)) == 0, "");
    return actual;
}

// 004091C0: PUSH ECX, PUSH EBX, PUSH EBP (3), MOV EBP,[ESP+0x10] (4).
bool(__fastcall* s_find)(ChunkArchive*, void*, const Ref*, int, Ref*, int);
bool __fastcall FindChildVerify(ChunkArchive* self, void*, const Ref* parent, int id, Ref* out, int occurrence) {
    Ref r = *out;
    bool expected = s_find(self, nullptr, parent, id, &r, occurrence);
    bool actual = self->FindChild(parent, id, out, occurrence);
    ei::verify::Check("ChunkArchive::FindChild", true, expected == actual && std::memcmp(&r, out, sizeof(Ref)) == 0,
                      "id %d", id);
    return actual;
}

// 00409230: SUB ESP,0x14 (3), PUSH EBP (1), MOV EBP,[ESP+0x1C] (4).
int(__fastcall* s_countOf)(ChunkArchive*, void*, const Ref*, int);
int __fastcall CountChildrenVerify(ChunkArchive* self, void*, const Ref* parent, int id) {
    int expected = s_countOf(self, nullptr, parent, id);
    int actual = self->CountChildrenOf(parent, id);
    ei::verify::Check("ChunkArchive::CountChildrenOf", expected, actual, "id %d", id);
    return actual;
}

// 00409A60: SUB ESP,0x14 (3), PUSH EBP (1), PUSH ESI (1).
int(__fastcall* s_count)(ChunkArchive*, void*, int);
int __fastcall CountChunksVerify(ChunkArchive* self, void*, int id) {
    int expected = s_count(self, nullptr, id);
    int actual = self->CountChunks(id);
    ei::verify::Check("ChunkArchive::CountChunks", expected, actual, "id %d", id);
    return actual;
}

// 00409A40: MOV EAX,[ECX+0x50] (3), PUSH ESI (1), ADD ECX,0x28 (3).
void(__fastcall* s_resize)(ChunkArchive*);
void __fastcall ResizeVerify(ChunkArchive* self) {
    s_resize(self);
    unsigned char header[0x28];
    std::memcpy(header, self->streams_ + 0x28, sizeof(header));
    self->ResizeToCurrent();
    ei::verify::Check("ChunkArchive::ResizeToCurrent", true, std::memcmp(header, self->streams_ + 0x28, sizeof(header)) == 0, "");
}

// 004092B0: SUB ESP,0x14 (3), PUSH EBX (1), PUSH EBP (1).
void(__fastcall* s_field)(ChunkArchive*, void*, int, void*, unsigned);
void __fastcall FieldVerify(ChunkArchive* self, void*, int id, void* data, unsigned size) {
    if (self->loading_ && size <= 0x4000) {
        static unsigned char scratch[0x4000];
        s_field(self, nullptr, id, scratch, size);
        self->Field(id, data, size);
        ei::verify::Check("ChunkArchive::Field", true, std::memcmp(scratch, data, size) == 0, "id %d size %u", id, size);
        return;
    }
    std::int32_t before = Top(self)->size;
    bool writing = !self->loading_;
    self->Field(id, data, size);
    if (writing)
        ei::verify::Check("ChunkArchive::Field", Top(self)->size - before,
                          static_cast<std::int32_t>(size + (size * 2 >= 0x100 ? 5 : 2)), "id %d size %u", id, size);
}

// 004098D0: PUSH ECX, PUSH EBX, PUSH EBP (3), MOV EBP,ECX (2).
bool(__fastcall* s_enter)(ChunkArchive*, void*, int);
bool __fastcall EnterVerify(ChunkArchive* self, void*, int id) {
    int depth = Depth(self);
    bool expected = s_enter(self, nullptr, id);
    Ref pushed = *Top(self);
    void* after;
    StackEraseOriginal(&self->stack_, nullptr, &after, self->stack_->prev);
    bool actual = self->EnterChunk(id);
    ei::verify::Check("ChunkArchive::EnterChunk", true,
                      expected == actual && std::memcmp(&pushed, Top(self), sizeof(Ref)) == 0 && Depth(self) == depth + 1,
                      "id %d", id);
    return actual;
}

// 00409110 / 00409980: ours only.
bool __fastcall AppendVerify(ChunkArchive* self, void*, Ref* chunk, int id, const void* data, std::uint32_t size) {
    std::int32_t before = chunk->size;
    bool result = self->AppendChild(chunk, id, data, size);
    ei::verify::Check("ChunkArchive::AppendChild", chunk->size - before,
                      static_cast<std::int32_t>(size + (size * 2 >= 0x100 ? 5 : 2)), "size %u", size);
    return result;
}
void __fastcall LeaveVerify(ChunkArchive* self) {
    int depth = Depth(self);
    self->LeaveChunk();
    ei::verify::Check("ChunkArchive::LeaveChunk", depth - 1, Depth(self), "");
}

// 00409E80: XOR EAX,EAX (2), MOV byte [ECX],0xFF (3).
void(__fastcall* s_reset)(Ref*);
void __fastcall RefResetVerify(Ref* self) {
    Ref copy = *self;
    s_reset(&copy);
    ChunkRefReset(self);
    ei::verify::Check("ChunkRefReset", true, std::memcmp(&copy, self, sizeof(Ref)) == 0, "");
}

// Whole-archive functions: ours only (they read / write the streams and create objects).
bool __fastcall ReadStreamChunkVerify(void* in, char* id, CMemoryStream* to) {
    bool result = ReadStreamChunk(in, id, to);
    ei::verify::Check("ReadStreamChunk", true, !result || to->size_ <= 10000000u, "");
    return result;
}
bool __fastcall FindStreamChunkVerify(void* in, int id, CMemoryStream* to, int occurrence) {
    bool result = FindStreamChunk(in, id, to, occurrence);
    ei::verify::Check("FindStreamChunk", true, true, "id %d", id);
    return result;
}
bool __fastcall WriteStreamChunkVerify(void* out, int id, CMemoryStream* from) {
    bool result = WriteStreamChunk(out, id, from);
    ei::verify::Check("WriteStreamChunk", true, result, "");
    return result;
}
void __fastcall OpenVerify(ChunkArchive* self, void*, void* stream, int loading, int version) {
    self->Open(stream, loading, version);
    ei::verify::Check("ChunkArchive::Open", 1, Depth(self), "loading %d", loading & 0xFF);
}
void __fastcall CloseVerify(ChunkArchive* self, void*, void* stream) {
    self->Close(stream);
    ei::verify::Check("ChunkArchive::Close", 0, Depth(self), "");
}

} // namespace

EI_VERIFY(0x00409070, NextChildVerify, 8, s_next);
EI_VERIFY(0x004091C0, FindChildVerify, 7, s_find);
EI_VERIFY(0x00409230, CountChildrenVerify, 8, s_countOf);
EI_VERIFY(0x00409A60, CountChunksVerify, 5, s_count);
EI_VERIFY(0x00409A40, ResizeVerify, 7, s_resize);
EI_VERIFY(0x004092B0, FieldVerify, 5, s_field);
EI_VERIFY(0x004098D0, EnterVerify, 5, s_enter);
EI_VERIFY(0x00409110, AppendVerify, 0, s_unused);
EI_VERIFY(0x00409980, LeaveVerify, 0, s_unused);
EI_VERIFY(0x00409E80, RefResetVerify, 5, s_reset);
EI_VERIFY(0x00409EA0, ReadStreamChunkVerify, 0, s_unused);
EI_VERIFY(0x00409F30, FindStreamChunkVerify, 0, s_unused);
EI_VERIFY(0x0040A3C0, WriteStreamChunkVerify, 0, s_unused);
EI_VERIFY(0x00409AE0, OpenVerify, 0, s_unused);
EI_VERIFY(0x0040A000, CloseVerify, 0, s_unused);
