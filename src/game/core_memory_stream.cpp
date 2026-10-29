// The memory stream's own slots (Read, Write, Seek, CopyFrom), its Resize / SetSize and bit helpers,
// CStream::CopyFrom and the stream helpers that write and read a sized block or a format header
// See core_streams.h and docs/notes/core.md.
#include "core_streams.h"

#include <intrin.h>

#include <cstring>
#include <vector>

namespace {

// The format header's magic number and version are written and read as four bytes each.
constexpr std::uint32_t kHeaderFieldBytes = 4;
// Sizes above this are written in four bytes (bit 0 set), others in two.
constexpr std::int32_t kShortSizeLimit = 30000;

} // namespace

// --- CMemoryStream slots -----------------------------------------------------------------------

std::uint32_t CMemoryStream::Read(void* buffer, std::uint32_t byteCount) {
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

void CMemoryStream::Write(const void* buffer, std::uint32_t byteCount) {
    if (g_memoryStreamWriteObserver)
        g_memoryStreamWriteObserver(this, byteCount, static_cast<void* const*>(_AddressOfReturnAddress()));
    if (position + byteCount > data.size()) data.resize((static_cast<std::uint32_t>(data.size()) + byteCount) * 2);
    std::memcpy(data.data() + position, buffer, byteCount);
    position += byteCount;
    if (!(size > position)) size = position;
}

void CMemoryStream::Seek(std::uint32_t newPosition) {
    position = newPosition;
    EndBits();
}

void CMemoryStream::CopyFrom(CStream* source) {
    source->Seek(0);
    Resize(source->Size());
    source->Read(data.data(), size);
    Seek(0);
}

// --- CMemoryStream sizes and bits --------------------------------------------------------------

void CMemoryStream::Resize(std::uint32_t byteCount) {
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

void CMemoryStream::SetSize(std::uint32_t newSize) {
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

void CMemoryStream::StartByte() {
    if (bitsIndex >= 0) data[bitsIndex] = bits;
    bitsIndex = static_cast<std::int32_t>(position);
    bits = 0;
    WriteValue(bits);
    bitMask = 1;
}

void CMemoryStream::EndBits() {
    if (bitsIndex >= 0) data[bitsIndex] = bits;
    bitsIndex = -1;
    bitMask = 0x80;
}

// --- CStream helpers ---------------------------------------------------------------------------

void CStream::CopyFrom(CStream* source) {
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

void CStream::ReadSized(CStream* to) {
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

void CStream::WriteSized(CStream* from) {
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

bool CStream::ReadHeader(std::uint32_t magic, std::int32_t newestVersion, std::int32_t oldestVersion) {
    std::int32_t value = 0;
    ReadValue(value);
    if (static_cast<std::uint32_t>(value) != magic) return false;
    ReadValue(value);
    if (value > newestVersion) return false;
    if (value < oldestVersion) return false;
    version = static_cast<std::uint32_t>(value);
    return true;
}

void CStream::WriteHeader(std::uint32_t magic, std::uint32_t headerVersion) {
    WriteValue(magic);
    WriteValue(headerVersion);
    version = headerVersion;
}
