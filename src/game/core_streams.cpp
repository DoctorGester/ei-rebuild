// Stream and reference-counted base classes: constructor and destructor bodies, the stream's string
// read, the memory stream's Clear and Reset, the aligned pointer arrays and the coder state
// constructor. See core_streams.h.
#include "core_streams.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/compiled_vtable.h"
#include "lib/containers.h"
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

namespace {
// The root's compiled table (C19; game.exe's 0073BB68): 0 the deleting destructor, 1 Serialize (empty, RET 4).
struct CountedRootClass final : ei::CompiledDeleter<CountedRootClass, &CRefCounted::RootDeletingDestruct> {
    virtual void Serialize(ChunkArchive*) {}   // 1
};
} // namespace

void* CRefCounted::RootVtable() { return CountedRootClass::Vtable(); }

namespace {
// The plain counted root's compiled table (C19; game.exe's 0073C368): 0 the deleting destructor.
struct PlainRootClass final : ei::CompiledTag<PlainRootClass> {
    virtual void* DeleteScalar(unsigned flags) {   // 0: 00457950
        SetVtable(this, PlainRootVtable());
        return DeleteIf(this, flags);
    }
};
} // namespace

void* PlainRootVtable() { return PlainRootClass::Vtable(); }

extern "C" int __cdecl _purecall();

namespace {
// The counted value's compiled table (C19; game.exe's 0073BB58): 0 the deleting destructor, 1 Serialize (empty,
// RET 4), 2 / 3 the compiler CRT's `_purecall` (game.exe's 006E73EB): a derived constructor stores its own table
// before anything can call them.
struct RefObjectBaseClass final : ei::CompiledDeleter<RefObjectBaseClass, &CRefObject::ValueDeletingDestruct> {
    virtual void Serialize(ChunkArchive*) {}            // 1
    virtual int OnShutdown() { return _purecall(); }    // 2
    virtual CRefObject* Clone() {                       // 3
        _purecall();
        return nullptr;
    }
};
} // namespace

void* CRefObject::Vtable() { return RefObjectBaseClass::Vtable(); }

void __fastcall RefObjectDestructBase(void* self) { SetVtable(self, CRefCounted::RootVtable()); }
EI_REWRITTEN(0x00412720, RefObjectDestructBase);
EI_REWRITTEN(0x00413430, RefObjectDestructBase);
EI_REWRITTEN(0x005ED7A0, RefObjectDestructBase);
EI_REWRITTEN(0x00412960, RefObjectDestructBase);
EI_REWRITTEN(0x004368C0, RefObjectDestructBase);
EI_REWRITTEN(0x00562770, RefObjectDestructBase);
EI_REWRITTEN(0x0058FBE0, RefObjectDestructBase);
EI_REWRITTEN(0x005B3590, RefObjectDestructBase);

CRefCounted* CRefCounted::RootDeletingDestruct(unsigned flags) {
    SetVtable(this, RootVtable());
    DeleteIf(this, flags);
    return this;
}
EI_REWRITTEN(0x00412730, CRefCounted::RootDeletingDestruct);
EI_REWRITTEN(0x005B3570, CRefCounted::RootDeletingDestruct);   // S13: the unit shape's (calls its copy 005B3590)

void* __fastcall RefValueConstructBase(void* self) {
    SetVtable(self, CRefObject::Vtable());
    return self;
}
EI_REWRITTEN(0x00419720, RefValueConstructBase);

CRefObject* CRefObject::ValueDeletingDestruct(unsigned flags) {
    RefObjectDestructBase(this);
    DeleteIf(this, flags);
    return this;
}
EI_REWRITTEN(0x00412700, CRefObject::ValueDeletingDestruct);
EI_REWRITTEN(0x00562750, CRefObject::ValueDeletingDestruct);   // byte-identical copies (A-W12)
EI_REWRITTEN(0x0058FBC0, CRefObject::ValueDeletingDestruct);

// --- CStream -----------------------------------------------------------------------------------

CStream* CStream::ConstructBase() {
    SetVtable(this, kVtableAddress);
    version_ = kDefaultVersion;
    ok_ = 1;
    return this;
}
EI_REWRITTEN(0x0047A200, CStream::ConstructBase);

void CStream::DestructBase() { SetVtable(this, kVtableAddress); }
EI_REWRITTEN(0x004174E0, CStream::DestructBase);

CStream* CStream::DeletingDestruct(unsigned flags) {
    SetVtable(this, kVtableAddress);
    DeleteIf(this, flags);
    return this;
}
EI_REPLACE_METHOD(0x00417400, CStream::DeletingDestruct);

mfc::CString* CStream::ReadString(void* out) {
    mfc::CString text;
    unsigned char byte;
    while (Read(&byte, 1) && byte) text = mfc::operator+(text, static_cast<char>(byte));
    return new (out) mfc::CString(text);
}
EI_REWRITTEN(0x00461190, CStream::ReadString);

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
EI_REWRITTEN(0x00468C20, CMemoryStream::Reset);

// --- Aligned pointer arrays --------------------------------------------------------------------

void** __fastcall AlignedPointerArrayAlloc(int count) {
    return static_cast<void**>(lib::AllocAligned32(static_cast<std::size_t>(count) * 4));
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
EI_REPLACE_METHOD(0x0042FCA0, CoderState::Construct);
