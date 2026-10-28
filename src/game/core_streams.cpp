// Stream and reference-counted base classes: constructor and destructor bodies, the stream's string
// read, the memory stream's Clear and Reset, the aligned pointer arrays and the coder state
// constructor. See core_streams.h.
#include "core_streams.h"

#include "asset_text.h"

#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/mfc.h"

#include <cstring>
#include <new>
#include <string>
#include <utility>

namespace {

constexpr std::uint32_t kDefaultVersion = 2000000000;

// operator+(const CString&, char) (AFXAPI).

} // namespace

// --- CStream -----------------------------------------------------------------------------------

CStream::CStream() {
    version_ = kDefaultVersion;
    ok_ = 1;
}

void* CStream::operator new(std::size_t size) noexcept { return gamecrt::operator_new(size); }
void CStream::operator delete(void* block) { gamecrt::operator_delete(block); }

void (*g_memoryStreamWriteObserver)(CMemoryStream* stream, std::uint32_t size, void* const* frame) = nullptr;

std::string* CStream::ReadString(std::string* out) {
    std::string text;
    unsigned char byte;
    while (Read(&byte, 1) && byte) text = text + static_cast<char>(byte);
    text::FromAssetText(&text);
    *out = text;
    return out;
}

void CStream::WriteString(std::string_view text) {
    const std::string bytes = text::ToAssetText(text.substr(0, text.find('\0')));
    Write(bytes.c_str(), static_cast<std::uint32_t>(bytes.size() + 1));
}

// --- CMemoryStream (its slots 1-3 and 7, Resize and SetSize are in core_memory_stream.cpp) --------

CMemoryStream::CMemoryStream() {
    data_ = nullptr;
    capacity_ = 0;
    position_ = 0;
    size_ = 0;
    bitMask_ = 0x80;
    bitsIndex_ = -1;
}

CMemoryStream::~CMemoryStream() {
    if (data_) gamecrt::operator_delete(data_);
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
    if (data_) gamecrt::operator_delete(data_);
    data_ = nullptr;
    capacity_ = 0;
    position_ = 0;
    size_ = 0;
    ok_ = 1;
    bitMask_ = 0x80;
    bitsIndex_ = -1;
}

// --- Aligned pointer arrays --------------------------------------------------------------------

// --- Coder state -------------------------------------------------------------------------------

CoderState* CoderState::Construct() {
    out.p = nullptr;
    out.bits = 0;
    out.bytePtr = nullptr;
    out.sink = nullptr;
    prev = 0;
    matchPos = -1;
    pos = 0;
    chain[0] = 0x400;
    std::memset(head, 0, sizeof(head));
    window[0] = 0;
    window[0x400] = 0;
    window[0x3FF] = 0;
    window[0x7FF] = 0;
    return this;
}
