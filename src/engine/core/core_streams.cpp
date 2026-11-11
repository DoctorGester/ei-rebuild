// The byte streams (core_streams.h): the base class's string, tagged-block, sized-block, header and difference
// coding, and the memory stream's reads, writes, sizes and bit helpers.
#include "engine/core/core_streams.h"

#include "engine/core/asset_text.h"
#include "lib/caller.h"
#include "lib/containers.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace {

// ReadStream refuses longer streams.
constexpr std::uint32_t kMaxStreamLength = 100000;

// A fixed view over `self`'s bytes run through the coder into a growing buffer, whose bytes then replace
// `out`'s (Resize, Seek 0, Write, Seek 0).
void CodeStream(MemoryStream* self, CoderState* coder, MemoryStream* out, bool compress) {
    GrowingBuffer result;
    std::uint8_t* data = self->data.data();
    std::uint8_t* last = data + self->Size();
    BufferView view(data, last);
    if (compress) coder->Compress(&view, &result);
    else coder->Decompress(&view, &result);
    out->Resize(static_cast<std::uint32_t>(result.Size()));
    out->Seek(0);
    std::uint32_t length = static_cast<std::uint32_t>(result.Size());
    out->Write(result.begin, length);
    out->Seek(0);
}

} // namespace

void (*g_memoryStreamWriteObserver)(MemoryStream* stream, std::uint32_t size, void* const* frame) = nullptr;

std::string ByteStream::ReadString() {
    std::string text;
    unsigned char byte;
    while (Read(&byte, 1) && byte) text.push_back(static_cast<char>(byte));
    text::FromAssetText(&text);
    return text;
}

void ByteStream::WriteString(std::string_view text) {
    const std::string bytes = text::ToAssetText(text.substr(0, text.find('\0')));
    Write(bytes.data(), static_cast<std::uint32_t>(bytes.size() + 1));   // data() ends in a zero byte
}

MemoryStream::MemoryStream() = default;

MemoryStream::~MemoryStream() = default;

MemoryStream::MemoryStream(MemoryStream&& other) noexcept
    : bits(other.bits), bitMask(other.bitMask), bitsIndex(other.bitsIndex),
      data(std::exchange(other.data, {})),
      position(std::exchange(other.position, 0)), size(std::exchange(other.size, 0)) {
    ok = other.ok;
    version = other.version;
}

MemoryStream& MemoryStream::operator=(MemoryStream&& other) noexcept {
    bits = other.bits;
    bitMask = other.bitMask;
    bitsIndex = other.bitsIndex;
    data = std::exchange(other.data, {});
    position = std::exchange(other.position, 0);
    size = std::exchange(other.size, 0);
    ok = other.ok;
    version = other.version;
    return *this;
}

void MemoryStream::Clear() {
    data = {};
    position = 0;
    size = 0;
    ok = true;
    bitMask = 0x80;
    bitsIndex = -1;
}

bool ByteStream::ReadTagged(std::uint32_t* tag, ByteStream* out) {
    std::uint32_t size = 0;
    if (!Read(tag, 4)) return false;
    if (!Read(&size, 4) || size < 8) return false;   // the size counts the tag and itself
    size -= 8;
    out->Clear();
    std::vector<std::uint8_t> block(size);
    if (Read(block.data(), size) != size) return false;
    out->Write(block.data(), size);
    out->Seek(0);
    return true;
}

bool ByteStream::FindTagged(std::uint32_t id, ByteStream* out) {
    Seek(0);
    std::uint32_t tag;
    while (ReadTagged(&tag, out)) {
        if (tag == id) return true;
    }
    out->Clear();
    return false;
}

void ByteStream::Skip(std::uint32_t size) {
    std::uint8_t dropped[16];
    while (size > 0) {
        const std::uint32_t part = size < sizeof(dropped) ? size : static_cast<std::uint32_t>(sizeof(dropped));
        Read(dropped, part);
        size -= part;
    }
}

void ByteStream::ReadText(char* buffer, int capacity) {
    char* at = buffer;
    // A failed read of a stream that does not zero-fill leaves the old byte.
    char c = 0;
    for (int left = capacity; left > 0;) {
        Read(&c, 1);
        *at++ = c;
        --left;
        if (c == 0) break;
    }
    buffer[capacity - 1] = 0;
    std::string text(buffer);
    text::FromAssetText(&text);
    text::CopyFitting(buffer, static_cast<std::size_t>(capacity), text);
}

namespace {

constexpr int kFullRun = 0x100;

// Writes `length` into the run header at `header`, comes back to the end and writes the next run's
// header (0 for now); returns where that header is.
std::uint32_t CloseRun(ByteStream* out, std::uint32_t header, std::uint8_t length) {
    std::uint32_t end = out->Position();
    out->Seek(header);
    out->WriteValue(length);
    out->Seek(end);
    std::uint32_t next = out->Position();
    out->WriteValue(std::uint8_t{0});
    return next;
}

} // namespace

void ByteStream::WriteDiff(ByteStream* base, ByteStream* current) {
    if (base->Size() != current->Size()) return;
    base->Seek(0);
    current->Seek(0);
    int remaining = static_cast<int>(base->Size());
    bool equalRun = true;
    int count = 0;
    std::uint32_t header = Position();
    WriteValue(std::uint8_t{0});
    for (; remaining > 0; --remaining) {
        std::uint8_t was;
        std::uint8_t now;
        base->ReadValue(was);
        current->ReadValue(now);
        if (was != now) {
            if (!equalRun) {
                if (++count == kFullRun) {
                    header = CloseRun(this, header, 0xFF);
                    header = CloseRun(this, header, 0);   // an empty run of equal bytes
                    count = 1;
                }
            } else {
                header = CloseRun(this, header, static_cast<std::uint8_t>(count));
                equalRun = false;
                count = 1;
            }
            WriteValue(now);
        } else if (equalRun) {
            if (++count == kFullRun) {
                header = CloseRun(this, header, 0xFF);
                header = CloseRun(this, header, 0);       // an empty run of changed bytes
                count = 1;
            }
        } else {
            header = CloseRun(this, header, static_cast<std::uint8_t>(count));
            equalRun = true;
            count = 1;
        }
    }
    CloseRun(this, header, static_cast<std::uint8_t>(count));
}

void ByteStream::ApplyDiff(ByteStream* base, ByteStream* diff) {
    std::uint8_t length = 0;   // a failed read of a stream that does not zero-fill leaves the old byte
    int done = 0;
    const int size = static_cast<int>(base->Size());
    base->Seek(0);
    Seek(0);
    if (size <= 0) return;
    std::uint8_t buffer[0x100];
    do {
        diff->Read(&length, 1);
        base->Read(buffer, length);
        Write(buffer, length);
        done += static_cast<int>(length);
        if (!diff->Read(&length, 1)) return;
        base->Read(buffer, length);
        diff->Read(buffer, length);
        Write(buffer, length);
        done += static_cast<int>(length);
    } while (done < size);
}

namespace {

// Sizes above this are written in four bytes (bit 0 set), others in two.
constexpr std::int32_t kShortSizeLimit = 30000;

} // namespace

std::uint32_t MemoryStream::Read(void* buffer, std::uint32_t byteCount) {
    if (position + byteCount > size) {
        std::uint32_t left = size - position;
        ok = false;
        std::memset(buffer, 0, byteCount);
        if (static_cast<std::int32_t>(left) < 0) return 0;
        byteCount = left;
    }
    std::memcpy(buffer, data.data() + position, byteCount);
    position += byteCount;
    return byteCount;
}

void MemoryStream::Write(const void* buffer, std::uint32_t byteCount) {
    if (g_memoryStreamWriteObserver) g_memoryStreamWriteObserver(this, byteCount, EI_CALLER_FRAME());
    if (position + byteCount > data.size()) data.resize((static_cast<std::uint32_t>(data.size()) + byteCount) * 2);
    std::memcpy(data.data() + position, buffer, byteCount);
    position += byteCount;
    if (!(size > position)) size = position;
}

void MemoryStream::Seek(std::uint32_t newPosition) {
    position = newPosition;
    EndBits();
}

void MemoryStream::CopyFrom(ByteStream* source) {
    source->Seek(0);
    Resize(source->Size());
    source->Read(data.data(), size);
    Seek(0);
}

void MemoryStream::Resize(std::uint32_t byteCount) {
    if (byteCount <= data.size()) {
        size = byteCount;
        position = 0;
        return;
    }
    data.resize((byteCount >> 1) + byteCount + 0x10);
    position = 0;
    if (bitsIndex >= 0) data[bitsIndex] = bits;
    bitsIndex = -1;
    size = byteCount;
    bitMask = 0x80;
}

void MemoryStream::SetSize(std::uint32_t newSize) {
    if (newSize <= data.size()) {
        size = newSize;
        position = 0;
        return;
    }
    data.assign(newSize, 0);
    size = newSize;
    position = 0;
    bitMask = 0x80;
}

void MemoryStream::StartByte() {
    if (bitsIndex >= 0) data[bitsIndex] = bits;
    bitsIndex = static_cast<std::int32_t>(position);
    bits = 0;
    WriteValue(bits);
    bitMask = 1;
}

void MemoryStream::EndBits() {
    if (bitsIndex >= 0) data[bitsIndex] = bits;
    bitsIndex = -1;
    bitMask = 0x80;
}

void ByteStream::CopyFrom(ByteStream* source) {
    std::uint8_t chunk[0x400];
    source->Seek(0);
    for (;;) {
        std::uint32_t read = source->Read(chunk, sizeof(chunk));
        if (!read) break;
        Write(chunk, read);
    }
}

void ByteStream::ReadSized(ByteStream* to) {
    std::uint16_t low = 0;
    Read(&low, 2);
    std::uint32_t size = low;
    if (low & 1) {
        std::uint16_t high = 0;
        Read(&high, 2);
        size |= static_cast<std::uint32_t>(high) << 16;
    }
    size >>= 1;
    to->Clear();
    to->Seek(0);
    if (!size) return;
    std::vector<std::uint8_t> block(size);
    Read(block.data(), size);
    to->Write(block.data(), size);
}

void ByteStream::WriteSized(ByteStream* from) {
    auto size = static_cast<std::int32_t>(from->Size());
    if (size > kShortSizeLimit) {
        WriteValue(static_cast<std::uint32_t>(size) * 2 + 1);
    } else {
        WriteValue(static_cast<std::uint16_t>(static_cast<std::uint32_t>(size) * 2));
    }
    std::vector<std::uint8_t> block(static_cast<std::uint32_t>(size));
    from->Seek(0);
    from->Read(block.data(), static_cast<std::uint32_t>(size));
    Write(block.data(), static_cast<std::uint32_t>(size));
}

bool ByteStream::ReadHeader(std::uint32_t magic, std::int32_t newestVersion, std::int32_t oldestVersion) {
    std::int32_t value = 0;
    ReadValue(value);
    if (static_cast<std::uint32_t>(value) != magic) return false;
    ReadValue(value);
    if (value > newestVersion) return false;
    if (value < oldestVersion) return false;
    version = value;
    return true;
}

void ByteStream::WriteHeader(std::uint32_t magic, std::int32_t headerVersion) {
    WriteValue(magic);
    WriteValue(headerVersion);
    version = headerVersion;
}

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
    std::uint8_t low = 0;
    Read(&low, 1);
    std::uint32_t length = low;
    if (low & 1) {
        std::uint8_t rest[3] = {};
        Read(rest, 3);
        length |= static_cast<std::uint32_t>(rest[0]) << 8 | static_cast<std::uint32_t>(rest[1]) << 16 |
                  static_cast<std::uint32_t>(rest[2]) << 24;
    }
    length >>= 1;
    if (length > kMaxStreamLength) return 0;
    to->SetSize(length);
    if (Read(to->data.data(), length) != length) return 0;
    to->Seek(0);
    to->ok = true;
    return 1;
}

void MemoryStream::CompressTo(CoderState* coder, MemoryStream* out) { CodeStream(this, coder, out, true); }

void MemoryStream::DecompressTo(CoderState* coder, MemoryStream* out) { CodeStream(this, coder, out, false); }

FileStream::FileStream() = default;

FileStream::~FileStream() = default;

std::uint32_t FileStream::Read(void* buffer, std::uint32_t size) {
    std::uint32_t read = file.Read(buffer, size);
    ok = ok && read == size;
    return read;
}

void FileStream::Write(const void* buffer, std::uint32_t size) { file.Write(buffer, size); }

long FileStream::SeekFrom(long position) { return file.Seek(position, platform::SeekFrom::Begin); }
void FileStream::Seek(std::uint32_t position) { SeekFrom(static_cast<long>(position)); }

std::uint32_t FileStream::Size() { return file.Size(); }
std::uint32_t FileStream::Position() { return file.Position(); }

void FileStream::Clear() {
    ok = true;
    file.Seek(0L, platform::SeekFrom::Begin);
}
