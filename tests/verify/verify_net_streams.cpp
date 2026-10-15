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
// Ours runs first on the real streams. Memory streams are kept in our memory before
// (deltasnap::KeptStream: header plus bytes) and the original runs afterwards on twins made from
// them (each buffer our memory, or a game-heap block, freed, when ours replaced the real stream's
// block), so ours' blocks are where the original's would be without the check (SV2f / SV2h). The streams are compared field by field (the buffer pointer
// excepted) and up to the size; anything else runs ours only.
//  - Compression: ours on the real coder and output stream; the original on a copy of the coder
//    (0x2828 bytes, taken before ours) writing into a twin of the output stream; the outputs
//    compared, and the coders but for the output cursor's three pointers into the (freed) result
//    buffer, which must sit at the same offsets from the cursor's start. The source stream is only
//    read.
//  - WriteStream / ReadStream: the original on twins of both streams; the result and both streams
//    compared. WriteStreamBoth: the original on a twin of the field's outputs and of `from`.
//    DeltaField::ReadStream: the reader global pointed at a twin of the reader for the original's
//    run.
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

using deltasnap::IsMemoryStream;
using deltasnap::KeptStream;
using deltasnap::SameStream;
using deltasnap::TwinStream;

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
    const KeptStream keptOut(out);
    std::uint32_t size = self->size_;
    (self->*ours)(coder, out);
    TwinStream target;
    original(self, nullptr, expected, target.Make(keptOut, out));
    bool sameOut = SameStream(&target.stream, out);
    bool sameCoder = SameCoder(*expected, *coder);
    ei::verify::Check(name, true, sameOut && sameCoder, "out %d coder %d, %u bytes in, %u / %u out", sameOut, sameCoder, size,
                      target.stream.size_, out->size_);
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
    const KeptStream keptTo(self);
    const KeptStream keptFrom(from);
    self->WriteStream(from);
    TwinStream to, source;
    to.Make(keptTo, self);
    source.Make(keptFrom, from);
    s_writeStream(&to.stream, nullptr, &source.stream);
    bool same = SameStream(&to.stream, self) && SameStream(&source.stream, from);
    ei::verify::Check("BitStream::WriteStream", true, same, "%u bytes", from->size_);
}

// 0047DAE0: PUSH ECX, PUSH ESI (2), MOV ESI,ECX (2), PUSH EDI (1).
int(__fastcall* s_readStream)(BitStream*, void*, BitStream*);
int __fastcall ReadStreamVerify(BitStream* self, void*, BitStream* to) {
    if (!IsMemoryStream(self) || !IsMemoryStream(to) || self == to) return self->ReadStream(to);
    const KeptStream keptFrom(self);
    const KeptStream keptTo(to);
    int actual = self->ReadStream(to);
    TwinStream from, target;
    from.Make(keptFrom, self);
    target.Make(keptTo, to);
    int expected = s_readStream(&from.stream, nullptr, &target.stream);
    bool same = expected == actual && SameStream(&from.stream, self) && SameStream(&target.stream, to);
    ei::verify::Check("BitStream::ReadStream", true, same, "%d / %d, %u bytes", expected, actual, to->size_);
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
    // The twin field: the snapshots' headers as they are (not touched), the outputs twins.
    alignas(8) unsigned char bytes[sizeof(DeltaField)];
    std::memcpy(bytes, self, sizeof(bytes));
    const KeptStream keptOut0(&self->out[0]);
    const KeptStream keptOut1(&self->out[1]);
    const KeptStream keptFrom(from);
    self->WriteStreamBoth(from);
    auto* twin = reinterpret_cast<DeltaField*>(bytes);
    TwinStream out0, out1, source;
    std::memcpy(&twin->out[0], out0.Make(keptOut0, &self->out[0]), sizeof(BitStream));
    std::memcpy(&twin->out[1], out1.Make(keptOut1, &self->out[1]), sizeof(BitStream));
    s_writeStreamBoth(twin, nullptr, source.Make(keptFrom, from));
    // The original may have grown the outputs: the twins free the buffers the twin field holds now.
    std::memcpy(&out0.stream, &twin->out[0], sizeof(BitStream));
    std::memcpy(&out1.stream, &twin->out[1], sizeof(BitStream));
    bool same = SameStream(&twin->out[0], &self->out[0]) && SameStream(&twin->out[1], &self->out[1]) &&
                SameStream(&source.stream, from);
    ei::verify::Check("DeltaField::WriteStreamBoth", true, same, "%u bytes", from->size_);
}

// 0047E240: PUSH ECX, PUSH ESI (2), MOV ESI,[007A9798] (6).
int(__fastcall* s_fieldReadStream)(DeltaField*, void*, BitStream*);
int __fastcall FieldReadStreamVerify(DeltaField* self, void*, BitStream* to) {
    BitStream* reader = delta::Reader();
    if (!reader || reader == to || !IsMemoryStream(reader) || !IsMemoryStream(to)) return self->ReadStream(to);
    const KeptStream keptFrom(reader);
    const KeptStream keptTo(to);
    int actual = self->ReadStream(to);
    TwinStream from, target;
    delta::Reader() = from.Make(keptFrom, reader);
    int expected = s_fieldReadStream(self, nullptr, target.Make(keptTo, to));
    delta::Reader() = reader;
    bool same = expected == actual && SameStream(&from.stream, reader) && SameStream(&target.stream, to);
    ei::verify::Check("DeltaField::ReadStream", true, same, "%d / %d, %u bytes", expected, actual, to->size_);
    return actual;
}

// --- The fixed view -------------------------------------------------------------------------------

// 0047D930: MOV EAX,[ECX+4] (3), MOV EDX,[ESP+4] (4).
std::uint8_t*(__fastcall* s_viewSeek)(GameBuffer*, void*, std::int32_t);
std::uint8_t* __fastcall BufferViewSeekVerify(GameBuffer* self, void*, std::int32_t position) {
    GameBuffer copy = *self;
    std::uint8_t* expected = s_viewSeek(&copy, nullptr, position);
    std::uint8_t* actual = BufferViewSeek(self, nullptr, position);
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
    void* result = BufferViewDeletingDestructor(self, nullptr, flags);
    ei::verify::Check("BufferViewDeletingDestructor", static_cast<void*>(self), result, "");
    return result;
}

// 0047D910: MOV AL,[ESP+4] (4), PUSH ESI (1). When it frees, ours only.
void*(__fastcall* s_baseDelete)(GameBuffer*, void*, unsigned);
void* __fastcall BufferViewBaseDeletingDestructorVerify(GameBuffer* self, void*, unsigned flags) {
    if (flags & 1) {
        void* result = BufferViewBaseDeletingDestructor(self, nullptr, flags);
        ei::verify::Check("BufferViewBaseDeletingDestructor", static_cast<void*>(self), result, "");
        return result;
    }
    GameBuffer copy = *self;
    void* expected = s_baseDelete(&copy, nullptr, flags);
    void* actual = BufferViewBaseDeletingDestructor(self, nullptr, flags);
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
