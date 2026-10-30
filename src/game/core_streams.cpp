// Stream and reference-counted base classes: constructor and destructor bodies, the stream's string
// read, the memory stream's Clear and Reset, the aligned pointer arrays and the coder state
// constructor. See core_streams.h.
#include "core_streams.h"

#include "asset_text.h"

#include "lib/containers.h"
#include "lib/return_address.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr std::uint32_t kDefaultVersion = 2000000000;

} // namespace

// --- ByteStream -----------------------------------------------------------------------------------

ByteStream::ByteStream() {
    version = kDefaultVersion;
    ok = 1;
}

void (*g_memoryStreamWriteObserver)(MemoryStream* stream, std::uint32_t size, void* const* frame) = nullptr;

std::string ByteStream::ReadString() {
    std::string text;
    unsigned char byte;
    while (Read(&byte, 1) && byte) text = text + static_cast<char>(byte);
    text::FromAssetText(&text);
    return text;
}

void ByteStream::WriteString(std::string_view text) {
    const std::string bytes = text::ToAssetText(text.substr(0, text.find('\0')));
    Write(bytes.c_str(), static_cast<std::uint32_t>(bytes.size() + 1));
}

// --- MemoryStream (its Read, Write, Seek and CopyFrom; Resize and SetSize are in core_streams.cpp) --------

MemoryStream::MemoryStream() {
    position = 0;
    size = 0;
    bitMask = 0x80;
    bitsIndex = -1;
}

MemoryStream::~MemoryStream() = default;

MemoryStream::MemoryStream(MemoryStream&& other) noexcept
    : bits(other.bits), bitMask(other.bitMask), bitsIndex(other.bitsIndex),
      data(std::exchange(other.data, {})),
      position(std::exchange(other.position, 0)), size(std::exchange(other.size, 0)) {
    reading = other.reading;
    ok = other.ok;
    version = other.version;
}

void MemoryStream::Clear() {
    data = {};
    position = 0;
    size = 0;
    ok = 1;
    bitMask = 0x80;
    bitsIndex = -1;
}

// --- Core stream chunks ------------------------------------------------------------------------------

// Tagged blocks and text in a stream: ByteStream::ReadTagged, FindTagged ReplaceTagged and ReadText. Every
// stream access goes through the virtual functions, in the game's order.

bool ByteStream::ReadTagged(std::uint32_t* tag, ByteStream* out) {
    std::uint32_t size = 0;
    if (!Read(tag, 4)) return false;
    if (!Read(&size, 4)) return false;
    size -= 8;
    out->Clear();
    out->reading = 0;
    std::vector<std::uint8_t> block(size);
    if (Read(block.data(), size) != size) return false;
    out->Write(block.data(), size);
    out->Seek(0);
    return true;
}

bool ByteStream::FindTagged(std::uint32_t id, ByteStream* out) {
    Seek(0);
    reading = 1;
    std::uint32_t tag;
    while (ReadTagged(&tag, out)) {
        if (tag == id) return true;
    }
    out->Clear();
    return false;
}

void ByteStream::ReadText(char* buffer, int capacity) {
    char* at = buffer;
    // Each byte is read into the low byte of one dword: a failed read of a stream that does not zero-fill
    // leaves the old byte.
    std::uint32_t slot = 0;
    for (int left = capacity; left > 0;) {
        Read(&slot, 1);
        char c = static_cast<char>(slot);
        *at++ = c;
        --left;
        if (c == 0) break;
    }
    buffer[capacity - 1] = 0;
    std::string text(buffer);
    text::FromAssetText(&text);
    text::CopyFitting(buffer, static_cast<std::size_t>(capacity), text);
}

// --- Core stream diff --------------------------------------------------------------------------------

// The save game's difference coding between two streams: ByteStream::WriteDiff
// and ByteStream::ApplyDiff. The format is described in core_streams.h. Every stream
// access goes through the stream's virtual functions, in the game's order.

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
    std::uint32_t end = Position();
    Seek(header);
    WriteValue(static_cast<std::uint8_t>(count));
    Seek(end);
    Position();
    WriteValue(std::uint8_t{0});
}

void ByteStream::ApplyDiff(ByteStream* base, ByteStream* diff) {
    std::uint32_t length = 0;   // one byte is read into it at a time
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

// --- Core memory stream ------------------------------------------------------------------------------

// The memory stream's own slots (Read, Write, Seek, CopyFrom), its Resize / SetSize and bit helpers,
// ByteStream::CopyFrom and the stream helpers that write and read a sized block or a format header
// See core_streams.h and docs/notes/core.md.

namespace {

// The format header's magic number and version are written and read as four bytes each.
constexpr std::uint32_t kHeaderFieldBytes = 4;
// Sizes above this are written in four bytes (bit 0 set), others in two.
constexpr std::int32_t kShortSizeLimit = 30000;

} // namespace

// --- MemoryStream slots -----------------------------------------------------------------------

std::uint32_t MemoryStream::Read(void* buffer, std::uint32_t byteCount) {
    if (position + byteCount > size) {
        std::uint32_t left = size - position;
        ok = 0;
        std::memset(buffer, 0, byteCount);
        if (static_cast<std::int32_t>(left) < 0) return 0;
        byteCount = left;
    }
    std::memcpy(buffer, data.data() + position, byteCount);
    position += byteCount;
    return byteCount;
}

void MemoryStream::Write(const void* buffer, std::uint32_t byteCount) {
    if (g_memoryStreamWriteObserver) {
#if defined(_MSC_VER)
        g_memoryStreamWriteObserver(this, byteCount, static_cast<void* const*>(_AddressOfReturnAddress()));
#else
        // The Windows harness scans stack words; other ABIs expose only the direct caller here.
        void* callers[64] = {EI_RETURN_ADDRESS()};
        g_memoryStreamWriteObserver(this, byteCount, callers);
#endif
    }
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

// --- MemoryStream sizes and bits --------------------------------------------------------------

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

// --- ByteStream helpers ---------------------------------------------------------------------------

void ByteStream::CopyFrom(ByteStream* source) {
    std::uint8_t chunk[0x400];
    source->Seek(0);
    source->reading = 1;
    reading = 0;
    for (;;) {
        std::uint32_t read = source->Read(chunk, sizeof(chunk));
        if (!read) break;
        Write(chunk, read);
    }
}

void ByteStream::ReadSized(ByteStream* to) {
    std::uint32_t size = 0;
    Read(&size, 2);
    if (size & 1) Read(reinterpret_cast<std::uint8_t*>(&size) + 2, 2);
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
    std::uint32_t word;
    if (size > kShortSizeLimit) {
        word = static_cast<std::uint32_t>(size) * 2 + 1;
        WriteValue(word);
    } else {
        word = static_cast<std::uint32_t>(size) * 2;
        Write(&word, 2);
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
    version = static_cast<std::uint32_t>(value);
    return true;
}

void ByteStream::WriteHeader(std::uint32_t magic, std::uint32_t headerVersion) {
    WriteValue(magic);
    WriteValue(headerVersion);
    version = headerVersion;
}
