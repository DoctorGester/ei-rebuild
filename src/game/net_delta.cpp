// Delta replication of object state. See net_delta.h.
#include "net_delta.h"

#include "lib/bytes.h"
#include "lib/rounding.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <string>

namespace {

std::uint32_t Word(const std::uint8_t* p) {
    std::uint32_t w;
    lib::LoadBytes(w, p);
    return w;
}

// All of `data`: a 0 bit, then a 0 bit before every 4-byte word, then the remaining bytes.
void WriteRaw(const std::uint8_t* data, std::uint32_t size, MemoryStream* out) {
    const std::uint32_t words = size / 4;
    out->PutBit(false);
    for (std::uint32_t i = 0; i < words; ++i) {
        out->PutBit(false);
        out->Write(data + i * 4, 4);
    }
    out->Write(data + words * 4, size - words * 4);
}

// The bits and changed words of `data` against `old`.
void WriteDiff(const std::uint8_t* old, const std::uint8_t* data, std::uint32_t size, MemoryStream* out) {
    if (std::memcmp(old, data, size) == 0) {
        out->PutBit(true);
        return;
    }
    const std::uint32_t words = size / 4;
    out->PutBit(false);
    for (std::uint32_t i = 0; i < words; ++i) {
        const std::uint8_t* word = data + i * 4;
        if (Word(old + i * 4) == Word(word)) {
            out->PutBit(true);
        } else {
            out->PutBit(false);
            out->Write(word, 4);
        }
    }
    out->Write(data + words * 4, size - words * 4);
}

// WriteDelta for one channel.
void WriteChannel(MemoryStream* snapshot, const void* data, std::uint32_t size, bool keepSnapshot, MemoryStream* out) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    const std::uint32_t at = snapshot->Position();
    if (snapshot->Size() < at + size) {
        WriteRaw(bytes, size, out);
        snapshot->Write(data, size);
        return;
    }
    std::uint8_t* current = snapshot->data.data() + at;
    WriteDiff(current, bytes, size, out);
    if (keepSnapshot) std::memcpy(current, data, size);
    snapshot->Seek(at + size);
}

// Reads what WriteDiff wrote: `data` = `old` when nothing changed (returns false), otherwise the changed words
// from `received` (returns true).
bool ReadDiff(const std::uint8_t* old, void* data, std::uint32_t size, MemoryStream* received) {
    if (received->GetBit()) {
        std::memcpy(data, old, size);
        return false;
    }
    auto* bytes = static_cast<std::uint8_t*>(data);
    const std::uint32_t words = size / 4;
    for (std::uint32_t i = 0; i < words; ++i) {
        std::uint8_t* word = bytes + i * 4;
        if (received->GetBit()) std::memcpy(word, old + i * 4, 4);
        else received->Read(word, 4);
    }
    received->Read(bytes + words * 4, size - words * 4);
    return true;
}

constexpr float kWriteOffsetXY = -16.0f;
constexpr float kWriteScaleXY = 120.46875f;
constexpr float kWriteOffsetZ = -32.0f;
constexpr float kWriteScaleZ = 341.328125f;
constexpr float kReadScaleXY = 0.008300908f;
constexpr float kReadOffsetXY = 16.0f;
constexpr float kReadScaleZ = 0.0029297322f;
constexpr float kReadOffsetZ = 32.0f;
constexpr float kWriteOffset2D = -32.0f;
constexpr float kWriteScale2D = 60.234375f;
constexpr float kReadScale2D = 0.016601816f;
constexpr float kReadOffset2D = 32.0f;

int Quantize(float v, float offset, float scale) { return lib::RoundEven((v - offset) * scale); }

float Dequantize(std::uint16_t q, float scale, float offset) { return static_cast<float>(static_cast<int>(q)) * scale - offset; }

constexpr float kRangeSteps = 65535.0f;
constexpr float kRangeStep = 0.000015259022f;      // (1 / 65535)

// The 16-bit float of WriteFloat16.
std::uint16_t PackFloat16(float value) {
    const auto bits = std::bit_cast<std::uint32_t>(value);
    const std::uint32_t sign = bits >> 31;
    const std::uint32_t exponent = (bits >> 23) & 0xFF;
    const std::uint32_t packedExponent = exponent < 0x40 ? 0 : (exponent - 0x40) & 0x7F;
    const std::uint32_t mantissa = (((bits & 0x7FFFFF) + 0x4000) >> 15) & 0xFF;
    return static_cast<std::uint16_t>(sign << 15 | packedExponent << 8 | mantissa);
}

// The exponent is at most 0xBF, so the result is never a NaN.
float UnpackFloat16(std::uint16_t packed) {
    const std::uint32_t sign = packed >> 15;
    const std::uint32_t exponent = (packed >> 8) & 0x7F;
    const std::uint32_t mantissa = packed & 0xFF;
    const std::uint32_t biased = exponent ? exponent + 0x40 : 0;
    return std::bit_cast<float>(sign << 31 | biased << 23 | mantissa << 15);
}

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
        header.Write(out.data.data(), out.Size());
    }
}

} // namespace

void DeltaField::Empty() {
    snapshot[0].Resize(0);
    snapshot[1].Resize(0);
    out[0].Resize(0);
    out[1].Resize(0);
}

// --- Writing -----------------------------------------------------------------------------------

DeltaWriter::DeltaWriter(DeltaField& target, DeltaWrite kind) : field(&target), keepSnapshot0(kind == DeltaWrite::Full) {
    target.snapshot[0].Seek(0);
    target.snapshot[1].Seek(0);
    target.out[0].Resize(0);
    target.out[1].Resize(0);
}

void DeltaWriter::WriteDelta(const void* data, std::uint32_t size) {
    WriteChannel(&field->snapshot[0], data, size, keepSnapshot0, &field->out[0]);
    WriteChannel(&field->snapshot[1], data, size, true, &field->out[1]);
}

void DeltaWriter::WriteBoth(const void* data, std::uint32_t size) {
    field->out[0].Write(data, size);
    field->out[1].Write(data, size);
}

void DeltaWriter::WriteStringBoth(std::string_view text) {
    field->out[0].WriteString(text);
    field->out[1].WriteString(text);
}

void DeltaWriter::WriteStreamBoth(MemoryStream* from) {
    field->out[0].WriteStream(from);
    field->out[1].WriteStream(from);
}

void DeltaWriter::WritePosition(const Vector3& position) {
    int q = Quantize(position.x, kWriteOffsetXY, kWriteScaleXY);
    WriteDelta(&q, 2);
    q = Quantize(position.y, kWriteOffsetXY, kWriteScaleXY);
    WriteDelta(&q, 2);
    q = Quantize(position.z, kWriteOffsetZ, kWriteScaleZ);
    WriteDelta(&q, 2);
}

void DeltaWriter::WritePosition2D(Vector2 position) {
    int q = Quantize(position.x, kWriteOffset2D, kWriteScale2D);
    WriteDelta(&q, 2);
    q = Quantize(position.y, kWriteOffset2D, kWriteScale2D);
    WriteDelta(&q, 2);
}

void DeltaWriter::WriteRanged(float value, float min, float max) {
    int q = lib::RoundEven((value - min) * (kRangeSteps / (max - min)));
    WriteDelta(&q, 2);
}

void DeltaWriter::WriteFloat16(float value) {
    std::uint16_t packed = PackFloat16(value);
    WriteDelta(&packed, 2);
}

void DeltaWriter::WriteFloat16x3(const Vector3& values) {
    WriteFloat16(values.x);
    WriteFloat16(values.y);
    WriteFloat16(values.z);
}

void DeltaWriter::End() {
    field->out[0].EndBits();
    field->out[1].EndBits();
    if (keepSnapshot0) field->snapshot[0].Resize(field->snapshot[0].Position());
    field->snapshot[1].Resize(field->snapshot[1].Position());
}

// --- Reading -----------------------------------------------------------------------------------

DeltaReader::DeltaReader(DeltaField& target, int channel, MemoryStream* from)
    : field(&target), snapshot(&target.snapshot[channel ? 1 : 0]), received(from) {
    target.snapshot[0].Seek(0);
    target.snapshot[1].Seek(0);
}

void DeltaReader::ReadDelta(void* data, std::uint32_t size) {
    const std::uint32_t at = snapshot->Position();
    if (ReadDiff(snapshot->data.data() + at, data, size, received)) {
        field->snapshot[1].Seek(at);
        field->snapshot[1].Write(data, size);
    }
    snapshot->Seek(at + size);
}

void DeltaReader::ReadRaw(void* data, std::uint32_t size) { received->Read(data, size); }

std::string DeltaReader::ReadString() { return received->ReadString(); }

void DeltaReader::ReadText(char* text, int size) {
    std::string string = ReadString();
    if (static_cast<int>(string.size()) < size - 1) std::memcpy(text, string.c_str(), string.size() + 1);
    else text[0] = '\0';
}

int DeltaReader::ReadStream(MemoryStream* to) { return received->ReadStream(to); }

std::uint16_t DeltaReader::ReadWord() {
    std::uint16_t q;
    ReadDelta(&q, 2);
    return q;
}

Vector3 DeltaReader::ReadPosition() {
    Vector3 position;
    position.x = Dequantize(ReadWord(), kReadScaleXY, kReadOffsetXY);
    position.y = Dequantize(ReadWord(), kReadScaleXY, kReadOffsetXY);
    position.z = Dequantize(ReadWord(), kReadScaleZ, kReadOffsetZ);
    return position;
}

Vector2 DeltaReader::ReadPosition2D() {
    Vector2 position;
    position.x = Dequantize(ReadWord(), kReadScale2D, kReadOffset2D);
    position.y = Dequantize(ReadWord(), kReadScale2D, kReadOffset2D);
    return position;
}

float DeltaReader::ReadRanged(float min, float max) {
    float step = (max - min) * kRangeStep;
    std::uint16_t q = ReadWord();
    return static_cast<float>(static_cast<int>(q)) * step + min;
}

float DeltaReader::ReadFloat16() { return UnpackFloat16(ReadWord()); }

Vector3 DeltaReader::ReadFloat16x3() {
    Vector3 values;
    values.x = ReadFloat16();
    values.y = ReadFloat16();
    values.z = ReadFloat16();
    return values;
}

bool DeltaReader::SnapshotConsumed() const { return snapshot->Position() == snapshot->Size(); }

void DeltaReader::End() { received->EndBits(); }

// --- The state ---------------------------------------------------------------------------------

void DeltaState::Reset() {
    for (DeltaField& field : fields) field.Empty();
    sentMask = 0;
    lastMask = 0;
    mask = 0;
    reader = nullptr;
    mode = Mode::Idle;
    header[0].Resize(0);
    header[1].Resize(0);
}

void DeltaState::SetFieldCount(int count) { fields.resize(count); }

void DeltaState::SetWriteMode(DeltaWrite kind) {
    if (kind == DeltaWrite::Full) {
        mask = 0xFFFF;
        mode = Mode::WriteFull;
    } else {
        mode = Mode::WriteChanges;
    }
}

void DeltaState::BeginRead(int channel, MemoryStream* newReader) {
    reader = newReader;
    newReader->ReadValue(mask);
    mode = channel ? Mode::Read1 : Mode::Read0;
}

std::optional<DeltaWriter> DeltaState::WriteField(NetField index) {
    const int i = static_cast<int>(index);
    if (!((1u << (i & 31)) & mask)) return std::nullopt;
    return DeltaWriter(fields[i], mode == Mode::WriteFull ? DeltaWrite::Full : DeltaWrite::Changes);
}

std::optional<DeltaReader> DeltaState::ReadField(NetField index) {
    const int i = static_cast<int>(index);
    if (!((1u << (i & 31)) & mask)) return std::nullopt;
    return DeltaReader(fields[i], mode == Mode::Read1 ? 1 : 0, reader);
}

void DeltaState::Finish() {
    if (mode == Mode::WriteChanges) {
        std::uint16_t written = mask;
        sentMask |= written;
        lastMask = written;
        mask = 0;
        RebuildHeader(this, 0, sentMask);
        RebuildHeader(this, 1, lastMask);
    }
    if (mode == Mode::WriteFull) {
        header[0].Resize(0);
        header[1].Resize(0);
        sentMask = 0;
        lastMask = 0;
        mask = 0;
    }
    mode = Mode::Idle;
    reader = nullptr;
}
