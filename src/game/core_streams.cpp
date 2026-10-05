// Stream and reference-counted base classes: constructor and destructor bodies, the stream's string
// read, the memory stream's Clear and Reset, the aligned pointer arrays and the coder state
// constructor. See core_streams.h.
#include "core_streams.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "originals_library.h"

#include <cstring>
#include <new>
#include "originals_engine.h"

namespace {
using ei::SetVtable;

// CStream::version_ of a new stream.
constexpr std::uint32_t kDefaultVersion = 2000000000;

// Scalar deleting destructor tail: frees the object when bit 0 of the flags is set.
void* DeleteIf(void* self, unsigned flags) {
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}

// operator+(const CString&, char) (AFXAPI, 006FD56E).

// The hook of CMemoryStream's slot 6 (a virtual: registered through this shim).
void __fastcall MemoryStreamClearHook(CMemoryStream* self) { self->CMemoryStream::Clear(); }

} // namespace

// --- CRefObject root / reference-counted value -------------------------------------------------

void __fastcall RefObjectDestructBase(void* self) { SetVtable(self, kRefObjectRootVtable); }
EI_REPLACE(0x00412720, RefObjectDestructBase);
EI_REPLACE(0x00413430, RefObjectDestructBase);   // byte-identical copy
EI_REPLACE(0x00412960, RefObjectDestructBase);   // byte-identical copy
EI_REPLACE(0x004368C0, RefObjectDestructBase);   // byte-identical copy

void* __fastcall RefObjectDeletingDestructor(void* self, void*, unsigned flags) {
    SetVtable(self, kRefObjectRootVtable);
    return DeleteIf(self, flags);
}
EI_REPLACE(0x00412730, RefObjectDeletingDestructor);

void* __fastcall RefValueConstructBase(void* self) {
    SetVtable(self, kRefValueVtable);
    return self;
}
EI_REPLACE(0x00419720, RefValueConstructBase);

// The original calls 00412720 (the root's destructor body) out of line.
void* __fastcall RefValueDeletingDestructor(void* self, void*, unsigned flags) {
    RefObjectDestructBase(self);
    return DeleteIf(self, flags);
}
EI_REPLACE(0x00412700, RefValueDeletingDestructor);

// --- CStream -----------------------------------------------------------------------------------

CStream* CStream::ConstructBase() {
    SetVtable(this, kVtableAddress);
    version_ = kDefaultVersion;
    ok_ = 1;
    return this;
}
EI_REPLACE_METHOD(0x0047A200, CStream::ConstructBase);

void CStream::DestructBase() { SetVtable(this, kVtableAddress); }
EI_REPLACE_METHOD(0x004174E0, CStream::DestructBase);

CStream* CStream::DeletingDestruct(unsigned flags) {
    SetVtable(this, kVtableAddress);
    DeleteIf(this, flags);
    return this;
}
EI_REPLACE_METHOD(0x00417400, CStream::DeletingDestruct);

mfc::CString* CStream::ReadString(void* out) {
    mfc::CString text;
    unsigned char byte;
    while (Read(&byte, 1) && byte) text = PlusChar(text, static_cast<char>(byte));
    return new (out) mfc::CString(text);
}
EI_REPLACE_METHOD(0x00461190, CStream::ReadString);

// --- CMemoryStream (its slots 1-3 and 7, Resize and SetSize are in core_memory_stream.cpp) --------

CMemoryStream* CMemoryStream::Construct() {
    version_ = kDefaultVersion;
    SetVtable(this, kVtableAddress);
    data_ = nullptr;
    capacity_ = 0;
    position_ = 0;
    size_ = 0;
    ok_ = 1;
    bitMask_ = 0x80;
    bitsIndex_ = -1;
    return this;
}
EI_REPLACE_METHOD(0x0041DD70, CMemoryStream::Construct);

void CMemoryStream::Destruct() {
    SetVtable(this, kVtableAddress);
    if (data_) gamecrt::operator_delete(data_);
    SetVtable(this, CStream::kVtableAddress);
}
EI_REPLACE_METHOD(0x004173D0, CMemoryStream::Destruct);
EI_REPLACE_METHOD(0x004790A0, CMemoryStream::Destruct);   // byte-identical copy (vtable 0073C960)

// The originals call their own copy of the destructor body out of line.
CMemoryStream* CMemoryStream::DeletingDestruct(unsigned flags) {
    Destruct();
    DeleteIf(this, flags);
    return this;
}
EI_REPLACE_METHOD(0x004173B0, CMemoryStream::DeletingDestruct);
EI_REPLACE_METHOD(0x004790D0, CMemoryStream::DeletingDestruct);

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
EI_REPLACE(0x00417350, MemoryStreamClearHook);

void CMemoryStream::Reset() {
    ok_ = 1;
    data_ = nullptr;
    capacity_ = 0;
    position_ = 0;
    size_ = 0;
    bitMask_ = 0x80;
    bitsIndex_ = -1;
}
EI_REPLACE_METHOD(0x00468C20, CMemoryStream::Reset);

// --- Aligned pointer arrays --------------------------------------------------------------------

void** __fastcall AlignedPointerArrayAlloc(int count) {
    auto* block = static_cast<std::uint8_t*>(gamecrt::operator_new(static_cast<std::size_t>(count) * 4 + 0x24));
    auto aligned = (reinterpret_cast<std::uintptr_t>(block) + 0x23) & ~std::uintptr_t{0x1F};
    reinterpret_cast<std::uint8_t**>(aligned)[-1] = block;
    return reinterpret_cast<void**>(aligned);
}
EI_REPLACE(0x0047B940, AlignedPointerArrayAlloc);

void AlignedObjectArray::Clear() {
    // The size is read again after every call (a release may change the array).
    CEngineObject** element = data;
    for (int i = 0; i < size; ++i) (*element++)->Release();
    if (data) {
        gamecrt::operator_delete(reinterpret_cast<void**>(data)[-1]);
        data = nullptr;
    }
    capacity = 0;
    size = 0;
}
EI_REPLACE_METHOD(0x0047B620, AlignedObjectArray::Clear);

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
EI_REPLACE_METHOD(0x0042FCA0, CoderState::Construct);
