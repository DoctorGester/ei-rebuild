// Delta replication of object state. See net_delta.h.
#include "net_delta.h"

#include "core_maths.h"
#include "net_coder.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "ei/x87.h"
#include "originals_library.h"

#include <bit>
#include <cstring>
#include "originals_engine.h"

// --- BitStream ---------------------------------------------------------------------------------

namespace {

std::uint32_t Word(const std::uint8_t* p) {
    std::uint32_t w;
    std::memcpy(&w, p, sizeof(w));
    return w;
}
} // namespace

void CMemoryStream::PutBit(bool bit) {
    bitMask_ = static_cast<std::int32_t>(static_cast<std::uint32_t>(bitMask_) << 1);
    if (bitMask_ > 0x80) StartByte();   // sets bitMask_ to 1
    if (bit) bits_ |= static_cast<std::uint8_t>(bitMask_);
}
EI_REWRITTEN(0x0047DFC0, CMemoryStream::PutBit);

bool CMemoryStream::GetBit() {
    bitMask_ = static_cast<std::int32_t>(static_cast<std::uint32_t>(bitMask_) << 1);
    if (bitMask_ > 0x80) {
        bitMask_ = 1;
        Read(&bits_, 1);
    }
    return (bitMask_ & static_cast<std::int8_t>(bits_)) != 0;
}

// --- Whole streams: length-prefixed copies and the message compression ------------------------

namespace {
// ReadStream refuses longer streams.
constexpr std::uint32_t kMaxStreamLength = 100000;

// The growing buffer's constructor and destructor (not rewritten).

// The number of bytes in a buffer, its high-water mark raised to the cursor first (inlined).
std::uint32_t BufferLength(GameBuffer& b) {
    if (b.cur > b.high) b.high = b.cur;
    return static_cast<std::uint32_t>(b.base - reinterpret_cast<std::intptr_t>(b.begin) + reinterpret_cast<std::intptr_t>(b.high));
}

// 0047D7F0 / 0047D970: a fixed view over `self`'s bytes run through the coder into a growing
// buffer, whose bytes then replace `out`'s (Resize, Seek 0, Write, Seek 0). The view is destroyed
// (its base vtable) before the buffer.
void CodeStream(BitStream* self, CoderState* coder, BitStream* out, void (CoderState::*code)(GameBuffer*, GameBuffer*)) {
    GameBuffer result;
    static_cast<GameBuffer*>(&result)->ConstructGrowing();
    std::uint8_t* data = self->data_;
    std::uint8_t* last = data + self->Size();
    GameBuffer view;
    view.InitView(data, last);
    (coder->*code)(&view, &result);
    out->Resize(BufferLength(result));
    out->Seek(0);
    std::uint32_t length = BufferLength(result);
    out->Write(result.begin, length);
    out->Seek(0);
    ei::SetVtable(&view, GameBuffer::kViewBaseVtableAddress);
    GrowingBufferDestruct(&result);
}
} // namespace

void CMemoryStream::WriteStream(BitStream* from) {
    from->EndBits();
    std::int32_t header = static_cast<std::int32_t>(from->Size() << 1);
    if (header < 0x100) {
        Write(&header, 1);
    } else {
        header |= 1;
        Write(&header, 4);
    }
    std::uint8_t* data = from->data_;
    Write(data, from->Size());
}
EI_REWRITTEN(0x0047DA80, CMemoryStream::WriteStream);

int CMemoryStream::ReadStream(BitStream* to) {
    std::uint32_t length = 0;
    Read(&length, 1);
    if (length & 1) Read(reinterpret_cast<std::uint8_t*>(&length) + 1, 3);
    length >>= 1;
    if (length > kMaxStreamLength) return 0;
    to->SetSize(length);
    if (Read(to->data_, length) != length) return 0;
    to->Seek(0);
    to->ok_ = 1;
    return 1;
}
EI_REWRITTEN(0x0047DAE0, CMemoryStream::ReadStream);

void CMemoryStream::CompressTo(CoderState* coder, BitStream* out) { CodeStream(this, coder, out, &CoderState::Compress); }
EI_REWRITTEN(0x0047D7F0, CMemoryStream::CompressTo);

void CMemoryStream::DecompressTo(CoderState* coder, BitStream* out) { CodeStream(this, coder, out, &CoderState::Decompress); }
EI_REWRITTEN(0x0047D970, CMemoryStream::DecompressTo);

// --- Channel writers ---------------------------------------------------------------------------

namespace delta {

void __fastcall WriteRaw(const void* data, std::uint32_t size, BitStream* out) {
    const auto* p = static_cast<const std::uint8_t*>(data);
    std::uint32_t rest = size;
    out->PutBit(false);
    if (static_cast<int>(size) > 3) {
        std::uint32_t words = size >> 2;
        rest = size - words * 4;
        do {
            out->PutBit(false);
            out->Write(p, 4);
            p += 4;
        } while (--words);
    }
    out->Write(p, rest);
}
EI_REWRITTEN(0x0047E6D0, WriteRaw);

void __fastcall WriteDiff(const void* old, const void* data, std::uint32_t size, BitStream* out) {
    if (std::memcmp(old, data, size) == 0) {
        out->PutBit(true);
        return;
    }
    out->PutBit(false);
    const auto* p = static_cast<const std::uint8_t*>(data);
    std::uint32_t rest = size;
    if (static_cast<int>(size) > 3) {
        std::ptrdiff_t toOld = static_cast<const std::uint8_t*>(old) - p;
        std::uint32_t words = size >> 2;
        rest = size - words * 4;
        do {
            if (Word(p + toOld) == Word(p)) {
                out->PutBit(true);
            } else {
                out->PutBit(false);
                out->Write(p, 4);
            }
            p += 4;
        } while (--words);
    }
    out->Write(p, rest);
}
EI_REWRITTEN(0x0047E5E0, WriteDiff);

// Without a snapshot to compare with (it is shorter than position + size) everything is sent and
// appended to the snapshot. Otherwise a 1 bit means "all unchanged"; else a 0 bit and, per 4-byte
// word, 1 (unchanged) or 0 and the word, then the remaining bytes as they are. With
// `keepSnapshot` the snapshot is updated in place; it is advanced past the bytes either way.
void __fastcall WriteDeltaChannel(BitStream* snapshot, const void* data, std::uint32_t size, int keepSnapshot,
                                  BitStream* out) {
    std::uint32_t end = snapshot->Position() + size;
    if (snapshot->Size() < end) {
        WriteRaw(data, size, out);
        snapshot->Write(data, size);
        return;
    }
    std::uint8_t* buffer = snapshot->data_;
    std::uint8_t* current = buffer + snapshot->Position();
    WriteDiff(current, data, size, out);
    if (keepSnapshot) std::memcpy(current, data, size);
    snapshot->Seek(snapshot->Position() + size);
}
EI_REWRITTEN(0x0047E740, WriteDeltaChannel);

} // namespace delta

// --- DeltaField --------------------------------------------------------------------------------

namespace {
void RewindAndEmpty(DeltaField* field) {
    field->snapshot[0].Seek(0);
    field->snapshot[1].Seek(0);
    field->out[0].Resize(0);
    field->out[1].Resize(0);
}

void SetKeep(int channel1Only) {
    if (!channel1Only) {
        delta::KeepSnapshot0() = 1;
        delta::KeepSnapshot1() = 1;
    } else {
        delta::KeepSnapshot0() = 0;
        delta::KeepSnapshot1() = 1;
    }
}

void StartRead(DeltaField* field, int second, BitStream* reader) {
    field->snapshot[0].Seek(0);
    field->snapshot[1].Seek(0);
    delta::ReadSnapshot() = second ? &field->snapshot[1] : &field->snapshot[0];
    delta::Reader() = reader;
}
} // namespace

int DeltaField::BeginWriteA(int channel1Only) {
    RewindAndEmpty(this);
    SetKeep(channel1Only);
    return channel1Only ? channel1Only : 1;
}
EI_REWRITTEN(0x0047DB70, DeltaField::BeginWriteA);

DeltaField* DeltaField::BeginWriteB(int channel1Only) {
    RewindAndEmpty(this);
    SetKeep(channel1Only);
    return this;
}
EI_REWRITTEN(0x0047FA40, DeltaField::BeginWriteB);

std::uintptr_t DeltaField::BeginReadA(int second, BitStream* reader) {
    StartRead(this, second, reader);
    return second ? static_cast<std::uintptr_t>(second) : reinterpret_cast<std::uintptr_t>(reader);
}
EI_REWRITTEN(0x0047DBD0, DeltaField::BeginReadA);

DeltaField* DeltaField::BeginReadB(int second, BitStream* reader) {
    StartRead(this, second, reader);
    return this;
}
EI_REWRITTEN(0x0047FAA0, DeltaField::BeginReadB);

void DeltaField::EndField() {
    if (delta::ReadSnapshot()) {
        delta::Reader()->EndBits();
        delta::ReadSnapshot() = nullptr;
        delta::Reader() = nullptr;
        return;
    }
    out[0].EndBits();
    out[1].EndBits();
    if (delta::KeepSnapshot0()) snapshot[0].Resize(snapshot[0].Position());
    if (delta::KeepSnapshot1()) snapshot[1].Resize(snapshot[1].Position());
}
EI_REWRITTEN(0x0047DC20, DeltaField::EndField);
EI_REWRITTEN(0x0047FAF0, DeltaField::EndField);

void DeltaField::WriteDelta(const void* data, std::uint32_t size) {
    delta::WriteDeltaChannel(&snapshot[0], data, size, delta::KeepSnapshot0(), &out[0]);
    delta::WriteDeltaChannel(&snapshot[1], data, size, delta::KeepSnapshot1(), &out[1]);
}
EI_REWRITTEN(0x0047DCA0, DeltaField::WriteDelta);

// The bytes come from the snapshot read against (all unchanged, or per unchanged word) or from the
// reader. When anything changed the result is also written to snapshot[1] at the same place.
void DeltaField::ReadDelta(void* data, std::uint32_t size) {
    std::uint32_t position = delta::ReadSnapshot()->Position();
    BitStream* reader = delta::Reader();
    const std::uint8_t* current = delta::ReadSnapshot()->data_ + position;
    auto* p = static_cast<std::uint8_t*>(data);
    if (reader->GetBit()) {
        std::memcpy(p, current, size);
    } else {
        std::uint32_t rest = size;
        if (static_cast<int>(size) > 3) {
            std::ptrdiff_t toCurrent = current - p;
            std::uint32_t words = size >> 2;
            rest = size - words * 4;
            do {
                if (reader->GetBit()) std::memcpy(p, p + toCurrent, 4);
                else reader->Read(p, 4);
                p += 4;
            } while (--words);
        }
        reader->Read(p, rest);
        snapshot[1].Seek(position);
        snapshot[1].Write(data, size);
    }
    delta::ReadSnapshot()->Seek(position + size);
}
EI_REWRITTEN(0x0047E000, DeltaField::ReadDelta);

void DeltaField::WriteBoth(const void* data, std::uint32_t size) {
    out[0].Write(data, size);
    out[1].Write(data, size);
}
EI_REWRITTEN(0x0047E140, DeltaField::WriteBoth);

void DeltaField::WriteStringBoth(const char* text) {
    out[0].Write(text, static_cast<std::uint32_t>(std::strlen(text) + 1));
    out[1].Write(text, static_cast<std::uint32_t>(std::strlen(text) + 1));
}
EI_REWRITTEN(0x0047E2D0, DeltaField::WriteStringBoth);

std::uint32_t DeltaField::ReadRaw(void* data, std::uint32_t size) { return delta::Reader()->Read(data, size); }
EI_REWRITTEN(0x0047E170, DeltaField::ReadRaw);

void* DeltaField::ReadString(void* cstring) {
    delta::Reader()->ReadString(cstring);
    return cstring;
}
EI_REWRITTEN(0x0047E310, DeltaField::ReadString);

void DeltaField::WriteStreamBoth(BitStream* from) {
    out[0].WriteStream(from);
    out[1].WriteStream(from);
}
EI_REWRITTEN(0x0047E190, DeltaField::WriteStreamBoth);

void DeltaField::ReadText(char* text, int size) {
    alignas(mfc::CString) unsigned char storage[sizeof(mfc::CString)];
    auto* string = static_cast<mfc::CString*>(ReadString(storage));
    if (string->GetLength() < size - 1) {
        const char* from = *string;
        std::memcpy(text, from, std::strlen(from) + 1);
    } else {
        text[0] = '\0';
    }
    string->~CString();
}
EI_REWRITTEN(0x0047E340, DeltaField::ReadText);

int DeltaField::ReadStream(BitStream* to) { return delta::Reader()->ReadStream(to); }
EI_REWRITTEN(0x0047E240, DeltaField::ReadStream);

// --- Readers ------------------------------------------------------------------------------------

namespace delta {

int __fastcall GetBit(BitStream* reader) { return reader->GetBit() ? 1 : 0; }
EI_REWRITTEN(0x0047EAE0, GetBit);

bool __fastcall ReadDiff(const void* old, void* data, std::uint32_t size, BitStream* reader) {
    if (reader->GetBit()) {
        std::memcpy(data, old, size);
        return false;
    }
    auto* p = static_cast<std::uint8_t*>(data);
    std::uint32_t rest = size;
    if (static_cast<int>(size) > 3) {
        std::ptrdiff_t toOld = static_cast<const std::uint8_t*>(old) - p;
        std::uint32_t words = size >> 2;
        rest = size - words * 4;
        do {
            if (reader->GetBit()) std::memcpy(p, p + toOld, 4);
            else reader->Read(p, 4);
            p += 4;
        } while (--words);
    }
    reader->Read(p, rest);
    return true;
}
EI_REWRITTEN(0x0047EB20, ReadDiff);

} // namespace delta

// --- Positions ---------------------------------------------------------------------------------

namespace {
constexpr float kWriteOffsetXY = -16.0f;          // 0073CA8C
constexpr float kWriteScaleXY = 120.46875f;       // 0073CA88
constexpr float kWriteOffsetZ = -32.0f;           // 0073CA84
constexpr float kWriteScaleZ = 341.328125f;       // 0073CA80
const float kReadScaleXY = std::bit_cast<float>(0x3C080088u);   // 0073CA9C
constexpr float kReadOffsetXY = 16.0f;            // 0073CA98
const float kReadScaleZ = std::bit_cast<float>(0x3B4000C0u);    // 0073CA94
constexpr float kReadOffsetZ = 32.0f;             // 0073CA90
constexpr float kWriteOffset2D = -32.0f;          // 0073CA84
constexpr float kWriteScale2D = 60.234375f;       // 0073CAA0
const float kReadScale2D = std::bit_cast<float>(0x3C880088u);   // 0073CAA4
constexpr float kReadOffset2D = 32.0f;            // 0073CA90

int Quantize(float v, float offset, float scale) { return x87::fistp(static_cast<float>((v - offset) * scale)); }

float Dequantize(std::uint16_t q, float scale, float offset) { return static_cast<float>(static_cast<int>(q)) * scale - offset; }
} // namespace

void DeltaField::WritePosition(const Vector3* position) {
    int q = Quantize(position->x, kWriteOffsetXY, kWriteScaleXY);
    WriteDelta(&q, 2);
    q = Quantize(position->y, kWriteOffsetXY, kWriteScaleXY);
    WriteDelta(&q, 2);
    q = Quantize(position->z, kWriteOffsetZ, kWriteScaleZ);
    WriteDelta(&q, 2);
}
EI_REWRITTEN(0x0047E3A0, DeltaField::WritePosition);

void DeltaField::WritePosition2D(const float* position) {
    int q = Quantize(position[0], kWriteOffset2D, kWriteScale2D);
    WriteDelta(&q, 2);
    q = Quantize(position[1], kWriteOffset2D, kWriteScale2D);
    WriteDelta(&q, 2);
}
EI_REWRITTEN(0x0047EBF0, DeltaField::WritePosition2D);

// One 16-bit value with the bit read inline (unchanged: from the snapshot read against).
std::uint16_t DeltaField::ReadWord() {
    std::uint32_t at = delta::ReadSnapshot()->Position();
    BitStream* reader = delta::Reader();
    const std::uint8_t* current = delta::ReadSnapshot()->data_ + at;
    std::uint16_t q;
    if (reader->GetBit()) {
        std::memcpy(&q, current, 2);
    } else {
        reader->Read(&q, 2);
        snapshot[1].Seek(at);
        snapshot[1].Write(&q, 2);
    }
    delta::ReadSnapshot()->Seek(at + 2);
    return q;
}

void DeltaField::ReadPosition(Vector3* position) {
    position->x = Dequantize(ReadWord(), kReadScaleXY, kReadOffsetXY);
    position->y = Dequantize(ReadWord(), kReadScaleXY, kReadOffsetXY);
    // z goes through ReadDiff.
    std::uint32_t at = delta::ReadSnapshot()->Position();
    BitStream* reader = delta::Reader();
    std::uint16_t q;
    if (delta::ReadDiff(delta::ReadSnapshot()->data_ + at, &q, 2, reader)) {
        snapshot[1].Seek(at);
        snapshot[1].Write(&q, 2);
    }
    delta::ReadSnapshot()->Seek(at + 2);
    position->z = Dequantize(q, kReadScaleZ, kReadOffsetZ);
}
EI_REWRITTEN(0x0047E930, DeltaField::ReadPosition);

void DeltaField::ReadPosition2D(float* position) {
    position[0] = Dequantize(ReadWord(), kReadScale2D, kReadOffset2D);
    position[1] = Dequantize(ReadWord(), kReadScale2D, kReadOffset2D);
}
EI_REWRITTEN(0x0047EE00, DeltaField::ReadPosition2D);

// Ranged values.
namespace {
constexpr float kRangeSteps = 65535.0f;                          // 0073CAA8
const float kRangeStep = std::bit_cast<float>(0x37800080u);      // 0073CAAC (1 / 65535)
} // namespace

void DeltaField::WriteRanged(float value, float min, float max) {
    int q = x87::fistp(static_cast<float>((value - min) * (kRangeSteps / (max - min))));
    WriteDelta(&q, 2);
}
EI_REWRITTEN(0x0047EF80, DeltaField::WriteRanged);

void DeltaField::ReadRanged(float* value, float min, float max) {
    float step = static_cast<float>((max - min) * kRangeStep);
    std::uint16_t q = ReadWord();
    *value = static_cast<float>(static_cast<int>(q)) * step + min;
}
EI_REWRITTEN(0x0047F110, DeltaField::ReadRanged);

// --- 16-bit floats -----------------------------------------------------------------------------

namespace delta {

// Sign; exponent - 0x40 in 7 bits (0 when the exponent is below 0x40); the top 8 mantissa bits
// rounded (a carry out of them is dropped, not added to the exponent).
std::uint16_t PackFloat16(std::uint32_t u) {
    auto h = static_cast<std::uint16_t>(((u >> 31) << 15) | (u & 0x7FFF));
    if ((u & 0x7F800000) < 0x20000000) {
        h &= 0x80FF;
    } else {
        std::uint32_t exponent = ((u >> 23) & 0xFF) << 8;
        h ^= static_cast<std::uint16_t>(((exponent - 0x3F01) ^ h) & 0x7F00);
    }
    std::uint32_t mantissa = ((u & 0x7FFFFF) + 0x4000) >> 15;
    h ^= static_cast<std::uint16_t>((mantissa ^ (h & 0xFF)) & 0xFF);
    return h;
}

std::uint32_t UnpackFloat16(std::uint16_t q) {
    std::uint32_t bits = q;
    std::uint32_t a = ((bits & 0xFFFF8000u) << 16) | (bits & 0x7FFFFFFFu);
    if (bits & 0x7F00) {
        std::uint32_t exponent = (((bits >> 8) & 0x7F) + 0x40) << 23;
        a ^= (exponent ^ a) & 0x7F800000;
    } else {
        a &= 0x807FFFFF;
    }
    return ((bits & 0xFF) << 15) | (a & 0xFF800000);
}

} // namespace delta

namespace {
void StoreBits(float* out, std::uint32_t bits) { std::memcpy(out, &bits, sizeof(bits)); }
} // namespace

void DeltaField::WriteFloat16(std::uint32_t floatBits) {
    std::uint16_t h = delta::PackFloat16(floatBits);
    WriteDelta(&h, 2);
}
EI_REWRITTEN(0x0047F1E0, DeltaField::WriteFloat16);

// The result is never a NaN (the exponent is at most 0xBF), so its FLD/FSTP store is a copy.
void DeltaField::ReadFloat16(float* value) { StoreBits(value, delta::UnpackFloat16(ReadWord())); }
EI_REWRITTEN(0x0047F3D0, DeltaField::ReadFloat16);

void DeltaField::WriteFloat16x3(const float* values) {
    for (int i = 0; i < 3; ++i) {
        std::uint16_t h = delta::PackFloat16(std::bit_cast<std::uint32_t>(values[i]));
        WriteDelta(&h, 2);
    }
}
EI_REWRITTEN(0x0047F4D0, DeltaField::WriteFloat16x3);

void DeltaField::ReadFloat16x3(float* values) {
    for (int i = 0; i < 3; ++i) StoreBits(&values[i], delta::UnpackFloat16(ReadWord()));
}
EI_REWRITTEN(0x0047F7F0, DeltaField::ReadFloat16x3);

// --- DeltaState --------------------------------------------------------------------------------

void DeltaState::Reset() {
    for (int i = 0; i < fieldCount_; ++i) {
        DeltaField& field = fields_[i];
        field.snapshot[0].Resize(0);
        field.snapshot[1].Resize(0);
        field.out[0].Resize(0);
        field.out[1].Resize(0);
    }
    sentMask_ = 0;
    lastMask_ = 0;
    mask_ = 0;
    reader_ = nullptr;
    mode_ = kIdle;
    header_[0].Resize(0);
    header_[1].Resize(0);
}
EI_REWRITTEN(0x0047FB70, DeltaState::Reset);

DeltaField* DeltaState::BeginField(int index) {
    if (!((1u << (index & 31)) & mask_)) return nullptr;
    DeltaField* field = &fields_[index];
    switch (mode_) {
    case kWriteFull:
        RewindAndEmpty(field);
        delta::KeepSnapshot0() = 1;
        delta::KeepSnapshot1() = 1;
        break;
    case kWriteDelta:
        RewindAndEmpty(field);
        delta::KeepSnapshot0() = 0;
        delta::KeepSnapshot1() = 1;
        break;
    case kRead0:
    case kRead1:
        StartRead(field, mode_ == kRead1, reader_);
        break;
    default:
        break;
    }
    return &fields_[index];
}
EI_REWRITTEN(0x0047FBF0, DeltaState::BeginField);

void DeltaState::SetWriteMode(int deltaOnly) {
    if (!deltaOnly) {
        mask_ = 0xFFFF;
        mode_ = kWriteFull;
    } else {
        mode_ = kWriteDelta;
    }
}
EI_REWRITTEN(0x0047FD50, DeltaState::SetWriteMode);

int DeltaState::BeginRead(int second, BitStream* reader) {
    reader_ = reader;
    reader->Read(&mask_, 2);
    mode_ = second ? kRead1 : kRead0;
    return second;
}
EI_REWRITTEN(0x0047FD80, DeltaState::BeginRead);

namespace {
// One channel's header: the mask, then the channel's output of every field in it.
void RebuildHeader(DeltaState* state, int channel, std::uint16_t mask) {
    BitStream& header = state->header_[channel];
    header.Resize(0);
    header.Write(&mask, 2);
    for (int i = 0; i < state->fieldCount_; ++i) {
        if (!(mask & (1 << i))) continue;
        BitStream& out = state->fields_[i].out[channel];
        header.EndBits();
        out.Seek(0);
        const std::uint8_t* data = out.data_;
        header.Write(data, out.Size());
    }
}
} // namespace

void DeltaState::Finish() {
    if (mode_ == kWriteDelta) {
        std::uint16_t written = mask_;
        sentMask_ |= written;
        lastMask_ = written;
        mask_ = 0;
        RebuildHeader(this, 0, sentMask_);
        RebuildHeader(this, 1, lastMask_);
    }
    if (mode_ == kWriteFull) {
        header_[0].Resize(0);
        header_[1].Resize(0);
        sentMask_ = 0;
        lastMask_ = 0;
        mask_ = 0;
    }
    mode_ = kIdle;
    reader_ = nullptr;
}
EI_REWRITTEN(0x0047FE90, DeltaState::Finish);
