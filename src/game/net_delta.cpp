// Delta replication of object state. See net_delta.h.
#include "net_delta.h"

#include "core_maths.h"
#include "net_coder.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include "lib/bytes.h"
#include "lib/rounding.h"

#include <bit>
#include <memory>


namespace {

std::uint32_t Word(const std::uint8_t* p) {
    std::uint32_t w;
    lib::LoadBytes(w, p);
    return w;
}

// ReadStream refuses longer streams.
constexpr std::uint32_t kMaxStreamLength = 100000;

// The number of bytes in a buffer, its high-water mark raised to the cursor first (inlined).
std::uint32_t BufferLength(GameBuffer& buffer) {
    if (buffer.cur > buffer.high) buffer.high = buffer.cur;
    return static_cast<std::uint32_t>(buffer.PositionOf(buffer.high));
}

// A fixed view over `self`'s bytes run through the coder into a growing buffer, whose bytes then replace
// `out`'s (Resize, Seek 0, Write, Seek 0). The view is destroyed (its base vtable) before the buffer.
void CodeStream(MemoryStream* self, CoderState* coder, MemoryStream* out, void (CoderState::*code)(GameBuffer*, GameBuffer*)) {
    GrowingBuffer result;
    std::uint8_t* data = self->data.data();
    std::uint8_t* last = data + self->Size();
    BufferView view(data, last);
    (coder->*code)(&view, &result);
    out->Resize(BufferLength(result));
    out->Seek(0);
    std::uint32_t length = BufferLength(result);
    out->Write(result.begin, length);
    out->Seek(0);
}

void RewindAndEmpty(DeltaField* field) {
    field->snapshot[0].Seek(0);
    field->snapshot[1].Seek(0);
    field->out[0].Resize(0);
    field->out[1].Resize(0);
}

void SetKeep(int channel1Only) {
    if (!channel1Only) {
        delta::g_keepSnapshot0 = 1;
        delta::g_keepSnapshot1 = 1;
    } else {
        delta::g_keepSnapshot0 = 0;
        delta::g_keepSnapshot1 = 1;
    }
}

void StartRead(DeltaField* field, int second, MemoryStream* reader) {
    field->snapshot[0].Seek(0);
    field->snapshot[1].Seek(0);
    delta::g_readSnapshot = second ? &field->snapshot[1] : &field->snapshot[0];
    delta::g_received = reader;
}

constexpr float kWriteOffsetXY = -16.0f;
constexpr float kWriteScaleXY = 120.46875f;
constexpr float kWriteOffsetZ = -32.0f;
constexpr float kWriteScaleZ = 341.328125f;
constexpr float kReadScaleXY = 0.008300908f;
static_assert(std::bit_cast<std::uint32_t>(kReadScaleXY) == 0x3C080088u);
constexpr float kReadOffsetXY = 16.0f;
constexpr float kReadScaleZ = 0.0029297322f;
static_assert(std::bit_cast<std::uint32_t>(kReadScaleZ) == 0x3B4000C0u);
constexpr float kReadOffsetZ = 32.0f;
constexpr float kWriteOffset2D = -32.0f;
constexpr float kWriteScale2D = 60.234375f;
constexpr float kReadScale2D = 0.016601816f;
static_assert(std::bit_cast<std::uint32_t>(kReadScale2D) == 0x3C880088u);
constexpr float kReadOffset2D = 32.0f;

int Quantize(float v, float offset, float scale) { return lib::RoundEven((v - offset) * scale); }

float Dequantize(std::uint16_t q, float scale, float offset) { return static_cast<float>(static_cast<int>(q)) * scale - offset; }

constexpr float kRangeSteps = 65535.0f;
constexpr float kRangeStep = 0.000015259022f;      // (1 / 65535)
static_assert(std::bit_cast<std::uint32_t>(kRangeStep) == 0x37800080u);

void StoreBits(float* out, std::uint32_t bits) { *out = std::bit_cast<float>(bits); }

// One channel's header: the mask, then the channel's output of every field in it.
void RebuildHeader(DeltaState* state, int channel, std::uint16_t mask) {
    MemoryStream& header = state->header[channel];
    header.Resize(0);
    header.WriteValue(mask);
    for (int i = 0; i < std::ssize(state->fields); ++i) {
        if (!(mask & (1 << i))) continue;
        MemoryStream& out = state->fields[i].out[channel];
        header.EndBits();
        out.Seek(0);
        const std::uint8_t* data = out.data.data();
        header.Write(data, out.Size());
    }
}

} // namespace

void MemoryStream::PutBit(bool bit) {
    bitMask = static_cast<std::int32_t>(static_cast<std::uint32_t>(bitMask) << 1);
    if (bitMask > 0x80) StartByte();    // sets bitMask to 1
    if (bit) bits |= static_cast<std::uint8_t>(bitMask);
}

bool MemoryStream::GetBit() {
    bitMask = static_cast<std::int32_t>(static_cast<std::uint32_t>(bitMask) << 1);
    if (bitMask > 0x80) {
        bitMask = 1;
        ReadValue(bits);
    }
    return (bitMask & static_cast<std::int8_t>(bits)) != 0;
}

// --- Whole streams: length-prefixed copies and the message compression ------------------------

void MemoryStream::WriteStream(MemoryStream* from) {
    from->EndBits();
    std::int32_t header = static_cast<std::int32_t>(from->Size() << 1);
    if (header < 0x100) {
        Write(&header, 1);
    } else {
        header |= 1;
        WriteValue(header);
    }
    std::uint8_t* newData = from->data.data();
    Write(newData, from->Size());
}

int MemoryStream::ReadStream(MemoryStream* to) {
    std::uint32_t length = 0;
    Read(&length, 1);
    if (length & 1) Read(reinterpret_cast<std::uint8_t*>(&length) + 1, 3);
    length >>= 1;
    if (length > kMaxStreamLength) return 0;
    to->SetSize(length);
    if (Read(to->data.data(), length) != length) return 0;
    to->Seek(0);
    to->ok = 1;
    return 1;
}

void MemoryStream::CompressTo(CoderState* coder, MemoryStream* out) { CodeStream(this, coder, out, &CoderState::Compress); }

void MemoryStream::DecompressTo(CoderState* coder, MemoryStream* out) { CodeStream(this, coder, out, &CoderState::Decompress); }

// --- Channel writers ---------------------------------------------------------------------------

namespace delta {

void WriteRaw(const std::uint8_t* data, std::uint32_t size, MemoryStream* out) {
    const std::uint8_t* p = data;
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

void WriteDiff(const std::uint8_t* old, const std::uint8_t* data, std::uint32_t size, MemoryStream* out) {
    if (std::memcmp(old, data, size) == 0) {
        out->PutBit(true);
        return;
    }
    out->PutBit(false);
    const std::uint8_t* p = data;
    std::uint32_t rest = size;
    if (static_cast<int>(size) > 3) {
        std::ptrdiff_t toOld = old - p;
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

// Without a snapshot to compare with (it is shorter than position + size) everything is sent and
// appended to the snapshot. Otherwise a 1 bit means "all unchanged"; else a 0 bit and, per 4-byte
// word, 1 (unchanged) or 0 and the word, then the remaining bytes as they are. With
// `keepSnapshot` the snapshot is updated in place; it is advanced past the bytes either way.
void WriteDeltaChannel(MemoryStream* snapshot, const void* data, std::uint32_t size, int keepSnapshot,
                                  MemoryStream* out) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    std::uint32_t end = snapshot->Position() + size;
    if (snapshot->Size() < end) {
        WriteRaw(bytes, size, out);
        snapshot->Write(data, size);
        return;
    }
    std::uint8_t* buffer = snapshot->data.data();
    std::uint8_t* current = buffer + snapshot->Position();
    WriteDiff(current, bytes, size, out);
    if (keepSnapshot) std::memcpy(current, data, size);
    snapshot->Seek(snapshot->Position() + size);
}

} // namespace delta


DeltaField* DeltaField::BeginWriteB(int channel1Only) {
    RewindAndEmpty(this);
    SetKeep(channel1Only);
    return this;
}

DeltaField* DeltaField::BeginReadB(int second, MemoryStream* reader) {
    StartRead(this, second, reader);
    return this;
}

void DeltaField::EndField() {
    if (delta::g_readSnapshot) {
        delta::g_received->EndBits();
        delta::g_readSnapshot = nullptr;
        delta::g_received = nullptr;
        return;
    }
    out[0].EndBits();
    out[1].EndBits();
    if (delta::g_keepSnapshot0) snapshot[0].Resize(snapshot[0].Position());
    if (delta::g_keepSnapshot1) snapshot[1].Resize(snapshot[1].Position());
}

void DeltaField::WriteDelta(const void* data, std::uint32_t size) {
    delta::WriteDeltaChannel(&snapshot[0], data, size, delta::g_keepSnapshot0, &out[0]);
    delta::WriteDeltaChannel(&snapshot[1], data, size, delta::g_keepSnapshot1, &out[1]);
}

// The bytes come from the snapshot read against (all unchanged, or per unchanged word) or from the
// reader. When anything changed the result is also written to snapshot[1] at the same place.
void DeltaField::ReadDelta(void* data, std::uint32_t size) {
    std::uint32_t position = delta::g_readSnapshot->Position();
    MemoryStream* reader = delta::g_received;
    const std::uint8_t* current = delta::g_readSnapshot->data.data() + position;
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
    delta::g_readSnapshot->Seek(position + size);
}

void DeltaField::WriteBoth(const void* data, std::uint32_t size) {
    out[0].Write(data, size);
    out[1].Write(data, size);
}

void DeltaField::WriteStringBoth(std::string_view text) {
    out[0].WriteString(text);
    out[1].WriteString(text);
}

std::uint32_t DeltaField::ReadRaw(void* data, std::uint32_t size) { return delta::g_received->Read(data, size); }

std::string DeltaField::ReadString() {
    return delta::g_received->ReadString();
}

void DeltaField::WriteStreamBoth(MemoryStream* from) {
    out[0].WriteStream(from);
    out[1].WriteStream(from);
}

void DeltaField::ReadText(char* text, int size) {
    std::string string = ReadString();
    if (static_cast<int>(string.size()) < size - 1) {
        const char* from = string.c_str();
        std::memcpy(text, from, std::strlen(from) + 1);
    } else {
        text[0] = '\0';
    }
}

int DeltaField::ReadStream(MemoryStream* to) { return delta::g_received->ReadStream(to); }

namespace delta {

bool ReadDiff(const std::uint8_t* old, void* data, std::uint32_t size, MemoryStream* reader) {
    if (reader->GetBit()) {
        std::memcpy(data, old, size);
        return false;
    }
    auto* p = static_cast<std::uint8_t*>(data);
    std::uint32_t rest = size;
    if (static_cast<int>(size) > 3) {
        std::ptrdiff_t toOld = old - p;
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

} // namespace delta


void DeltaField::WritePosition(const Vector3& position) {
    int q = Quantize(position.x, kWriteOffsetXY, kWriteScaleXY);
    WriteDelta(&q, 2);
    q = Quantize(position.y, kWriteOffsetXY, kWriteScaleXY);
    WriteDelta(&q, 2);
    q = Quantize(position.z, kWriteOffsetZ, kWriteScaleZ);
    WriteDelta(&q, 2);
}

void DeltaField::WritePosition2D(const float* position) {
    int q = Quantize(position[0], kWriteOffset2D, kWriteScale2D);
    WriteDelta(&q, 2);
    q = Quantize(position[1], kWriteOffset2D, kWriteScale2D);
    WriteDelta(&q, 2);
}

// One 16-bit value with the bit read inline (unchanged: from the snapshot read against).
std::uint16_t DeltaField::ReadWord() {
    std::uint32_t at = delta::g_readSnapshot->Position();
    MemoryStream* reader = delta::g_received;
    const std::uint8_t* current = delta::g_readSnapshot->data.data() + at;
    std::uint16_t q;
    if (reader->GetBit()) {
        std::memcpy(&q, current, 2);
    } else {
        reader->ReadValue(q);
        snapshot[1].Seek(at);
        snapshot[1].WriteValue(q);
    }
    delta::g_readSnapshot->Seek(at + 2);
    return q;
}

void DeltaField::ReadPosition(Vector3* position) {
    position->x = Dequantize(ReadWord(), kReadScaleXY, kReadOffsetXY);
    position->y = Dequantize(ReadWord(), kReadScaleXY, kReadOffsetXY);
    // z goes through ReadDiff.
    std::uint32_t at = delta::g_readSnapshot->Position();
    MemoryStream* reader = delta::g_received;
    std::uint16_t q;
    if (delta::ReadDiff(delta::g_readSnapshot->data.data() + at, &q, 2, reader)) {
        snapshot[1].Seek(at);
        snapshot[1].WriteValue(q);
    }
    delta::g_readSnapshot->Seek(at + 2);
    position->z = Dequantize(q, kReadScaleZ, kReadOffsetZ);
}

void DeltaField::ReadPosition2D(float* position) {
    position[0] = Dequantize(ReadWord(), kReadScale2D, kReadOffset2D);
    position[1] = Dequantize(ReadWord(), kReadScale2D, kReadOffset2D);
}

// Ranged values.

void DeltaField::WriteRanged(float value, float min, float max) {
    int q = lib::RoundEven((value - min) * (kRangeSteps / (max - min)));
    WriteDelta(&q, 2);
}

void DeltaField::ReadRanged(float* value, float min, float max) {
    float step = (max - min) * kRangeStep;
    std::uint16_t q = ReadWord();
    *value = static_cast<float>(static_cast<int>(q)) * step + min;
}

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

void DeltaField::WriteFloat16(std::uint32_t floatBits) {
    std::uint16_t h = delta::PackFloat16(floatBits);
    WriteDelta(&h, 2);
}

// The result is never a NaN (the exponent is at most 0xBF), so storing it is a plain copy.
void DeltaField::ReadFloat16(float* value) { StoreBits(value, delta::UnpackFloat16(ReadWord())); }

void DeltaField::WriteFloat16x3(const float* values) {
    for (int i = 0; i < 3; ++i) {
        std::uint16_t h = delta::PackFloat16(std::bit_cast<std::uint32_t>(values[i]));
        WriteDelta(&h, 2);
    }
}

void DeltaField::ReadFloat16x3(float* values) {
    for (int i = 0; i < 3; ++i) StoreBits(&values[i], delta::UnpackFloat16(ReadWord()));
}

void DeltaState::Reset() {
    for (DeltaField& field : fields) {
        field.snapshot[0].Resize(0);
        field.snapshot[1].Resize(0);
        field.out[0].Resize(0);
        field.out[1].Resize(0);
    }
    sentMask = 0;
    lastMask = 0;
    mask = 0;
    reader = nullptr;
    mode = kIdle;
    header[0].Resize(0);
    header[1].Resize(0);
}

DeltaField* DeltaState::BeginField(int index) {
    if (!((1u << (index & 31)) & mask)) return nullptr;
    DeltaField* field = &fields[index];
    switch (mode) {
    case kWriteFull:
        RewindAndEmpty(field);
        delta::g_keepSnapshot0 = 1;
        delta::g_keepSnapshot1 = 1;
        break;
    case kWriteDelta:
        RewindAndEmpty(field);
        delta::g_keepSnapshot0 = 0;
        delta::g_keepSnapshot1 = 1;
        break;
    case kRead0:
    case kRead1:
        StartRead(field, mode == kRead1, reader);
        break;
    default:
        break;
    }
    return &fields[index];
}

void DeltaState::SetWriteMode(int deltaOnly) {
    if (!deltaOnly) {
        mask = 0xFFFF;
        mode = kWriteFull;
    } else {
        mode = kWriteDelta;
    }
}

int DeltaState::BeginRead(int second, MemoryStream* newReader) {
    reader = newReader;
    newReader->ReadValue(mask);
    mode = second ? kRead1 : kRead0;
    return second;
}

void DeltaState::Finish() {
    if (mode == kWriteDelta) {
        std::uint16_t written = mask;
        sentMask |= written;
        lastMask = written;
        mask = 0;
        RebuildHeader(this, 0, sentMask);
        RebuildHeader(this, 1, lastMask);
    }
    if (mode == kWriteFull) {
        header[0].Resize(0);
        header[1].Resize(0);
        sentMask = 0;
        lastMask = 0;
        mask = 0;
    }
    mode = kIdle;
    reader = nullptr;
}

// --- The state's lifetime --------------------------------------------------------------------------

DeltaState::DeltaState() { Reset(); }

DeltaState::~DeltaState() = default;

void DeltaState::SetFieldCount(int count) { fields.resize(count); }
