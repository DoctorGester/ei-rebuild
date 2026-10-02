// Stream and reference-counted base classes: constructor and destructor bodies, the memory
// stream's Clear, the aligned pointer arrays and the coder state constructor. See core_streams.h.
#include "core_streams.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"

#include <cstring>

namespace {
void SetVtable(void* object, std::uintptr_t vtable) { std::memcpy(object, &vtable, sizeof(vtable)); }

// Offsets in CStream (the memory stream adds its fields after these).
constexpr std::size_t kStreamOk = 0x05;
constexpr std::size_t kStreamLimit = 0x08;
constexpr std::uint32_t kDefaultLimit = 2000000000;

// Scalar deleting destructor tail: frees the object when bit 0 of the flags is set.
void* DeleteIf(void* self, unsigned flags) {
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}
} // namespace

// --- CRefObject root / reference-counted value -------------------------------------------------

void __fastcall RefObjectDestructBase(void* self) { SetVtable(self, kRefObjectRootVtable); }
EI_REPLACE(0x00412720, RefObjectDestructBase);
EI_REPLACE(0x00413430, RefObjectDestructBase);   // byte-identical copy

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

void* __fastcall StreamConstructBase(void* self) {
    auto* bytes = static_cast<std::uint8_t*>(self);
    SetVtable(self, kStreamVtable);
    std::memcpy(bytes + kStreamLimit, &kDefaultLimit, sizeof(kDefaultLimit));
    bytes[kStreamOk] = 1;
    return self;
}
EI_REPLACE(0x0047A200, StreamConstructBase);

void __fastcall StreamDestructBase(void* self) { SetVtable(self, kStreamVtable); }
EI_REPLACE(0x004174E0, StreamDestructBase);

void* __fastcall StreamDeletingDestructor(void* self, void*, unsigned flags) {
    SetVtable(self, kStreamVtable);
    return DeleteIf(self, flags);
}
EI_REPLACE(0x00417400, StreamDeletingDestructor);

// --- CMemoryStream -----------------------------------------------------------------------------

// Fields +4, +6, +7 and +0xC are left as they are.
CMemoryStream* __fastcall MemoryStreamConstruct(CMemoryStream* self) {
    self->unknown_8 = kDefaultLimit;
    SetVtable(self, kMemoryStreamVtable);
    self->data_ = nullptr;
    self->unknown_1C = 0;
    self->position_ = 0;
    self->size_ = 0;
    self->ok_ = 1;
    self->growBy_ = 0x80;
    self->unknown_14 = -1;
    return self;
}
EI_REPLACE(0x0041DD70, MemoryStreamConstruct);

// The buffer pointer is left dangling (the object is going away).
void __fastcall MemoryStreamDestruct(CMemoryStream* self) {
    SetVtable(self, kMemoryStreamVtable);
    if (self->data_) gamecrt::operator_delete(self->data_);
    SetVtable(self, kStreamVtable);
}
EI_REPLACE(0x004173D0, MemoryStreamDestruct);
EI_REPLACE(0x004790A0, MemoryStreamDestruct);   // byte-identical copy (vtable 0073C960)

// The originals call their own copy of the destructor body out of line.
void* __fastcall MemoryStreamDeletingDestructor(CMemoryStream* self, void*, unsigned flags) {
    MemoryStreamDestruct(self);
    return DeleteIf(self, flags);
}
EI_REPLACE(0x004173B0, MemoryStreamDeletingDestructor);
EI_REPLACE(0x004790D0, MemoryStreamDeletingDestructor);

// Slot 6: frees the buffer and empties the stream (CMemoryStream::Reset at 00468C20 is the same
// without the free).
void __fastcall MemoryStreamClear(CMemoryStream* self) {
    if (self->data_) gamecrt::operator_delete(self->data_);
    self->data_ = nullptr;
    self->unknown_1C = 0;
    self->position_ = 0;
    self->size_ = 0;
    self->ok_ = 1;
    self->growBy_ = 0x80;
    self->unknown_14 = -1;
}
EI_REPLACE(0x00417350, MemoryStreamClear);

// --- Aligned pointer arrays --------------------------------------------------------------------

void** __fastcall AlignedPointerArrayAlloc(int count) {
    auto* block = static_cast<std::uint8_t*>(gamecrt::operator_new(static_cast<std::size_t>(count) * 4 + 0x24));
    auto aligned = (reinterpret_cast<std::uintptr_t>(block) + 0x23) & ~std::uintptr_t{0x1F};
    reinterpret_cast<std::uint8_t**>(aligned)[-1] = block;
    return reinterpret_cast<void**>(aligned);
}
EI_REPLACE(0x0047B940, AlignedPointerArrayAlloc);

void AlignedObjectArray::Clear() {
    constexpr int kSlotRelease = 4;
    // The size is read again after every call (an element's slot 4 may change the array).
    void** element = data;
    for (int i = 0; i < size; ++i) ei::VCall<void>(*element++, kSlotRelease);
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
    value = 0;
    dirty = 0;
    out = nullptr;
    sink = nullptr;
    unknown_2824 = 0;
    unknown_281C = -1;
    unknown_2818 = 0;
    windowSize = 0x400;
    std::memset(table, 0, sizeof(table));
    window[0] = 0;
    window[0x400] = 0;
    window[0x3FF] = 0;
    window[0x7FF] = 0;
    return this;
}
EI_REPLACE_METHOD(0x0042FCA0, CoderState::Construct);
