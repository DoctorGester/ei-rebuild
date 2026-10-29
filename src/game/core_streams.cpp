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
    version = kDefaultVersion;
    ok = 1;
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
    position = 0;
    size = 0;
    bitMask = 0x80;
    bitsIndex = -1;
}

CMemoryStream::~CMemoryStream() = default;

CMemoryStream::CMemoryStream(CMemoryStream&& other) noexcept
    : bits(other.bits), bitMask(other.bitMask), bitsIndex(other.bitsIndex),
      data(std::exchange(other.data, {})),
      position(std::exchange(other.position, 0)), size(std::exchange(other.size, 0)) {
    reading = other.reading;
    ok = other.ok;
    version = other.version;
}

void CMemoryStream::Clear() {
    data = {};
    position = 0;
    size = 0;
    ok = 1;
    bitMask = 0x80;
    bitsIndex = -1;
}
