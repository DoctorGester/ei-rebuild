// The memory stream's own slots (Read, Write, Seek, CopyFrom), its Resize / SetSize and bit helpers,
// CStream::CopyFrom and the stream helpers that write and read a sized block or a format header
// See core_streams.h and docs/notes/core.md.
#include "core_streams.h"

#include <intrin.h>

#include <cstring>

namespace {

// The format header's magic number and version are written and read as four bytes each.
constexpr std::uint32_t kHeaderFieldBytes = 4;
// Sizes above this are written in four bytes (bit 0 set), others in two.
constexpr std::int32_t kShortSizeLimit = 30000;

} // namespace

// --- CMemoryStream slots -----------------------------------------------------------------------

std::uint32_t CMemoryStream::Read(void* buffer, std::uint32_t size) {
    if (position_ + size > size_) {
        std::uint32_t left = size_ - position_;
        ok_ = 0;
        std::memset(buffer, 0, size);
        if (static_cast<std::int32_t>(left) < 0) return 0;
        size = left;
    }
    std::memcpy(buffer, data_ + position_, size);
    position_ += size;
    return size;
}

void CMemoryStream::Write(const void* buffer, std::uint32_t size) {
    if (g_memoryStreamWriteObserver)
        g_memoryStreamWriteObserver(this, size, static_cast<void* const*>(_AddressOfReturnAddress()));
    if (position_ + size > capacity_) {
        std::uint32_t old = capacity_;
        capacity_ = (old + size) * 2;
        auto* grown = new std::uint8_t[capacity_];
        std::memcpy(grown, data_, old);
        delete[] data_;
        data_ = grown;
    }
    std::memcpy(data_ + position_, buffer, size);
    position_ += size;
    if (!(size_ > position_)) size_ = position_;
}

void CMemoryStream::Seek(std::uint32_t position) {
    position_ = position;
    EndBits();
}

void CMemoryStream::CopyFrom(CStream* source) {
    source->Seek(0);
    Resize(source->Size());
    source->Read(data_, size_);
    Seek(0);
}

// --- CMemoryStream sizes and bits --------------------------------------------------------------

void CMemoryStream::Resize(std::uint32_t size) {
    if (size <= capacity_) {
        size_ = size;
        position_ = 0;
        return;
    }
    std::uint8_t* old = data_;
    std::uint32_t capacity = (size >> 1) + size + 0x10;
    data_ = new std::uint8_t[capacity];
    std::memcpy(data_, old, size_);
    capacity_ = capacity;
    position_ = 0;
    if (bitsIndex_ >= 0) data_[bitsIndex_] = bits_;
    bitsIndex_ = -1;
    size_ = size;
    bitMask_ = 0x80;
    if (old) delete[] old;
}

void CMemoryStream::SetSize(std::uint32_t size) {
    if (size <= capacity_) {
        size_ = size;
        position_ = 0;
        return;
    }
    if (data_) delete[] data_;
    auto* fresh = new std::uint8_t[size];
    capacity_ = size;
    size_ = size;
    data_ = fresh;
    position_ = 0;
    bitMask_ = 0x80;
}

void CMemoryStream::StartByte() {
    if (bitsIndex_ >= 0) data_[bitsIndex_] = bits_;
    bitsIndex_ = static_cast<std::int32_t>(position_);
    bits_ = 0;
    WriteValue(bits_);
    bitMask_ = 1;
}

void CMemoryStream::EndBits() {
    if (bitsIndex_ >= 0) data_[bitsIndex_] = bits_;
    bitsIndex_ = -1;
    bitMask_ = 0x80;
}

// --- CStream helpers ---------------------------------------------------------------------------

void CStream::CopyFrom(CStream* source) {
    std::uint8_t chunk[0x400];
    source->Seek(0);
    source->reading_ = 1;
    reading_ = 0;
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
    auto* block = new std::uint8_t[size];
    Read(block, size);
    to->Write(block, size);
    delete[] block;
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
    auto* block = new std::uint8_t[static_cast<std::uint32_t>(size)];
    from->Seek(0);
    from->Read(block, static_cast<std::uint32_t>(size));
    Write(block, static_cast<std::uint32_t>(size));
    delete[] block;
}

bool CStream::ReadHeader(std::uint32_t magic, std::int32_t newestVersion, std::int32_t oldestVersion) {
    std::int32_t value = 0;
    ReadValue(value);
    if (static_cast<std::uint32_t>(value) != magic) return false;
    ReadValue(value);
    if (value > newestVersion) return false;
    if (value < oldestVersion) return false;
    version_ = static_cast<std::uint32_t>(value);
    return true;
}

void CStream::WriteHeader(std::uint32_t magic, std::uint32_t version) {
    WriteValue(magic);
    WriteValue(version);
    version_ = version;
}
