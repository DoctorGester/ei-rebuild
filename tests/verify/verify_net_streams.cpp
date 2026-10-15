// Checks for the whole-stream functions of net_delta.cpp (the message compression 0047D7F0 /
// 0047D970, the length-prefixed copies 0047DA80 / 0047DAE0 and their DeltaField forms 0047E190 /
// 0047E240) and the fixed buffer view's slots in net_coder.cpp (0047D910, 0047D930, 0047D940,
// 0047D960).
//
// Reached by: the compression every frame of every game (the client's and the server's messages
// to each other go through it before the loopback links; sandbox_actions.txt counts ~800 coder
// calls); ReadStream by the client's world message on every new game or load (00669010); WriteStream
// by the server's state sends (CPlayerServer::SendState02, 0066E430 / 0066E7D0); the DeltaField
// forms only by CEffectShowVisibility's replication (the vision fog spell; spells_cast.txt casts
// one, which ends at once without a caster). The view's slots: Seek only if a cursor seeks a view;
// the destructor body only while unwinding; the deleting destructors never (the views live on the
// stack).
//
// Memory streams are deep-copied (deltasnap::CloneStream: header plus a private buffer from the
// game heap) for the original's run and compared field by field (the buffer pointer excepted) and
// up to the size; anything else runs ours only.
//  - Compression: the original on a copy of the coder (0x2828 bytes) writing into a copy of the
//    output stream, ours on the real ones; the outputs compared, and the coders but for the output
//    cursor's three pointers into the (freed) result buffer, which must sit at the same offsets
//    from the cursor's start. The source stream is only read.
//  - WriteStream / ReadStream: the original on copies of both streams; the result and both
//    streams compared. WriteStreamBoth: the original on a copy of the field (both outputs
//    deep-copied) and of `from`. DeltaField::ReadStream: the reader global pointed at a copy of
//    the reader for the original's run.
//  - The view: Seek and the destructor body side by side on a copy of the 0x1C bytes; the base's
//    deleting destructor the same when it does not free; the view's deleting destructor (a CALL
//    at +3) ours only.
#include "verify.h"

#include "delta_snapshot.h"

#include "../../src/game/net_coder.h"
#include "../../src/game/net_delta.h"

#include <cstddef>
#include <cstring>

namespace {

using deltasnap::CloneStream;
using deltasnap::FreeStream;
using deltasnap::IsMemoryStream;
using deltasnap::SameStream;

void* s_unused;

// --- Compression ----------------------------------------------------------------------------------

// Pointers into the two runs' result buffers: equal, or at the same offset from their bases.
bool SamePointer(const std::uint8_t* a, const std::uint8_t* baseA, const std::uint8_t* b, const std::uint8_t* baseB) {
    return a == b || a - baseA == b - baseB;
}

bool SameCoder(const CoderState& a, const CoderState& b) {
    if (std::memcmp(&a, &b, offsetof(CoderState, out)) != 0) return false;
    if (std::memcmp(&a.pos, &b.pos, sizeof(CoderState) - offsetof(CoderState, pos)) != 0) return false;
    const BufferCursor& x = a.out;
    const BufferCursor& y = b.out;
    return SamePointer(x.p, x.start, y.p, y.start) && SamePointer(x.bytePtr, x.start, y.bytePtr, y.start) && x.acc == y.acc &&
           x.bits == y.bits && std::memcmp(x.unused_0D, y.unused_0D, 3) == 0 && x.sink == y.sink;
}

using CodeFn = void(__fastcall*)(BitStream*, void*, CoderState*, BitStream*);
alignas(8) unsigned char g_coder[sizeof(CoderState)];
bool g_codeBusy;

void RunCode(const char* name, CodeFn original, void (BitStream::*ours)(CoderState*, BitStream*), BitStream* self,
             CoderState* coder, BitStream* out) {
    if (g_codeBusy || !IsMemoryStream(self) || !IsMemoryStream(out) || self == out) {
        (self->*ours)(coder, out);
        return;
    }
    g_codeBusy = true;
    auto* expected = reinterpret_cast<CoderState*>(g_coder);
    std::memcpy(g_coder, coder, sizeof(CoderState));
    BitStream target;
    CloneStream(&target, out);
    std::uint32_t size = self->size_;
    original(self, nullptr, expected, &target);
    (self->*ours)(coder, out);
    bool sameOut = SameStream(&target, out);
    bool sameCoder = SameCoder(*expected, *coder);
    ei::verify::Check(name, true, sameOut && sameCoder, "out %d coder %d, %u bytes in, %u / %u out", sameOut, sameCoder, size,
                      target.size_, out->size_);
    FreeStream(&target);
    g_codeBusy = false;
}

// 0047D7F0 / 0047D970: PUSH -1 (2), PUSH imm32 (5).
CodeFn s_compress;
void __fastcall CompressToVerify(BitStream* self, void*, CoderState* coder, BitStream* out) {
    RunCode("BitStream::CompressTo", s_compress, &BitStream::CompressTo, self, coder, out);
}
CodeFn s_decompress;
void __fastcall DecompressToVerify(BitStream* self, void*, CoderState* coder, BitStream* out) {
    RunCode("BitStream::DecompressTo", s_decompress, &BitStream::DecompressTo, self, coder, out);
}

// --- Length-prefixed copies -----------------------------------------------------------------------

// 0047DA80: PUSH EBX, PUSH ESI (2), MOV ESI,[ESP+0xC] (4).
void(__fastcall* s_writeStream)(BitStream*, void*, BitStream*);
void __fastcall WriteStreamVerify(BitStream* self, void*, BitStream* from) {
    if (!IsMemoryStream(self) || !IsMemoryStream(from) || self == from) {
        self->WriteStream(from);
        return;
    }
    BitStream to, source;
    CloneStream(&to, self);
    CloneStream(&source, from);
    s_writeStream(&to, nullptr, &source);
    self->WriteStream(from);
    bool same = SameStream(&to, self) && SameStream(&source, from);
    ei::verify::Check("BitStream::WriteStream", true, same, "%u bytes", from->size_);
    FreeStream(&to);
    FreeStream(&source);
}

// 0047DAE0: PUSH ECX, PUSH ESI (2), MOV ESI,ECX (2), PUSH EDI (1).
int(__fastcall* s_readStream)(BitStream*, void*, BitStream*);
int __fastcall ReadStreamVerify(BitStream* self, void*, BitStream* to) {
    if (!IsMemoryStream(self) || !IsMemoryStream(to) || self == to) return self->ReadStream(to);
    BitStream from, target;
    CloneStream(&from, self);
    CloneStream(&target, to);
    int expected = s_readStream(&from, nullptr, &target);
    int actual = self->ReadStream(to);
    bool same = expected == actual && SameStream(&from, self) && SameStream(&target, to);
    ei::verify::Check("BitStream::ReadStream", true, same, "%d / %d, %u bytes", expected, actual, to->size_);
    FreeStream(&from);
    FreeStream(&target);
    return actual;
}

// 0047E190: PUSH EBX, PUSH ESI (2), MOV ESI,[ESP+0xC] (4).
void(__fastcall* s_writeStreamBoth)(DeltaField*, void*, BitStream*);
void __fastcall WriteStreamBothVerify(DeltaField* self, void*, BitStream* from) {
    bool aliased = from == &self->out[0] || from == &self->out[1];
    if (aliased || !IsMemoryStream(&self->out[0]) || !IsMemoryStream(&self->out[1]) || !IsMemoryStream(from)) {
        self->WriteStreamBoth(from);
        return;
    }
    alignas(8) unsigned char bytes[sizeof(DeltaField)];
    std::memcpy(bytes, self, sizeof(bytes));
    auto* copy = reinterpret_cast<DeltaField*>(bytes);
    CloneStream(&copy->out[0], &self->out[0]);
    CloneStream(&copy->out[1], &self->out[1]);
    BitStream source;
    CloneStream(&source, from);
    s_writeStreamBoth(copy, nullptr, &source);
    self->WriteStreamBoth(from);
    bool same = SameStream(&copy->out[0], &self->out[0]) && SameStream(&copy->out[1], &self->out[1]) && SameStream(&source, from);
    ei::verify::Check("DeltaField::WriteStreamBoth", true, same, "%u bytes", from->size_);
    FreeStream(&copy->out[0]);
    FreeStream(&copy->out[1]);
    FreeStream(&source);
}

// 0047E240: PUSH ECX, PUSH ESI (2), MOV ESI,[007A9798] (6).
int(__fastcall* s_fieldReadStream)(DeltaField*, void*, BitStream*);
int __fastcall FieldReadStreamVerify(DeltaField* self, void*, BitStream* to) {
    BitStream* reader = delta::Reader();
    if (!reader || reader == to || !IsMemoryStream(reader) || !IsMemoryStream(to)) return self->ReadStream(to);
    BitStream from, target;
    CloneStream(&from, reader);
    CloneStream(&target, to);
    delta::Reader() = &from;
    int expected = s_fieldReadStream(self, nullptr, &target);
    delta::Reader() = reader;
    int actual = self->ReadStream(to);
    bool same = expected == actual && SameStream(&from, reader) && SameStream(&target, to);
    ei::verify::Check("DeltaField::ReadStream", true, same, "%d / %d, %u bytes", expected, actual, to->size_);
    FreeStream(&from);
    FreeStream(&target);
    return actual;
}

// --- The fixed view -------------------------------------------------------------------------------

// 0047D930: MOV EAX,[ECX+4] (3), MOV EDX,[ESP+4] (4).
std::uint8_t*(__fastcall* s_viewSeek)(GameBuffer*, void*, std::int32_t);
std::uint8_t* __fastcall BufferViewSeekVerify(GameBuffer* self, void*, std::int32_t position) {
    GameBuffer copy = *self;
    std::uint8_t* expected = s_viewSeek(&copy, nullptr, position);
    std::uint8_t* actual = self->SeekView(position);
    ei::verify::Check("BufferViewSeek", true, expected == actual && std::memcmp(&copy, self, sizeof(GameBuffer)) == 0, "position %d",
                      position);
    return actual;
}

// 0047D960: MOV DWORD PTR [ECX],0073CA48 (6).
void(__fastcall* s_viewDestruct)(GameBuffer*);
void __fastcall BufferViewDestructVerify(GameBuffer* self) {
    GameBuffer copy = *self;
    s_viewDestruct(&copy);
    BufferViewDestruct(self);
    ei::verify::Check("BufferViewDestruct", true, std::memcmp(&copy, self, sizeof(GameBuffer)) == 0, "");
}

// 0047D940: a CALL at +3: ours only.
void* __fastcall BufferViewDeletingDestructorVerify(GameBuffer* self, void*, unsigned flags) {
    void* result = self->DeleteView(flags);
    ei::verify::Check("BufferViewDeletingDestructor", static_cast<void*>(self), result, "");
    return result;
}

// 0047D910: MOV AL,[ESP+4] (4), PUSH ESI (1). When it frees, ours only.
void*(__fastcall* s_baseDelete)(GameBuffer*, void*, unsigned);
void* __fastcall BufferViewBaseDeletingDestructorVerify(GameBuffer* self, void*, unsigned flags) {
    if (flags & 1) {
        void* result = self->DeleteViewBase(flags);
        ei::verify::Check("BufferViewBaseDeletingDestructor", static_cast<void*>(self), result, "");
        return result;
    }
    GameBuffer copy = *self;
    void* expected = s_baseDelete(&copy, nullptr, flags);
    void* actual = self->DeleteViewBase(flags);
    bool same = expected == static_cast<void*>(&copy) && actual == static_cast<void*>(self) &&
                std::memcmp(&copy, self, sizeof(GameBuffer)) == 0;
    ei::verify::Check("BufferViewBaseDeletingDestructor", true, same, "");
    return actual;
}

} // namespace

EI_VERIFY(0x0047D7F0, CompressToVerify, 7, s_compress);
EI_VERIFY(0x0047D970, DecompressToVerify, 7, s_decompress);
EI_VERIFY(0x0047DA80, WriteStreamVerify, 6, s_writeStream);
EI_VERIFY(0x0047DAE0, ReadStreamVerify, 5, s_readStream);
EI_VERIFY(0x0047E190, WriteStreamBothVerify, 6, s_writeStreamBoth);
EI_VERIFY(0x0047E240, FieldReadStreamVerify, 8, s_fieldReadStream);
EI_VERIFY(0x0047D930, BufferViewSeekVerify, 7, s_viewSeek);
EI_VERIFY(0x0047D960, BufferViewDestructVerify, 6, s_viewDestruct);
EI_VERIFY(0x0047D940, BufferViewDeletingDestructorVerify, 0, s_unused);
EI_VERIFY(0x0047D910, BufferViewBaseDeletingDestructorVerify, 5, s_baseDelete);
