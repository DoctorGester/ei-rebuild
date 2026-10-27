// Stream and reference-counted base classes: constructor and destructor bodies, the stream's string
// read, the memory stream's Clear and Reset, the aligned pointer arrays and the coder state
// constructor. See core_streams.h.
#include "core_streams.h"

#include "ei/hook.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/mfc.h"

#include <cstring>
#include <new>
#include <string>

namespace {

// CStream::version_ of a new stream.
constexpr std::uint32_t kDefaultVersion = 2000000000;

// operator+(const CString&, char) (AFXAPI, 006FD56E).

} // namespace

// --- CRefCounted / CRefObject: game.exe's out-of-line constructor and destructor bodies ----------

EI_REWRITTEN_SPECIAL(0x00412720, CRefCounted::~CRefCounted);
EI_REWRITTEN_SPECIAL(0x005ED7A0, CRefCounted::~CRefCounted);
EI_REWRITTEN_SPECIAL(0x004368C0, CRefCounted::~CRefCounted);
EI_REWRITTEN_SPECIAL(0x00562770, CRefCounted::~CRefCounted);
EI_REWRITTEN_SPECIAL(0x0058FBE0, CRefCounted::~CRefCounted);
EI_REWRITTEN_SPECIAL(0x00412730, CRefCounted::~CRefCounted);   // deleting
EI_REWRITTEN_SPECIAL(0x00419720, CRefObject::CRefObject);
EI_REWRITTEN_SPECIAL(0x00412700, CRefObject::~CRefObject);   // deleting
EI_REWRITTEN_SPECIAL(0x00562750, CRefObject::~CRefObject);   // deleting; byte-identical copies (A-W12)
EI_REWRITTEN_SPECIAL(0x0058FBC0, CRefObject::~CRefObject);   // deleting

// --- CStream -----------------------------------------------------------------------------------

CStream::CStream() {
    version_ = kDefaultVersion;
    ok_ = 1;
}
EI_REWRITTEN_SPECIAL(0x0047A200, CStream::CStream);
EI_REWRITTEN_SPECIAL(0x004174E0, CStream::~CStream);
EI_REWRITTEN_SPECIAL(0x00417400, CStream::~CStream);

void* CStream::operator new(std::size_t size) noexcept { return gamecrt::operator_new(size); }
void CStream::operator delete(void* block) { gamecrt::operator_delete(block); }

void (*g_memoryStreamWriteObserver)(CMemoryStream* stream, std::uint32_t size, void* const* frame) = nullptr;

mfc::CString* CStream::ReadString(void* out) {
    std::string text;
    unsigned char byte;
    while (Read(&byte, 1) && byte) text = mfc::operator+(text, static_cast<char>(byte));
    return new (out) mfc::CString(text);
}
EI_REWRITTEN(0x00461190, CStream::ReadString);

// --- CMemoryStream (its slots 1-3 and 7, Resize and SetSize are in core_memory_stream.cpp) --------

CMemoryStream::CMemoryStream() {
    data_ = nullptr;
    capacity_ = 0;
    position_ = 0;
    size_ = 0;
    bitMask_ = 0x80;
    bitsIndex_ = -1;
}
EI_REWRITTEN_SPECIAL(0x0041DD70, CMemoryStream::CMemoryStream);
EI_REWRITTEN_SPECIAL(0x0066C4F0, CMemoryStream::CMemoryStream);   // the queue streams' (table 0073C960)

CMemoryStream::~CMemoryStream() {
    if (data_) gamecrt::operator_delete(data_);
}
EI_REWRITTEN_SPECIAL(0x004173D0, CMemoryStream::~CMemoryStream);
EI_REWRITTEN_SPECIAL(0x004790A0, CMemoryStream::~CMemoryStream);   // the queue streams' copy
EI_REWRITTEN_SPECIAL(0x004173B0, CMemoryStream::~CMemoryStream);
EI_REWRITTEN_SPECIAL(0x004790D0, CMemoryStream::~CMemoryStream);

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
EI_REWRITTEN(0x00417350, CMemoryStream::Clear);

void CMemoryStream::Reset() {
    ok_ = 1;
    data_ = nullptr;
    capacity_ = 0;
    position_ = 0;
    size_ = 0;
    bitMask_ = 0x80;
    bitsIndex_ = -1;
}
EI_REWRITTEN(0x00468C20, CMemoryStream::Reset);

// --- Aligned pointer arrays --------------------------------------------------------------------

void** __fastcall AlignedPointerArrayAlloc(int count) {
    return lib::AlignedArray<void*>::Allocate(count);
}
EI_REWRITTEN(0x0047B940, AlignedPointerArrayAlloc);
EI_REWRITTEN(0x004D9ED0, AlignedPointerArrayAlloc);
EI_REWRITTEN(0x0066BE00, AlignedPointerArrayAlloc);
EI_REWRITTEN(0x005A6F80, AlignedPointerArrayAlloc);

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
EI_REWRITTEN(0x0042FCA0, CoderState::Construct);
