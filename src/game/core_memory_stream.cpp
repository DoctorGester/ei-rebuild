// The memory stream's own slots (Read, Write, Seek, CopyFrom), its Resize / SetSize and bit helpers,
// CStream::CopyFrom and the stream helpers that write and read a sized block or a format header
// (004A4D40-004A5650). See core_streams.h and docs/notes/core.md.
#include "core_streams.h"

#include "ei/hook.h"
#include "lib/crt.h"

#include <cstring>

namespace {

// Virtual slots are registered through shims (rewrite-guide rule 4).
// The slot is void, but 004A53F0 leaves the new position in EAX and callers return it
// (CPlayerClient::SendResume, TakeExit, ...).
std::uint32_t __fastcall MemoryStreamWriteHook(CMemoryStream* self, void*, const void* buffer, std::uint32_t size) {
    self->CMemoryStream::Write(buffer, size);
    return self->position_;
}

// The format header's magic number and version are written and read as four bytes each.
constexpr std::uint32_t kHeaderFieldBytes = 4;
// Sizes above this are written in four bytes (bit 0 set), others in two.
constexpr std::int32_t kShortSizeLimit = 30000;

} // namespace

// --- CMemoryStream slots -----------------------------------------------------------------------

std::uint32_t CMemoryStream::ReadImpl(void* buffer, std::uint32_t size) {
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
EI_REPLACE_METHOD(0x004A5380, CMemoryStream::ReadImpl);

void CMemoryStream::Write(const void* buffer, std::uint32_t size) {
    if (position_ + size > capacity_) {
        std::uint32_t old = capacity_;
        capacity_ = (old + size) * 2;
        auto* grown = static_cast<std::uint8_t*>(gamecrt::operator_new(capacity_));
        std::memcpy(grown, data_, old);
        gamecrt::operator_delete(data_);
        data_ = grown;
    }
    std::memcpy(data_ + position_, buffer, size);
    position_ += size;
    if (!(size_ > position_)) size_ = position_;
}
EI_REPLACE(0x004A53F0, MemoryStreamWriteHook);

void CMemoryStream::SeekImpl(std::uint32_t position) {
    position_ = position;
    EndBits();
}
EI_REPLACE_METHOD(0x004A5490, CMemoryStream::SeekImpl);

void CMemoryStream::CopyFromImpl(CStream* source) {
    source->Seek(0);
    Resize(source->Size());
    source->Read(data_, size_);
    Seek(0);
}
EI_REPLACE_METHOD(0x004A55D0, CMemoryStream::CopyFromImpl);

// --- CMemoryStream sizes and bits --------------------------------------------------------------

void CMemoryStream::Resize(std::uint32_t size) {
    if (size <= capacity_) {
        size_ = size;
        position_ = 0;
        return;
    }
    std::uint8_t* old = data_;
    std::uint32_t capacity = (size >> 1) + size + 0x10;
    data_ = static_cast<std::uint8_t*>(gamecrt::operator_new(capacity));
    std::memcpy(data_, old, size_);
    capacity_ = capacity;
    position_ = 0;
    if (bitsIndex_ >= 0) data_[bitsIndex_] = bits_;
    bitsIndex_ = -1;
    size_ = size;
    bitMask_ = 0x80;
    if (old) gamecrt::operator_delete(old);
}

void CMemoryStream::SetSize(std::uint32_t size) {
    if (size <= capacity_) {
        size_ = size;
        position_ = 0;
        return;
    }
    if (data_) gamecrt::operator_delete(data_);
    auto* fresh = static_cast<std::uint8_t*>(gamecrt::operator_new(size));
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
    Write(&bits_, 1);
    bitMask_ = 1;
}

void CMemoryStream::EndBits() {
    if (bitsIndex_ >= 0) data_[bitsIndex_] = bits_;
    bitsIndex_ = -1;
    bitMask_ = 0x80;
}

// --- CStream helpers ---------------------------------------------------------------------------

void CStream::CopyFromImpl(CStream* source) {
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
EI_REPLACE_METHOD(0x004A4D40, CStream::CopyFromImpl);

void CStream::ReadSized(CStream* to) {
    std::uint32_t size = 0;
    Read(&size, 2);
    if (size & 1) Read(reinterpret_cast<std::uint8_t*>(&size) + 2, 2);
    size >>= 1;
    to->Clear();
    to->Seek(0);
    if (!size) return;
    void* block = gamecrt::operator_new(size);
    Read(block, size);
    to->Write(block, size);
    gamecrt::operator_delete(block);
}

void CStream::WriteSized(CStream* from) {
    auto size = static_cast<std::int32_t>(from->Size());
    std::uint32_t word;
    if (size > kShortSizeLimit) {
        word = static_cast<std::uint32_t>(size) * 2 + 1;
        Write(&word, 4);
    } else {
        word = static_cast<std::uint32_t>(size) * 2;
        Write(&word, 2);
    }
    void* block = gamecrt::operator_new(static_cast<std::uint32_t>(size));
    from->Seek(0);
    from->Read(block, static_cast<std::uint32_t>(size));
    Write(block, static_cast<std::uint32_t>(size));
    gamecrt::operator_delete(block);
}

bool CStream::ReadHeader(std::uint32_t magic, std::int32_t newestVersion, std::int32_t oldestVersion) {
    std::int32_t value = 0;
    Read(&value, kHeaderFieldBytes);
    if (static_cast<std::uint32_t>(value) != magic) return false;
    Read(&value, kHeaderFieldBytes);
    if (value > newestVersion) return false;
    if (value < oldestVersion) return false;
    version_ = static_cast<std::uint32_t>(value);
    return true;
}

void CStream::WriteHeader(std::uint32_t magic, std::uint32_t version) {
    Write(&magic, kHeaderFieldBytes);
    Write(&version, kHeaderFieldBytes);
    version_ = version;
}
