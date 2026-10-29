// Stream and reference-counted base classes: constructor and destructor bodies, the stream's string
// read, the memory stream's Clear and Reset, the aligned pointer arrays and the coder state
// constructor. See core_streams.h.
#include "core_streams.h"

#include "asset_text.h"

#include "lib/containers.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

#include <new>

namespace {

constexpr std::uint32_t kDefaultVersion = 2000000000;

} // namespace

// --- CStream -----------------------------------------------------------------------------------

CStream::CStream() {
    version_ = kDefaultVersion;
    ok_ = 1;
}


void (*g_memoryStreamWriteObserver)(CMemoryStream* stream, std::uint32_t size, void* const* frame) = nullptr;

std::string CStream::ReadString() {
    std::string text;
    unsigned char byte;
    while (Read(&byte, 1) && byte) text = text + static_cast<char>(byte);
    text::FromAssetText(&text);
    return text;
}

void CStream::WriteString(std::string_view text) {
    const std::string bytes = text::ToAssetText(text.substr(0, text.find('\0')));
    Write(bytes.c_str(), static_cast<std::uint32_t>(bytes.size() + 1));
}

// --- CMemoryStream (its Read, Write, Seek and CopyFrom; Resize and SetSize are in core_memory_stream.cpp) --------

CMemoryStream::CMemoryStream() {
    data_ = nullptr;
    capacity_ = 0;
    position_ = 0;
    size_ = 0;
    bitMask_ = 0x80;
    bitsIndex_ = -1;
}

CMemoryStream::~CMemoryStream() {
    if (data_) delete[] data_;
}

CMemoryStream::CMemoryStream(CMemoryStream&& other) noexcept
    : bits_(other.bits_), bitMask_(other.bitMask_), bitsIndex_(other.bitsIndex_),
      data_(std::exchange(other.data_, nullptr)), capacity_(std::exchange(other.capacity_, 0)),
      position_(std::exchange(other.position_, 0)), size_(std::exchange(other.size_, 0)) {
    reading_ = other.reading_;
    ok_ = other.ok_;
    version_ = other.version_;
}

void CMemoryStream::Clear() {
    if (data_) delete[] data_;
    data_ = nullptr;
    capacity_ = 0;
    position_ = 0;
    size_ = 0;
    ok_ = 1;
    bitMask_ = 0x80;
    bitsIndex_ = -1;
}
