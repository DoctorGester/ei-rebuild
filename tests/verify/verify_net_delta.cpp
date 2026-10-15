// Differential checks for net_delta.cpp.
//
// Everything here changes memory streams (headers, buffers that may be reallocated) and a few
// globals. Ours runs first on the real objects: each wrapper keeps the streams the function can
// touch in our memory (deltasnap::KeptStream / KeptField / KeptState: header plus bytes), runs ours,
// then makes twins from what it kept (each buffer our memory, or a game-heap block when ours replaced
// the real stream's block: delta_snapshot.h), points the globals at the twins, runs the original on
// them and puts ours' globals back. The streams must then match: every header field except the
// buffer pointer, and the buffer contents up to the stream's size. The globals must match too
// (pointers into the twin field compared by their offset). Ours' blocks are then where the
// original's would be without the check (SV2f / SV2h).
#include "verify.h"
#include "delta_snapshot.h"

#include "../../src/game/ai_motion.h"
#include "../../src/game/net_delta.h"
#include "../../src/lib/crt.h"

#include <cstring>
#include <vector>

namespace {

using deltasnap::IsMemoryStream;
using deltasnap::KeptField;
using deltasnap::KeptState;
using deltasnap::KeptStream;
using deltasnap::SameStream;
using deltasnap::TwinField;
using deltasnap::TwinState;
using deltasnap::TwinStream;

void* s_unused;

// Only memory streams (the two CMemoryStream vtables) are copied: another stream class (e.g. one over
// a file) would share its underlying resource with the copy. Anything else runs ours only.
bool IsMemoryField(const DeltaField* f) {
    return IsMemoryStream(&f->snapshot[0]) && IsMemoryStream(&f->snapshot[1]) && IsMemoryStream(&f->out[0]) &&
           IsMemoryStream(&f->out[1]);
}

// `size` bytes of `p` kept in our memory (at least one byte, for a buffer the original writes into).
std::vector<std::uint8_t> KeepBytes(const void* p, std::size_t size) {
    std::vector<std::uint8_t> bytes(size ? size : 1);
    if (p && size) std::memcpy(bytes.data(), p, size);
    return bytes;
}

// --- Globals -----------------------------------------------------------------------------------------

struct Globals {
    int keep0, keep1;
    BitStream* snapshot;
    BitStream* reader;
    static Globals Save() { return {delta::KeepSnapshot0(), delta::KeepSnapshot1(), delta::ReadSnapshot(), delta::Reader()}; }
    void Restore() const {
        delta::KeepSnapshot0() = keep0;
        delta::KeepSnapshot1() = keep1;
        delta::ReadSnapshot() = snapshot;
        delta::Reader() = reader;
    }
};

bool Inside(const void* p, const void* base, std::size_t size) {
    auto a = reinterpret_cast<std::uintptr_t>(p);
    auto b = reinterpret_cast<std::uintptr_t>(base);
    return a >= b && a < b + size;
}

// A pointer into `from` (a field) mapped into `to`; other pointers unchanged.
template <typename T>
T* Map(T* p, const void* from, const void* to, std::size_t size) {
    auto a = reinterpret_cast<std::uintptr_t>(p);
    auto f = reinterpret_cast<std::uintptr_t>(from);
    if (a >= f && a < f + size) return reinterpret_cast<T*>(reinterpret_cast<std::uintptr_t>(to) + (a - f));
    return p;
}

// Globals after the original (on twins) and after ours (on the real objects) agree, with pointers
// into the twin field / reader mapped back.
bool SameGlobals(const Globals& expected, const Globals& actual, const DeltaField* twin, const DeltaField* real,
                 const BitStream* readerTwin, const BitStream* realReader) {
    BitStream* snapshot = Map(expected.snapshot, twin, real, sizeof(DeltaField));
    BitStream* reader = readerTwin && expected.reader == readerTwin ? const_cast<BitStream*>(realReader) : expected.reader;
    return expected.keep0 == actual.keep0 && expected.keep1 == actual.keep1 && snapshot == actual.snapshot &&
           reader == actual.reader;
}

// --- BitStream ---------------------------------------------------------------------------------------

// 0047DFC0: MOV DL,[ESP+4] (4), PUSH EBX (1).
void(__fastcall* s_putBit)(BitStream*, void*, int);
void __fastcall PutBitVerify(BitStream* self, void*, int bit) {
    if (!IsMemoryStream(self)) return self->PutBit((bit & 0xFF) != 0);
    const KeptStream kept(self);
    self->PutBit((bit & 0xFF) != 0);
    TwinStream expected;
    s_putBit(expected.Make(kept, self), nullptr, bit);
    ei::verify::Check("BitStream::PutBit", true, SameStream(&expected.stream, self), "mask %d", self->bitMask_);
}

// --- Channel writers ---------------------------------------------------------------------------------

// 0047E6D0: PUSH EBP (1), PUSH ESI (1), MOV ESI,[ESP+0xC] (4).
void(__fastcall* s_writeRaw)(const void*, std::uint32_t, BitStream*);
void __fastcall WriteRawVerify(const void* data, std::uint32_t size, BitStream* out) {
    if (!IsMemoryStream(out)) return delta::WriteRaw(data, size, out);
    const KeptStream kept(out);
    delta::WriteRaw(data, size, out);
    TwinStream expected;
    s_writeRaw(data, size, expected.Make(kept, out));
    ei::verify::Check("delta::WriteRaw", true, SameStream(&expected.stream, out), "size %u", size);
}

// 0047E5E0: MOV EAX,[ESP+4] (4), PUSH EBX (1).
void(__fastcall* s_writeDiff)(const void*, const void*, std::uint32_t, BitStream*);
void __fastcall WriteDiffVerify(const void* old, const void* data, std::uint32_t size, BitStream* out) {
    if (!IsMemoryStream(out)) return delta::WriteDiff(old, data, size, out);
    const KeptStream kept(out);
    delta::WriteDiff(old, data, size, out);
    TwinStream expected;
    s_writeDiff(old, data, size, expected.Make(kept, out));
    ei::verify::Check("delta::WriteDiff", true, SameStream(&expected.stream, out), "size %u", size);
}

// 0047E740: SUB ESP,0xC (3), PUSH EBX (1), PUSH EBP (1).
void(__fastcall* s_writeChannel)(BitStream*, const void*, std::uint32_t, int, BitStream*);
void __fastcall WriteChannelVerify(BitStream* snapshot, const void* data, std::uint32_t size, int keep, BitStream* out) {
    if (!IsMemoryStream(snapshot) || !IsMemoryStream(out)) return delta::WriteDeltaChannel(snapshot, data, size, keep, out);
    const KeptStream keptSnapshot(snapshot);
    const KeptStream keptOut(out);
    delta::WriteDeltaChannel(snapshot, data, size, keep, out);
    TwinStream expectedSnapshot;
    TwinStream expectedOut;
    expectedSnapshot.Make(keptSnapshot, snapshot);
    expectedOut.Make(keptOut, out);
    s_writeChannel(&expectedSnapshot.stream, data, size, keep, &expectedOut.stream);
    bool same = SameStream(&expectedSnapshot.stream, snapshot) && SameStream(&expectedOut.stream, out);
    ei::verify::Check("delta::WriteDeltaChannel", true, same, "size %u keep %d", size, keep);
}

// --- DeltaField: begin / end -------------------------------------------------------------------------

// Runs `ours` on the real field, then `original` on a twin of the field (and of the reader when one
// is set) made from what was kept before ours; compares the fields, the reader, the globals and the
// returned values (via `sameRet`).
template <typename Original, typename Ours, typename SameRet>
void CheckField(const char* name, DeltaField* self, Original original, Ours ours, SameRet sameRet) {
    const Globals before = Globals::Save();
    if (!IsMemoryField(self) || (before.reader && !IsMemoryStream(before.reader))) {
        ours(self);
        return;
    }
    BitStream* realReader = before.reader;
    const KeptField keptField(self);
    KeptStream keptReader;
    if (realReader) keptReader.Keep(realReader);

    auto actualRet = ours(self);
    const Globals actual = Globals::Save();

    TwinField expected;
    expected.Make(keptField, self);
    TwinStream readerTwin;
    Globals scratch = before;
    scratch.snapshot = Map(before.snapshot, self, &expected.field, sizeof(DeltaField));
    if (realReader) scratch.reader = readerTwin.Make(keptReader, realReader);
    scratch.Restore();
    auto expectedRet = original(&expected.field);
    const Globals after = Globals::Save();
    actual.Restore();

    bool same = expected.Same(self) &&
                SameGlobals(after, actual, &expected.field, self, realReader ? &readerTwin.stream : nullptr, realReader) &&
                (!realReader || SameStream(&readerTwin.stream, realReader)) && sameRet(expectedRet, actualRet, &expected.field);
    ei::verify::Check(name, true, same, "");
}

// 0047DB70 / 0047FA40: PUSH ESI (1), MOV ESI,ECX (2), PUSH 0 (2).
int(__fastcall* s_beginWriteA)(DeltaField*, void*, int);
DeltaField*(__fastcall* s_beginWriteB)(DeltaField*, void*, int);
int __fastcall BeginWriteAVerify(DeltaField* self, void*, int channel1Only) {
    int result = 0;
    CheckField(
        "DeltaField::BeginWriteA", self, [&](DeltaField* f) { return s_beginWriteA(f, nullptr, channel1Only); },
        [&](DeltaField* f) { return result = f->BeginWriteA(channel1Only); },
        [](int e, int a, DeltaField*) { return e == a; });
    return result;
}
DeltaField* __fastcall BeginWriteBVerify(DeltaField* self, void*, int channel1Only) {
    CheckField(
        "DeltaField::BeginWriteB", self, [&](DeltaField* f) { return s_beginWriteB(f, nullptr, channel1Only); },
        [&](DeltaField* f) { return f->BeginWriteB(channel1Only); },
        [self](DeltaField* e, DeltaField* a, DeltaField* twin) { return e == twin && a == self; });
    return self;
}

// 0047DBD0 / 0047FAA0: PUSH ESI (1), MOV ESI,ECX (2), PUSH EDI (1), PUSH 0 (2).
std::uintptr_t(__fastcall* s_beginReadA)(DeltaField*, void*, int, BitStream*);
DeltaField*(__fastcall* s_beginReadB)(DeltaField*, void*, int, BitStream*);
std::uintptr_t __fastcall BeginReadAVerify(DeltaField* self, void*, int second, BitStream* reader) {
    std::uintptr_t result = 0;
    CheckField(
        "DeltaField::BeginReadA", self, [&](DeltaField* f) { return s_beginReadA(f, nullptr, second, reader); },
        [&](DeltaField* f) { return result = f->BeginReadA(second, reader); },
        [](std::uintptr_t e, std::uintptr_t a, DeltaField*) { return e == a; });
    return result;
}
DeltaField* __fastcall BeginReadBVerify(DeltaField* self, void*, int second, BitStream* reader) {
    CheckField(
        "DeltaField::BeginReadB", self, [&](DeltaField* f) { return s_beginReadB(f, nullptr, second, reader); },
        [&](DeltaField* f) { return f->BeginReadB(second, reader); },
        [self](DeltaField* e, DeltaField* a, DeltaField* twin) { return e == twin && a == self; });
    return self;
}

// 0047DC20 / 0047FAF0: MOV EAX,[0x007A9794] (5).
void(__fastcall* s_endField1)(DeltaField*);
void(__fastcall* s_endField2)(DeltaField*);
void CheckEnd(const char* name, void(__fastcall* original)(DeltaField*), DeltaField* self) {
    CheckField(
        name, self, [&](DeltaField* f) { original(f); return 0; }, [](DeltaField* f) { f->EndField(); return 0; },
        [](int, int, DeltaField*) { return true; });
}
void __fastcall EndField1Verify(DeltaField* self) { CheckEnd("DeltaField::EndField@0047DC20", s_endField1, self); }
void __fastcall EndField2Verify(DeltaField* self) { CheckEnd("DeltaField::EndField@0047FAF0", s_endField2, self); }

// --- DeltaField: writers -------------------------------------------------------------------------------

// 0047DCA0: SUB ESP,0x10 (3), MOV EAX,[0x007A9784] (5).
void(__fastcall* s_writeDelta)(DeltaField*, void*, const void*, std::uint32_t);
void __fastcall WriteDeltaVerify(DeltaField* self, void*, const void* data, std::uint32_t size) {
    CheckField(
        "DeltaField::WriteDelta", self, [&](DeltaField* f) { s_writeDelta(f, nullptr, data, size); return 0; },
        [&](DeltaField* f) { f->WriteDelta(data, size); return 0; }, [](int, int, DeltaField*) { return true; });
}

// 0047E140: PUSH EBX (1), MOV EBX,[ESP+8] (4).
void(__fastcall* s_writeBoth)(DeltaField*, void*, const void*, std::uint32_t);
void __fastcall WriteBothVerify(DeltaField* self, void*, const void* data, std::uint32_t size) {
    CheckField(
        "DeltaField::WriteBoth", self, [&](DeltaField* f) { s_writeBoth(f, nullptr, data, size); return 0; },
        [&](DeltaField* f) { f->WriteBoth(data, size); return 0; }, [](int, int, DeltaField*) { return true; });
}

// 0047E2D0: PUSH EBX, PUSH EBP, PUSH ESI (3), MOV ESI,[ESP+0x10] (4).
void(__fastcall* s_writeString)(DeltaField*, void*, const char*);
void __fastcall WriteStringVerify(DeltaField* self, void*, const char* text) {
    CheckField(
        "DeltaField::WriteStringBoth", self, [&](DeltaField* f) { s_writeString(f, nullptr, text); return 0; },
        [&](DeltaField* f) { f->WriteStringBoth(text); return 0; }, [](int, int, DeltaField*) { return true; });
}

// --- DeltaField: readers -------------------------------------------------------------------------------

// Readers of a field: ours on the real objects into `actualOut`; then the original on twins of the
// field, of the reader and (when it is not part of this field) of the snapshot read against, writing
// its result into a buffer of ours that starts with the bytes `actualOut` held before (`size` bytes
// compared).
template <typename Original, typename Ours>
void CheckFieldRead(const char* name, DeltaField* self, void* actualOut, std::size_t size, Original original, Ours ours) {
    const Globals before = Globals::Save();
    if (!before.reader || !before.snapshot || size > 0x10000 || !IsMemoryField(self) || !IsMemoryStream(before.reader) ||
        !IsMemoryStream(before.snapshot)) {
        ours(actualOut);
        return;
    }
    BitStream* outside = Inside(before.snapshot, self, sizeof(DeltaField)) ? nullptr : before.snapshot;
    const KeptField keptField(self);
    const KeptStream keptReader(before.reader);
    KeptStream keptOutside;
    if (outside) keptOutside.Keep(outside);
    std::vector<std::uint8_t> buffer = KeepBytes(actualOut, size);

    ours(actualOut);
    const Globals actual = Globals::Save();

    TwinField expected;
    expected.Make(keptField, self);
    TwinStream reader;
    TwinStream snapshotTwin;
    Globals scratch = before;
    scratch.reader = reader.Make(keptReader, before.reader);
    scratch.snapshot = outside ? snapshotTwin.Make(keptOutside, outside) : Map(before.snapshot, self, &expected.field, sizeof(DeltaField));
    scratch.Restore();
    original(&expected.field, buffer.data());
    actual.Restore();

    bool same = expected.Same(self) && SameStream(&reader.stream, before.reader) &&
                (!outside || SameStream(&snapshotTwin.stream, outside)) && std::memcmp(buffer.data(), actualOut, size) == 0 &&
                actual.snapshot == before.snapshot && actual.reader == before.reader;
    ei::verify::Check(name, true, same, "size %u", static_cast<unsigned>(size));
}

// 0047E000: SUB ESP,0xC (3), MOV [ESP+8],ECX (4).
void(__fastcall* s_readDelta)(DeltaField*, void*, void*, std::uint32_t);
void __fastcall ReadDeltaVerify(DeltaField* self, void*, void* data, std::uint32_t size) {
    CheckFieldRead(
        "DeltaField::ReadDelta", self, data, size,
        [&](DeltaField* f, void* out) { s_readDelta(f, nullptr, out, size); },
        [&](void* out) { self->ReadDelta(out, size); });
}

// 0047E930: SUB ESP,8 (3), PUSH EBX (1), PUSH EBP (1).
void(__fastcall* s_readPosition)(DeltaField*, void*, Vector3*);
void __fastcall ReadPositionVerify(DeltaField* self, void*, Vector3* position) {
    CheckFieldRead(
        "DeltaField::ReadPosition", self, position, sizeof(Vector3),
        [&](DeltaField* f, void* out) { s_readPosition(f, nullptr, static_cast<Vector3*>(out)); },
        [&](void* out) { self->ReadPosition(static_cast<Vector3*>(out)); });
}

// 0047E3A0: PUSH EBP (1), MOV EBP,ESP (2), SUB ESP,0x14 (3).
void(__fastcall* s_writePosition)(DeltaField*, void*, const Vector3*);
void __fastcall WritePositionVerify(DeltaField* self, void*, const Vector3* position) {
    CheckField(
        "DeltaField::WritePosition", self, [&](DeltaField* f) { s_writePosition(f, nullptr, position); return 0; },
        [&](DeltaField* f) { f->WritePosition(position); return 0; }, [](int, int, DeltaField*) { return true; });
}

// 0047EE00: SUB ESP,8 (3), PUSH EBX (1), PUSH EBP (1).
void(__fastcall* s_readPosition2D)(DeltaField*, void*, float*);
void __fastcall ReadPosition2DVerify(DeltaField* self, void*, float* position) {
    CheckFieldRead(
        "DeltaField::ReadPosition2D", self, position, 2 * sizeof(float),
        [&](DeltaField* f, void* out) { s_readPosition2D(f, nullptr, static_cast<float*>(out)); },
        [&](void* out) { self->ReadPosition2D(static_cast<float*>(out)); });
}

// 0047EBF0: PUSH EBP (1), MOV EBP,ESP (2), SUB ESP,0xC (3).
void(__fastcall* s_writePosition2D)(DeltaField*, void*, const float*);
void __fastcall WritePosition2DVerify(DeltaField* self, void*, const float* position) {
    CheckField(
        "DeltaField::WritePosition2D", self, [&](DeltaField* f) { s_writePosition2D(f, nullptr, position); return 0; },
        [&](DeltaField* f) { f->WritePosition2D(position); return 0; }, [](int, int, DeltaField*) { return true; });
}

// Typed writers (on a field twin) and readers (into scratch output).
template <typename Fn>
void CheckWriter(const char* name, DeltaField* self, Fn call) {
    CheckField(
        name, self, [&](DeltaField* f) { call(f, true); return 0; }, [&](DeltaField* f) { call(f, false); return 0; },
        [](int, int, DeltaField*) { return true; });
}

// 0047EF80: PUSH EBP (1), MOV EBP,ESP (2), FLD [EBP+0x10] (3).
void(__fastcall* s_writeRanged)(DeltaField*, void*, float, float, float);
void __fastcall WriteRangedVerify(DeltaField* self, void*, float value, float min, float max) {
    CheckWriter("DeltaField::WriteRanged", self, [&](DeltaField* f, bool original) {
        if (original) s_writeRanged(f, nullptr, value, min, max);
        else f->WriteRanged(value, min, max);
    });
}

// 0047F1E0: PUSH ECX (1), MOV EAX,[ESP+8] (4).
void(__fastcall* s_writeFloat16)(DeltaField*, void*, std::uint32_t);
void __fastcall WriteFloat16Verify(DeltaField* self, void*, std::uint32_t bits) {
    CheckWriter("DeltaField::WriteFloat16", self, [&](DeltaField* f, bool original) {
        if (original) s_writeFloat16(f, nullptr, bits);
        else f->WriteFloat16(bits);
    });
}

// 0047F4D0: SUB ESP,0xC (3), MOV EAX,[ESP+0x10] (4).
void(__fastcall* s_writeFloat16x3)(DeltaField*, void*, const float*);
void __fastcall WriteFloat16x3Verify(DeltaField* self, void*, const float* values) {
    CheckWriter("DeltaField::WriteFloat16x3", self, [&](DeltaField* f, bool original) {
        if (original) s_writeFloat16x3(f, nullptr, values);
        else f->WriteFloat16x3(values);
    });
}

// 0047F110: PUSH ECX (1), FLD [ESP+0x10] (4).
void(__fastcall* s_readRanged)(DeltaField*, void*, float*, float, float);
void __fastcall ReadRangedVerify(DeltaField* self, void*, float* value, float min, float max) {
    CheckFieldRead(
        "DeltaField::ReadRanged", self, value, sizeof(float),
        [&](DeltaField* f, void* out) { s_readRanged(f, nullptr, static_cast<float*>(out), min, max); },
        [&](void* out) { self->ReadRanged(static_cast<float*>(out), min, max); });
}

// 0047F3D0: PUSH ECX, PUSH EBX, PUSH EBP (3), MOV EBP,ECX (2).
void(__fastcall* s_readFloat16)(DeltaField*, void*, float*);
void __fastcall ReadFloat16Verify(DeltaField* self, void*, float* value) {
    CheckFieldRead(
        "DeltaField::ReadFloat16", self, value, sizeof(float),
        [&](DeltaField* f, void* out) { s_readFloat16(f, nullptr, static_cast<float*>(out)); },
        [&](void* out) { self->ReadFloat16(static_cast<float*>(out)); });
}

// 0047F7F0: PUSH ECX, PUSH EBX, PUSH EBP (3), MOV EBP,ECX (2).
void(__fastcall* s_readFloat16x3)(DeltaField*, void*, float*);
void __fastcall ReadFloat16x3Verify(DeltaField* self, void*, float* values) {
    CheckFieldRead(
        "DeltaField::ReadFloat16x3", self, values, 3 * sizeof(float),
        [&](DeltaField* f, void* out) { s_readFloat16x3(f, nullptr, static_cast<float*>(out)); },
        [&](void* out) { self->ReadFloat16x3(static_cast<float*>(out)); });
}

// 0047EAE0: PUSH ESI (1), MOV ESI,ECX (2), MOV ECX,[ESI+0x10] (3).
int(__fastcall* s_getBit)(BitStream*);
int __fastcall GetBitVerify(BitStream* reader) {
    if (!IsMemoryStream(reader)) return delta::GetBit(reader);
    const KeptStream kept(reader);
    int actualBit = delta::GetBit(reader);
    TwinStream expected;
    int expectedBit = s_getBit(expected.Make(kept, reader));
    ei::verify::Check("delta::GetBit", true, expectedBit == actualBit && SameStream(&expected.stream, reader), "%d / %d",
                      expectedBit, actualBit);
    return actualBit;
}

// 0047EB20: PUSH EBP (1), MOV EBP,[ESP+0xC] (4). Only AL is the result. The original reads `old` as
// it was before ours ran (it may be `data` itself) and writes into a buffer of ours that starts with
// the bytes `data` held.
int(__fastcall* s_readDiff)(const void*, void*, std::uint32_t, BitStream*);
bool __fastcall ReadDiffVerify(const void* old, void* data, std::uint32_t size, BitStream* reader) {
    if (size > 0x10000 || !IsMemoryStream(reader)) return delta::ReadDiff(old, data, size, reader);
    const KeptStream kept(reader);
    const std::vector<std::uint8_t> keptOld = KeepBytes(old, size);
    std::vector<std::uint8_t> buffer = KeepBytes(data, size);
    bool actualRet = delta::ReadDiff(old, data, size, reader);
    TwinStream expected;
    bool expectedRet = (s_readDiff(old ? keptOld.data() : nullptr, buffer.data(), size, expected.Make(kept, reader)) & 0xFF) != 0;
    bool same = expectedRet == actualRet && SameStream(&expected.stream, reader) && std::memcmp(buffer.data(), data, size) == 0;
    ei::verify::Check("delta::ReadDiff", true, same, "size %u", size);
    return actualRet;
}

// 0047E170: MOV EDX,[ESP+8] (4), MOV ECX,[0x007A9798] (6).
std::uint32_t(__fastcall* s_readRaw)(DeltaField*, void*, void*, std::uint32_t);
std::uint32_t __fastcall ReadRawVerify(DeltaField* self, void*, void* data, std::uint32_t size) {
    BitStream* realReader = delta::Reader();
    if (!realReader || size > 0x10000 || !IsMemoryStream(realReader)) return self->ReadRaw(data, size);
    const KeptStream kept(realReader);
    std::vector<std::uint8_t> buffer = KeepBytes(data, size);
    std::uint32_t actual = self->ReadRaw(data, size);
    TwinStream reader;
    delta::Reader() = reader.Make(kept, realReader);
    std::uint32_t expected = s_readRaw(self, nullptr, buffer.data(), size);
    delta::Reader() = realReader;
    bool same = expected == actual && SameStream(&reader.stream, realReader) && std::memcmp(buffer.data(), data, size) == 0;
    ei::verify::Check("DeltaField::ReadRaw", true, same, "size %u: %u / %u", size, expected, actual);
    return actual;
}

// 0047E310 builds a CString: ours only (it forwards to 00461190 on the reader).
void* __fastcall ReadStringVerify(DeltaField* self, void*, void* out) {
    void* result = self->ReadString(out);
    ei::verify::Check("DeltaField::ReadString", true, result == out, "");
    return result;
}

// --- DeltaState ------------------------------------------------------------------------------------

constexpr std::size_t kStateHeader = offsetof(DeltaState, header_);

bool SameStateHeader(const DeltaState* a, const DeltaState* b) {
    // Everything before the header streams except the field array pointer.
    return std::memcmp(a, b, offsetof(DeltaState, fields_)) == 0 &&
           std::memcmp(&a->fieldCount_, &b->fieldCount_, kStateHeader - offsetof(DeltaState, fieldCount_)) == 0;
}

// Runs `ours` on the real state, then `original` on a twin of the whole state (headers and every
// field) made from what was kept before ours; the states and the globals must match.
template <typename Original, typename Ours>
void CheckState(const char* name, DeltaState* self, Original original, Ours ours) {
    const KeptState kept(self);
    if (!kept.valid()) {
        ours(self);
        return;
    }
    const int mode = self->mode_;
    const Globals before = Globals::Save();
    ours(self);
    const Globals actual = Globals::Save();
    if (self->fieldCount_ != kept.fieldCount()) {   // neither function changes the field count
        ei::verify::Check(name, kept.fieldCount(), self->fieldCount_, "field count, mode %d", mode);
        return;
    }
    TwinState expected(kept, self);
    before.Restore();
    original(expected.get());
    const Globals after = Globals::Save();
    actual.Restore();
    bool same = expected.Same(self) && after.keep0 == actual.keep0 && after.keep1 == actual.keep1 &&
                after.snapshot == actual.snapshot && after.reader == actual.reader;
    ei::verify::Check(name, true, same, "mode %d, %d fields", mode, self->fieldCount_);
}

// 0047FB70: PUSH EBX (1), PUSH EDI (1), MOV EDI,ECX (2), XOR EBX,EBX (2).
void(__fastcall* s_reset)(DeltaState*);
void __fastcall ResetVerify(DeltaState* self) {
    CheckState("DeltaState::Reset", self, [](DeltaState* s) { s_reset(s); }, [](DeltaState* s) { s->Reset(); });
}

// 0047FBF0: PUSH EBX, PUSH EBP, PUSH EDI (3), MOV EDI,[ESP+0x10] (4). Only field `index` is touched:
// the twin state's array pointer is set so that its element `index` is the twin field.
DeltaField*(__fastcall* s_beginField)(DeltaState*, void*, int);
DeltaField* __fastcall BeginFieldVerify(DeltaState* self, void*, int index) {
    if (!((1u << (index & 31)) & self->mask_)) {
        DeltaField* expected = s_beginField(self, nullptr, index);
        DeltaField* actual = self->BeginField(index);
        ei::verify::Check("DeltaState::BeginField", true, expected == actual, "index %d (not in mask)", index);
        return actual;
    }
    DeltaField* real = &self->fields_[index];
    if (!IsMemoryField(real)) return self->BeginField(index);
    alignas(4) unsigned char raw[kStateHeader];
    auto* state = reinterpret_cast<DeltaState*>(raw);
    std::memcpy(state, self, kStateHeader);
    const KeptField kept(real);
    const Globals before = Globals::Save();
    DeltaField* actualRet = self->BeginField(index);
    const Globals actual = Globals::Save();
    TwinField expected;
    state->fields_ = expected.Make(kept, real) - index;
    before.Restore();
    DeltaField* expectedRet = s_beginField(state, nullptr, index);
    const Globals after = Globals::Save();
    actual.Restore();
    bool same = expectedRet == &expected.field && actualRet == real && expected.Same(real) &&
                SameGlobals(after, actual, &expected.field, real, nullptr, nullptr) && SameStateHeader(state, self);
    ei::verify::Check("DeltaState::BeginField", true, same, "index %d mode %d", index, self->mode_);
    return actualRet;
}

// 0047FD50: MOV EAX,[ESP+4] (4), TEST EAX,EAX (2).
void(__fastcall* s_setMode)(DeltaState*, void*, int);
void __fastcall SetModeVerify(DeltaState* self, void*, int deltaOnly) {
    alignas(4) unsigned char raw[kStateHeader];
    std::memcpy(raw, self, kStateHeader);
    s_setMode(reinterpret_cast<DeltaState*>(raw), nullptr, deltaOnly);
    self->SetWriteMode(deltaOnly);
    ei::verify::Check("DeltaState::SetWriteMode", true, std::memcmp(raw, self, kStateHeader) == 0, "%d", deltaOnly);
}

// 0047FD80: PUSH ESI (1), MOV ESI,ECX (2), MOV ECX,[ESP+0xC] (4). The original reads a twin of the
// reader.
int(__fastcall* s_beginRead)(DeltaState*, void*, int, BitStream*);
int __fastcall BeginReadVerify(DeltaState* self, void*, int second, BitStream* reader) {
    if (!IsMemoryStream(reader)) return self->BeginRead(second, reader);
    alignas(4) unsigned char raw[kStateHeader];
    auto* state = reinterpret_cast<DeltaState*>(raw);
    std::memcpy(state, self, kStateHeader);
    const KeptStream kept(reader);
    int actual = self->BeginRead(second, reader);
    TwinStream readerTwin;
    int expected = s_beginRead(state, nullptr, second, readerTwin.Make(kept, reader));
    state->reader_ = state->reader_ == &readerTwin.stream ? reader : nullptr;
    bool same = expected == actual && std::memcmp(raw, self, kStateHeader) == 0 && SameStream(&readerTwin.stream, reader);
    ei::verify::Check("DeltaState::BeginRead", true, same, "%d", second);
    return actual;
}

// 0047FE90 (A-E1): SUB ESP,0xC (3), PUSH EBX, PUSH EBP, PUSH ESI (3). Side by side on the state
// itself: copied, the original run, its result copied, the first copy put back, ours run. A state
// with a stream that is not a memory stream runs ours only. Every write and read of an object's
// network state ends with it (the world's updates, main thread).
// Not ours first (SV2h2): the real state's buffers end as the first copy's blocks, made on every
// call. Ours first on the real state with a twin for the original (CheckState, as Reset) keeps the
// real blocks as in a --no-hooks run, but then console Rebirth (update 40, the script's #gobject)
// and ui_widescreen_hud / _more Rebirth (update 80, cunit148's rotation by one ulp) part from their
// goldens, which were recorded where ours' layout follows this order; see the notes (SV2h2).
void(__fastcall* s_finish)(DeltaState*);
void __fastcall FinishVerify(DeltaState* self) {
    deltasnap::StateCopy before(self);
    if (!before.valid()) {
        self->Finish();
        return;
    }
    int mode = self->mode_;
    s_finish(self);
    deltasnap::StateCopy expected(self);
    before.MoveInto(self);
    self->Finish();
    ei::verify::Check("DeltaState::Finish", true, expected.Same(self), "mode %d, %d fields", mode, self->fieldCount_);
}

// 0047E340 (A-E1): PUSH ECX (1), LEA EAX,[ESP] (4). Ours reads the reader into `text`; the original a
// twin of it into a buffer of the same size; the buffers are compared by content and the readers
// must end alike. The unit and effect readers call it on the client's updates.
void(__fastcall* s_readText)(DeltaField*, void*, char*, int);
void __fastcall ReadTextVerify(DeltaField* self, void*, char* text, int size) {
    BitStream* realReader = delta::Reader();
    if (!realReader || size <= 0 || size > 0x10000 || !IsMemoryStream(realReader)) {
        self->ReadText(text, size);
        return;
    }
    const KeptStream kept(realReader);
    self->ReadText(text, size);
    std::vector<char> buffer(static_cast<std::size_t>(size), '\0');
    TwinStream reader;
    delta::Reader() = reader.Make(kept, realReader);
    s_readText(self, nullptr, buffer.data(), size);
    delta::Reader() = realReader;
    bool same = SameStream(&reader.stream, realReader) && std::strcmp(buffer.data(), text) == 0;
    ei::verify::Check("DeltaField::ReadText", true, same, "size %d: \"%.40s\" / \"%.40s\"", size, buffer.data(), text);
}

} // namespace

EI_VERIFY(0x0047FE90, FinishVerify, 6, s_finish);
EI_VERIFY(0x0047E340, ReadTextVerify, 5, s_readText);
EI_VERIFY(0x0047DFC0, PutBitVerify, 5, s_putBit);
EI_VERIFY(0x0047E6D0, WriteRawVerify, 6, s_writeRaw);
EI_VERIFY(0x0047E5E0, WriteDiffVerify, 5, s_writeDiff);
EI_VERIFY(0x0047E740, WriteChannelVerify, 5, s_writeChannel);
EI_VERIFY(0x0047DB70, BeginWriteAVerify, 5, s_beginWriteA);
EI_VERIFY(0x0047FA40, BeginWriteBVerify, 5, s_beginWriteB);
EI_VERIFY(0x0047DBD0, BeginReadAVerify, 6, s_beginReadA);
EI_VERIFY(0x0047FAA0, BeginReadBVerify, 6, s_beginReadB);
EI_VERIFY(0x0047DC20, EndField1Verify, 5, s_endField1);
EI_VERIFY(0x0047FAF0, EndField2Verify, 5, s_endField2);
EI_VERIFY(0x0047DCA0, WriteDeltaVerify, 8, s_writeDelta);
EI_VERIFY(0x0047E140, WriteBothVerify, 5, s_writeBoth);
EI_VERIFY(0x0047E2D0, WriteStringVerify, 7, s_writeString);
EI_VERIFY(0x0047E000, ReadDeltaVerify, 7, s_readDelta);
EI_VERIFY(0x0047E170, ReadRawVerify, 10, s_readRaw);
EI_VERIFY(0x0047E930, ReadPositionVerify, 5, s_readPosition);
EI_VERIFY(0x0047E3A0, WritePositionVerify, 6, s_writePosition);
EI_VERIFY(0x0047EAE0, GetBitVerify, 6, s_getBit);
EI_VERIFY(0x0047EE00, ReadPosition2DVerify, 5, s_readPosition2D);
EI_VERIFY(0x0047EBF0, WritePosition2DVerify, 6, s_writePosition2D);
EI_VERIFY(0x0047EF80, WriteRangedVerify, 6, s_writeRanged);
EI_VERIFY(0x0047F1E0, WriteFloat16Verify, 5, s_writeFloat16);
EI_VERIFY(0x0047F4D0, WriteFloat16x3Verify, 7, s_writeFloat16x3);
EI_VERIFY(0x0047F110, ReadRangedVerify, 5, s_readRanged);
EI_VERIFY(0x0047F3D0, ReadFloat16Verify, 5, s_readFloat16);
EI_VERIFY(0x0047F7F0, ReadFloat16x3Verify, 5, s_readFloat16x3);
EI_VERIFY(0x0047EB20, ReadDiffVerify, 5, s_readDiff);
EI_VERIFY(0x0047E310, ReadStringVerify, 0, s_unused);
EI_VERIFY(0x0047FB70, ResetVerify, 6, s_reset);
EI_VERIFY(0x0047FBF0, BeginFieldVerify, 7, s_beginField);
EI_VERIFY(0x0047FD50, SetModeVerify, 6, s_setMode);
EI_VERIFY(0x0047FD80, BeginReadVerify, 7, s_beginRead);

// --- AIPath network state (src/game/ai_path_state.cpp) -------------------------------------------------
// Unit state messages and the path message (0x0E) write it every time a unit's order or steps change
// (every scenario with moving units); the client's reads run on the units' updates.

// 005C7DB0 WriteState: SUB ESP,0xC (3), PUSH EBX (1), MOV EBX,[ESP+0x14] (4). Ours writes into the
// field, the original into a twin of it made afterwards; the fields are compared.
static void(__fastcall* s_pathWrite)(AIPath*, void*, DeltaField*);
static void __fastcall PathWriteStateVerify(AIPath* self, void*, DeltaField* field) {
    CheckField(
        "AIPath::WriteState", field, [&](DeltaField* f) { s_pathWrite(self, nullptr, f); return 0; },
        [&](DeltaField* f) { self->WriteState(f); return 0; }, [](int, int, DeltaField*) { return true; });
}
EI_VERIFY(0x005C7DB0, PathWriteStateVerify, 8, s_pathWrite);

// 005C7FC0 ReadState: SUB ESP,0x20 (3), LEA EAX,[ESP+0xC] (4). Ours reads into the path first; then the
// original from twins of the field and the reader (kept before ours) into a scratch path (empty
// steps, freed afterwards). Compared: the field, the reader, the order (0x20 bytes) and the steps.
static void(__fastcall* s_pathRead)(AIPath*, void*, DeltaField*);
static void __fastcall PathReadStateVerify(AIPath* self, void*, DeltaField* field) {
    const Globals before = Globals::Save();
    if (!before.reader || !before.snapshot || !IsMemoryField(field) || !IsMemoryStream(before.reader) ||
        !IsMemoryStream(before.snapshot) || !Inside(before.snapshot, field, sizeof(DeltaField))) {
        self->ReadState(field);
        return;
    }
    const KeptField keptField(field);
    const KeptStream keptReader(before.reader);
    self->ReadState(field);
    const Globals actual = Globals::Save();

    TwinField expected;
    expected.Make(keptField, field);
    TwinStream reader;
    alignas(4) unsigned char scratchBytes[sizeof(AIPath)] = {};
    auto* scratch = reinterpret_cast<AIPath*>(scratchBytes);
    scratch->steps.vtable = PathCellListVtable();
    scratch->steps.blockSize = 0x80;
    Globals scratchGlobals = before;
    scratchGlobals.reader = reader.Make(keptReader, before.reader);
    scratchGlobals.snapshot = Map(before.snapshot, field, &expected.field, sizeof(DeltaField));
    scratchGlobals.Restore();
    s_pathRead(scratch, nullptr, &expected.field);
    actual.Restore();
    bool same = expected.Same(field) && SameStream(&reader.stream, before.reader) &&
                std::memcmp(scratch, self, 0x20) == 0 && scratch->steps.count == self->steps.count;
    for (auto *a = scratch->steps.head, *b = self->steps.head; same && a && b; a = a->next, b = b->next)
        same = std::memcmp(&a->data, &b->data, sizeof(PathCell)) == 0;
    ei::verify::Check("AIPath::ReadState", true, same, "%d steps (original %d)", self->steps.count, scratch->steps.count);
    scratch->steps.RemoveAll();
}
EI_VERIFY(0x005C7FC0, PathReadStateVerify, 7, s_pathRead);
