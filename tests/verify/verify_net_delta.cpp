// Differential checks for net_delta.cpp.
//
// Everything here changes memory streams (headers, buffers that may be reallocated) and a few
// globals. Each wrapper deep-copies the streams the function can touch (header plus a private copy
// of the buffer from the game heap), points the globals at the copies, runs the original on the
// copies, restores the globals and runs ours on the real objects. The streams must then match:
// every header field except the buffer pointer, and the buffer contents up to the stream's size.
// The globals must match too (pointers into the copied field compared by their offset). The
// copies' buffers are freed afterwards.
#include "verify.h"

#include "../../src/game/net_delta.h"
#include "../../src/lib/crt.h"

#include <cstring>

namespace {

void* s_unused;

// --- Stream copies ---------------------------------------------------------------------------------

// Only memory streams (the two CMemoryStream vtables) are copied: another stream class (e.g. one over
// a file) would share its underlying resource with the copy. Anything else runs ours only.
bool IsMemoryStream(const BitStream* s) {
    std::uintptr_t vtable;
    std::memcpy(&vtable, s, sizeof(vtable));
    return vtable == 0x0073BD90 || vtable == 0x0073C960;
}
bool IsMemoryField(const DeltaField* f) {
    return IsMemoryStream(&f->snapshot[0]) && IsMemoryStream(&f->snapshot[1]) && IsMemoryStream(&f->out[0]) &&
           IsMemoryStream(&f->out[1]);
}

void CloneStream(BitStream* dst, const BitStream* src) {
    std::memcpy(dst, src, sizeof(BitStream));
    if (!src->data_) return;
    std::uint32_t bytes = src->capacity_ > src->size_ ? src->capacity_ : src->size_;
    if (bytes == 0) bytes = 1;
    dst->data_ = static_cast<std::uint8_t*>(gamecrt::operator_new(bytes));
    std::memset(dst->data_, 0, bytes);
    std::memcpy(dst->data_, src->data_, src->size_);
}

void FreeStream(BitStream* s) {
    if (s->data_) gamecrt::operator_delete(s->data_);
    s->data_ = nullptr;
}

bool SameStream(const BitStream* a, const BitStream* b) {
    constexpr std::size_t kData = offsetof(BitStream, data_);
    const auto* x = reinterpret_cast<const unsigned char*>(a);
    const auto* y = reinterpret_cast<const unsigned char*>(b);
    if (std::memcmp(x, y, kData) != 0) return false;
    if (std::memcmp(x + kData + 4, y + kData + 4, sizeof(BitStream) - kData - 4) != 0) return false;
    if (a->size_ == 0) return true;
    if (!a->data_ || !b->data_) return a->data_ == b->data_;
    return std::memcmp(a->data_, b->data_, a->size_) == 0;
}

struct FieldCopy {
    DeltaField field;
    FieldCopy(const DeltaField* src) {
        for (int i = 0; i < 2; ++i) {
            CloneStream(&field.snapshot[i], &src->snapshot[i]);
            CloneStream(&field.out[i], &src->out[i]);
        }
    }
    ~FieldCopy() {
        for (int i = 0; i < 2; ++i) {
            FreeStream(&field.snapshot[i]);
            FreeStream(&field.out[i]);
        }
    }
    bool Same(const DeltaField* other) const {
        for (int i = 0; i < 2; ++i) {
            if (!SameStream(&field.snapshot[i], &other->snapshot[i])) return false;
            if (!SameStream(&field.out[i], &other->out[i])) return false;
        }
        return true;
    }
};

struct StreamCopy {
    BitStream stream;
    explicit StreamCopy(const BitStream* src) { CloneStream(&stream, src); }
    ~StreamCopy() { FreeStream(&stream); }
};

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

// Globals after the original (on copies) and after ours (on the real objects) agree, with pointers
// into the copied field / reader mapped back.
bool SameGlobals(const Globals& expected, const Globals& actual, const DeltaField* copy, const DeltaField* real,
                 const BitStream* readerCopy, const BitStream* realReader) {
    BitStream* snapshot = Map(expected.snapshot, copy, real, sizeof(DeltaField));
    BitStream* reader = expected.reader == readerCopy ? const_cast<BitStream*>(realReader) : expected.reader;
    return expected.keep0 == actual.keep0 && expected.keep1 == actual.keep1 && snapshot == actual.snapshot &&
           reader == actual.reader;
}

// --- BitStream ---------------------------------------------------------------------------------------

// 0047DFC0: MOV DL,[ESP+4] (4), PUSH EBX (1).
void(__fastcall* s_putBit)(BitStream*, void*, int);
void __fastcall PutBitVerify(BitStream* self, void*, int bit) {
    if (!IsMemoryStream(self)) return self->PutBit((bit & 0xFF) != 0);
    StreamCopy expected(self);
    s_putBit(&expected.stream, nullptr, bit);
    self->PutBit((bit & 0xFF) != 0);
    ei::verify::Check("BitStream::PutBit", true, SameStream(&expected.stream, self), "mask %d", self->mask_);
}

// --- Channel writers ---------------------------------------------------------------------------------

// 0047E6D0: PUSH EBP (1), PUSH ESI (1), MOV ESI,[ESP+0xC] (4).
void(__fastcall* s_writeRaw)(const void*, std::uint32_t, BitStream*);
void __fastcall WriteRawVerify(const void* data, std::uint32_t size, BitStream* out) {
    if (!IsMemoryStream(out)) return delta::WriteRaw(data, size, out);
    StreamCopy expected(out);
    s_writeRaw(data, size, &expected.stream);
    delta::WriteRaw(data, size, out);
    ei::verify::Check("delta::WriteRaw", true, SameStream(&expected.stream, out), "size %u", size);
}

// 0047E5E0: MOV EAX,[ESP+4] (4), PUSH EBX (1).
void(__fastcall* s_writeDiff)(const void*, const void*, std::uint32_t, BitStream*);
void __fastcall WriteDiffVerify(const void* old, const void* data, std::uint32_t size, BitStream* out) {
    if (!IsMemoryStream(out)) return delta::WriteDiff(old, data, size, out);
    StreamCopy expected(out);
    s_writeDiff(old, data, size, &expected.stream);
    delta::WriteDiff(old, data, size, out);
    ei::verify::Check("delta::WriteDiff", true, SameStream(&expected.stream, out), "size %u", size);
}

// 0047E740: SUB ESP,0xC (3), PUSH EBX (1), PUSH EBP (1).
void(__fastcall* s_writeChannel)(BitStream*, const void*, std::uint32_t, int, BitStream*);
void __fastcall WriteChannelVerify(BitStream* snapshot, const void* data, std::uint32_t size, int keep, BitStream* out) {
    if (!IsMemoryStream(snapshot) || !IsMemoryStream(out)) return delta::WriteDeltaChannel(snapshot, data, size, keep, out);
    StreamCopy expectedSnapshot(snapshot);
    StreamCopy expectedOut(out);
    s_writeChannel(&expectedSnapshot.stream, data, size, keep, &expectedOut.stream);
    delta::WriteDeltaChannel(snapshot, data, size, keep, out);
    bool same = SameStream(&expectedSnapshot.stream, snapshot) && SameStream(&expectedOut.stream, out);
    ei::verify::Check("delta::WriteDeltaChannel", true, same, "size %u keep %d", size, keep);
}

// --- DeltaField: begin / end -------------------------------------------------------------------------

// Runs `original` on a copy of the field (and of the reader when one is set), then `ours` on the
// real field; compares the fields, the reader, the globals and the returned values (via `sameRet`).
template <typename Original, typename Ours, typename SameRet>
void CheckField(const char* name, DeltaField* self, Original original, Ours ours, SameRet sameRet) {
    const Globals before = Globals::Save();
    if (!IsMemoryField(self) || (before.reader && !IsMemoryStream(before.reader))) {
        ours(self);
        return;
    }
    FieldCopy expected(self);
    BitStream* realReader = before.reader;
    StreamCopy* readerCopy = realReader ? new StreamCopy(realReader) : nullptr;
    Globals scratch = before;
    scratch.snapshot = Map(before.snapshot, self, &expected.field, sizeof(DeltaField));
    if (readerCopy) scratch.reader = &readerCopy->stream;
    scratch.Restore();
    auto expectedRet = original(&expected.field);
    const Globals after = Globals::Save();
    before.Restore();
    auto actualRet = ours(self);
    bool same = expected.Same(self) && SameGlobals(after, Globals::Save(), &expected.field, self,
                                                   readerCopy ? &readerCopy->stream : nullptr, realReader) &&
                (!readerCopy || SameStream(&readerCopy->stream, realReader)) && sameRet(expectedRet, actualRet, &expected.field);
    ei::verify::Check(name, true, same, "");
    delete readerCopy;
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
        [self](DeltaField* e, DeltaField* a, DeltaField* copy) { return e == copy && a == self; });
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
        [self](DeltaField* e, DeltaField* a, DeltaField* copy) { return e == copy && a == self; });
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

// Readers of a field: the original runs on a copy of the field, of the reader and (when it is not
// part of this field) of the snapshot read against, writing its result into `expectedOut`; ours on
// the real objects into `actualOut` (`size` bytes compared).
template <typename Original, typename Ours>
void CheckFieldRead(const char* name, DeltaField* self, void* actualOut, std::size_t size, Original original, Ours ours) {
    const Globals before = Globals::Save();
    if (!before.reader || !before.snapshot || size > 0x10000 || !IsMemoryField(self) || !IsMemoryStream(before.reader) ||
        !IsMemoryStream(before.snapshot)) {
        ours(actualOut);
        return;
    }
    BitStream* outside = Inside(before.snapshot, self, sizeof(DeltaField)) ? nullptr : before.snapshot;
    FieldCopy expected(self);
    StreamCopy reader(before.reader);
    StreamCopy* snapshotCopy = outside ? new StreamCopy(outside) : nullptr;
    auto* buffer = static_cast<std::uint8_t*>(gamecrt::operator_new(size ? size : 1));
    Globals scratch = before;
    scratch.reader = &reader.stream;
    scratch.snapshot = outside ? &snapshotCopy->stream : Map(before.snapshot, self, &expected.field, sizeof(DeltaField));
    scratch.Restore();
    original(&expected.field, buffer);
    before.Restore();
    ours(actualOut);
    bool same = expected.Same(self) && SameStream(&reader.stream, before.reader) &&
                (!outside || SameStream(&snapshotCopy->stream, outside)) && std::memcmp(buffer, actualOut, size) == 0 &&
                Globals::Save().snapshot == before.snapshot && Globals::Save().reader == before.reader;
    ei::verify::Check(name, true, same, "size %u", static_cast<unsigned>(size));
    gamecrt::operator_delete(buffer);
    delete snapshotCopy;
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

// Typed writers (on a field copy) and readers (into scratch output).
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
    StreamCopy expected(reader);
    int expectedBit = s_getBit(&expected.stream);
    int actualBit = delta::GetBit(reader);
    ei::verify::Check("delta::GetBit", true, expectedBit == actualBit && SameStream(&expected.stream, reader), "%d / %d",
                      expectedBit, actualBit);
    return actualBit;
}

// 0047EB20: PUSH EBP (1), MOV EBP,[ESP+0xC] (4). Only AL is the result.
int(__fastcall* s_readDiff)(const void*, void*, std::uint32_t, BitStream*);
bool __fastcall ReadDiffVerify(const void* old, void* data, std::uint32_t size, BitStream* reader) {
    if (size > 0x10000 || !IsMemoryStream(reader)) return delta::ReadDiff(old, data, size, reader);
    StreamCopy expected(reader);
    auto* buffer = static_cast<std::uint8_t*>(gamecrt::operator_new(size ? size : 1));
    bool expectedRet = (s_readDiff(old, buffer, size, &expected.stream) & 0xFF) != 0;
    bool actualRet = delta::ReadDiff(old, data, size, reader);
    bool same = expectedRet == actualRet && SameStream(&expected.stream, reader) && std::memcmp(buffer, data, size) == 0;
    ei::verify::Check("delta::ReadDiff", true, same, "size %u", size);
    gamecrt::operator_delete(buffer);
    return actualRet;
}

// 0047E170: MOV EDX,[ESP+8] (4), MOV ECX,[0x007A9798] (6).
std::uint32_t(__fastcall* s_readRaw)(DeltaField*, void*, void*, std::uint32_t);
std::uint32_t __fastcall ReadRawVerify(DeltaField* self, void*, void* data, std::uint32_t size) {
    BitStream* realReader = delta::Reader();
    if (!realReader || size > 0x10000 || !IsMemoryStream(realReader)) return self->ReadRaw(data, size);
    StreamCopy reader(realReader);
    auto* buffer = static_cast<std::uint8_t*>(gamecrt::operator_new(size ? size : 1));
    delta::Reader() = &reader.stream;
    std::uint32_t expected = s_readRaw(self, nullptr, buffer, size);
    delta::Reader() = realReader;
    std::uint32_t actual = self->ReadRaw(data, size);
    bool same = expected == actual && SameStream(&reader.stream, realReader) && std::memcmp(buffer, data, size) == 0;
    ei::verify::Check("DeltaField::ReadRaw", true, same, "size %u: %u / %u", size, expected, actual);
    gamecrt::operator_delete(buffer);
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

// 0047FB70: PUSH EBX (1), PUSH EDI (1), MOV EDI,ECX (2), XOR EBX,EBX (2). The field array is copied.
void(__fastcall* s_reset)(DeltaState*);
void __fastcall ResetVerify(DeltaState* self) {
    bool memory = IsMemoryStream(&self->header_[0]) && IsMemoryStream(&self->header_[1]);
    for (int i = 0; memory && self->fieldCount_ <= 64 && i < self->fieldCount_; ++i) memory = IsMemoryField(&self->fields_[i]);
    if (self->fieldCount_ < 0 || self->fieldCount_ > 64 || !memory) {
        self->Reset();
        return;
    }
    alignas(4) unsigned char raw[sizeof(DeltaState)];
    auto* expected = reinterpret_cast<DeltaState*>(raw);
    std::memcpy(expected, self, kStateHeader);
    CloneStream(&expected->header_[0], &self->header_[0]);
    CloneStream(&expected->header_[1], &self->header_[1]);
    FieldCopy* fields[64] = {};
    auto* array = static_cast<DeltaField*>(gamecrt::operator_new(sizeof(DeltaField) * (self->fieldCount_ + 1)));
    for (int i = 0; i < self->fieldCount_; ++i) {
        fields[i] = new FieldCopy(&self->fields_[i]);
        std::memcpy(&array[i], &fields[i]->field, sizeof(DeltaField));
    }
    expected->fields_ = array;
    s_reset(expected);
    self->Reset();
    bool same = SameStateHeader(expected, self) && SameStream(&expected->header_[0], &self->header_[0]) &&
                SameStream(&expected->header_[1], &self->header_[1]);
    for (int i = 0; i < self->fieldCount_; ++i) {
        // The original resized the copies in `array`; their buffers are what must be freed.
        std::memcpy(&fields[i]->field, &array[i], sizeof(DeltaField));
        same = same && fields[i]->Same(&self->fields_[i]);
        delete fields[i];
    }
    gamecrt::operator_delete(array);
    FreeStream(&expected->header_[0]);
    FreeStream(&expected->header_[1]);
    ei::verify::Check("DeltaState::Reset", true, same, "fields %d", self->fieldCount_);
}

// 0047FBF0: PUSH EBX, PUSH EBP, PUSH EDI (3), MOV EDI,[ESP+0x10] (4). Only field `index` is touched:
// the copy's array pointer is set so that its element `index` is the copied field.
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
    FieldCopy expected(real);
    state->fields_ = &expected.field - index;
    const Globals before = Globals::Save();
    DeltaField* expectedRet = s_beginField(state, nullptr, index);
    const Globals after = Globals::Save();
    before.Restore();
    DeltaField* actualRet = self->BeginField(index);
    bool same = expectedRet == &expected.field && actualRet == real && expected.Same(real) &&
                SameGlobals(after, Globals::Save(), &expected.field, real, nullptr, nullptr) &&
                SameStateHeader(state, self);
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

// 0047FD80: PUSH ESI (1), MOV ESI,ECX (2), MOV ECX,[ESP+0xC] (4). The reader is copied.
int(__fastcall* s_beginRead)(DeltaState*, void*, int, BitStream*);
int __fastcall BeginReadVerify(DeltaState* self, void*, int second, BitStream* reader) {
    if (!IsMemoryStream(reader)) return self->BeginRead(second, reader);
    alignas(4) unsigned char raw[kStateHeader];
    auto* state = reinterpret_cast<DeltaState*>(raw);
    std::memcpy(state, self, kStateHeader);
    StreamCopy readerCopy(reader);
    int expected = s_beginRead(state, nullptr, second, &readerCopy.stream);
    int actual = self->BeginRead(second, reader);
    state->reader_ = state->reader_ == &readerCopy.stream ? reader : nullptr;
    bool same = expected == actual && std::memcmp(raw, self, kStateHeader) == 0 && SameStream(&readerCopy.stream, reader);
    ei::verify::Check("DeltaState::BeginRead", true, same, "%d", second);
    return actual;
}

} // namespace

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
