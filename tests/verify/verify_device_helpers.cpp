// Checks for device_helpers.cpp (A-S11). Main thread.
//  - Side by side (pure): the batch's vertex buffer lock 004E7820 (every mesh draw: any scene with
//    a figure or a shadow) and the axis swap matrix 004F1330 (the renderer's creation, every start;
//    vanilla launch only).
//  - Ours only (they move reference counts, allocate or free): the surface holder's release
//    004E6BA0 and the counted pointers' assignments 004EBE30 / 004EBE60 / 004EBE80 (texture loads
//    and material stages: any zone load), the aligned allocations 004ECB50 / 004ECB70 and the
//    vertex array's SetSize 004EC790 (clipped mesh draws: any scene), the list iterator copies
//    (the device's creation and the batch flushes), CDXSurface's deleting destructor 004EF790 (a
//    texture unloaded: leaving a zone).
//  - Ours only, and no scenario reaches them: the pointer destructors called by unwind funclets
//    only (004C17E0, 004D9E60 / 70 / B0, 004EBBB0 ... 004FDF70, 004FDED0), the batch's index
//    append 004E7E00 (its callers go there only with a locked buffer, and there never is one), the
//    two base classes' deleting destructors 004E7800 / 004F6A00.
#include "verify.h"

#include "../../src/game/clip.h"
#include "../../src/game/engine_object.h"
#include "../../src/game/model.h"
#include "../../src/game/texture.h"
#include "../../src/lib/containers.h"

#include <cstdint>
#include <cstring>

// device_helpers.cpp (no header declares the shims).
void __fastcall SurfaceHolderRelease(CDXSurface* holder);
void __fastcall SurfacePointerRelease(CDXSurface** pointer);
void __fastcall SurfacePointerAssign(CDXSurface** pointer, void*, CDXSurface* value);
void __fastcall PipelinePointerAssign(CTransformPipeline** pointer, void*, CTransformPipeline* value);
void __fastcall TexturePointerAssign(CEngineObject** pointer, void*, CEngineObject* value);
void __fastcall EngineObjectPointerRelease(CEngineObject** pointer);
void __fastcall ComPointerDestroy(void** pointer);
void** __fastcall ListIteratorCopy(void** iterator, void*, void** out);
std::uint8_t* __fastcall AllocateAlignedBytes(int bytes);
TLVertex* __fastcall AllocateVertices(int count);
void __fastcall AlignedArrayDestroy(lib::AlignedArray<std::uint8_t>* array);
CDXSurface* __fastcall DXSurfaceDelete(CDXSurface* holder, void*, unsigned flags);
void* __fastcall RendererBaseDelete(void* object, void*, unsigned flags);
void* __fastcall LoaderBaseDelete(void* object, void*, unsigned flags);
Matrix4* __stdcall AxisSwapMatrix(Matrix4* out);

namespace {
void* s_unused;

// ---- Counted holders ----

void __fastcall SurfaceHolderReleaseVerify(CDXSurface* holder) {
    int before = holder->refCount;
    SurfaceHolderRelease(holder);
    // At 1 the holder is gone.
    ei::verify::Check("surface holder release (004E6BA0)", true, before == 1 || holder->refCount == before - 1,
                      "count before %d", before);
}

void __fastcall SurfacePointerReleaseVerify(CDXSurface** pointer) {
    CDXSurface* holder = *pointer;
    SurfacePointerRelease(pointer);
    ei::verify::Check("counted pointer destructor (004EBBB0 and copies)", static_cast<void*>(holder),
                      static_cast<void*>(*pointer), "");
}

// The assignments: the pointer holds the value, which has one reference more (unless it was the
// value already: then the count went up and down, and at 1 the object would be gone).
template <typename T, typename Count>
bool Assigned(T** pointer, T* value, bool same, Count before, Count T::*count) {
    if (*pointer != value) return false;
    return !value || same || value->*count == before + 1;
}

void __fastcall SurfacePointerAssignVerify(CDXSurface** pointer, void*, CDXSurface* value) {
    bool same = *pointer == value;
    int before = value ? value->refCount : 0;
    SurfacePointerAssign(pointer, nullptr, value);
    ei::verify::Check("surface pointer assignment (004EBE30)", true,
                      Assigned(pointer, value, same, before, &CDXSurface::refCount), "count before %d", before);
}

void __fastcall PipelinePointerAssignVerify(CTransformPipeline** pointer, void*, CTransformPipeline* value) {
    bool same = *pointer == value;
    int before = value ? value->refs_ : 0;
    PipelinePointerAssign(pointer, nullptr, value);
    ei::verify::Check("pipeline pointer assignment (004EBE80)", true,
                      Assigned(pointer, value, same, before, &CTransformPipeline::refs_), "count before %d", before);
}

void __fastcall TexturePointerAssignVerify(CEngineObject** pointer, void*, CEngineObject* value) {
    bool same = *pointer == value;
    int before = value ? value->refCount_ : 0;
    TexturePointerAssign(pointer, nullptr, value);
    ei::verify::Check("texture pointer assignment (004EBE60)", true,
                      Assigned(pointer, value, same, before, &CEngineObject::refCount_), "count before %d", before);
}

void __fastcall EngineObjectPointerReleaseVerify(CEngineObject** pointer) {
    CEngineObject* object = *pointer;
    EngineObjectPointerRelease(pointer);
    ei::verify::Check("engine object pointer destructor (004C17E0 and copies)", static_cast<void*>(object),
                      static_cast<void*>(*pointer), "");
}

void __fastcall ComPointerDestroyVerify(void** pointer) {
    void* object = *pointer;
    ComPointerDestroy(pointer);
    ei::verify::Check("COM pointer destructor (004FDED0)", object, *pointer, "");
}

// ---- List iterators ----

void** __fastcall ListIteratorCopyVerify(void** iterator, void*, void** out) {
    void** result = ListIteratorCopy(iterator, nullptr, out);
    ei::verify::Check("list iterator copy (004EBCA0 and copies)", true, result == out && *out == *iterator, "");
    return result;
}

// ---- Aligned blocks ----

// A block from the aligned allocator: 32-byte aligned, the allocation's own pointer just before it.
bool AlignedBlock(const void* data) {
    auto aligned = reinterpret_cast<std::uintptr_t>(data);
    auto block = reinterpret_cast<std::uintptr_t>(static_cast<void* const*>(data)[-1]);
    return (aligned & 0x1F) == 0 && aligned - block >= 4 && aligned - block <= 0x23;
}

std::uint8_t* __fastcall AllocateAlignedBytesVerify(int bytes) {
    std::uint8_t* result = AllocateAlignedBytes(bytes);
    ei::verify::Check("aligned byte block (004ECB50)", true, AlignedBlock(result), "bytes %d", bytes);
    return result;
}

TLVertex* __fastcall AllocateVerticesVerify(int count) {
    TLVertex* result = AllocateVertices(count);
    ei::verify::Check("aligned vertex block (004ECB70)", true, AlignedBlock(result), "count %d", count);
    return result;
}

void __fastcall VerticesSetSizeVerify(lib::AlignedArray<TLVertex>* array, void*, int count, int growBy) {
    int step = array->growBy;
    array->SetSize(count, growBy);
    bool good = array->size == count && array->capacity >= count && (count == 0) == (array->data == nullptr) &&
                array->growBy == (growBy == -1 ? step : growBy) && (count == 0 || AlignedBlock(array->data));
    ei::verify::Check("AlignedArray<TLVertex>::SetSize (004EC790)", true, good, "count %d size %d capacity %d", count,
                      array->size, array->capacity);
}

void __fastcall AlignedArrayDestroyVerify(lib::AlignedArray<std::uint8_t>* array) {
    lib::AlignedArray<std::uint8_t> before = *array;
    AlignedArrayDestroy(array);
    ei::verify::Check("aligned array destructor (004D9EB0)", true, std::memcmp(&before, array, sizeof(before)) == 0, "");
}

// ---- The vertex-buffer batch ----

// 004E7820: XOR EAX,EAX (2), RET 4 (3): the whole function.
void*(__fastcall* s_lockVertexBuffer)(CTextureDevice*, void*, int);
void* __fastcall LockVertexBufferVerify(CTextureDevice* self, void*, int count) {
    void* expected = s_lockVertexBuffer(self, nullptr, count);
    void* result = self->LockVertexBuffer(count);
    ei::verify::Check("CTextureDevice::LockVertexBuffer (004E7820)", expected, result, "count %d", count);
    return result;
}

void __fastcall AppendBatchIndicesVerify(CTextureDevice* self, void*, const std::uint16_t* indices, int count) {
    int before = self->batchIndices_.size;
    self->AppendBatchIndices(indices, count);
    ei::verify::Check("CTextureDevice::AppendBatchIndices (004E7E00)", before + count, self->batchIndices_.size,
                      "count %d", count);
}

// ---- Deleting destructors ----

CDXSurface* __fastcall DXSurfaceDeleteVerify(CDXSurface* holder, void*, unsigned flags) {
    CDXSurface* result = DXSurfaceDelete(holder, nullptr, flags);
    ei::verify::Check("CDXSurface deleting destructor (004EF790)", static_cast<void*>(holder),
                      static_cast<void*>(result), "flags %u", flags);
    return result;
}

void* __fastcall RendererBaseDeleteVerify(void* object, void*, unsigned flags) {
    void* result = RendererBaseDelete(object, nullptr, flags);
    ei::verify::Check("renderer base deleting destructor (004E7800)", object, result, "flags %u", flags);
    return result;
}

void* __fastcall LoaderBaseDeleteVerify(void* object, void*, unsigned flags) {
    void* result = LoaderBaseDelete(object, nullptr, flags);
    ei::verify::Check("loader base deleting destructor (004F6A00)", object, result, "flags %u", flags);
    return result;
}

// ---- Matrices ----

// 004F1330: PUSH EBP (1), MOV EBP,ESP (2), PUSH ECX (1), PUSH EDI (1).
Matrix4*(__stdcall* s_axisSwap)(Matrix4*);
Matrix4* __stdcall AxisSwapMatrixVerify(Matrix4* out) {
    Matrix4 expected;
    s_axisSwap(&expected);
    Matrix4* result = AxisSwapMatrix(out);
    ei::verify::Check("axis swap matrix (004F1330)", true,
                      result == out && std::memcmp(&expected, out, sizeof(expected)) == 0, "");
    return result;
}

} // namespace

EI_VERIFY(0x004E6BA0, SurfaceHolderReleaseVerify, 0, s_unused);
EI_VERIFY(0x004EBBB0, SurfacePointerReleaseVerify, 0, s_unused);
EI_VERIFY(0x004EBBD0, SurfacePointerReleaseVerify, 0, s_unused);
EI_VERIFY(0x004EBDF0, SurfacePointerReleaseVerify, 0, s_unused);
EI_VERIFY(0x004EBE10, SurfacePointerReleaseVerify, 0, s_unused);
EI_VERIFY(0x004FAED0, SurfacePointerReleaseVerify, 0, s_unused);
EI_VERIFY(0x004FAEF0, SurfacePointerReleaseVerify, 0, s_unused);
EI_VERIFY(0x004FDF70, SurfacePointerReleaseVerify, 0, s_unused);
EI_VERIFY(0x004EBE30, SurfacePointerAssignVerify, 0, s_unused);
EI_VERIFY(0x004EBE80, PipelinePointerAssignVerify, 0, s_unused);
EI_VERIFY(0x004EBE60, TexturePointerAssignVerify, 0, s_unused);
EI_VERIFY(0x004C17E0, EngineObjectPointerReleaseVerify, 0, s_unused);
EI_VERIFY(0x004D9E60, EngineObjectPointerReleaseVerify, 0, s_unused);
EI_VERIFY(0x004D9E70, EngineObjectPointerReleaseVerify, 0, s_unused);
EI_VERIFY(0x004FDED0, ComPointerDestroyVerify, 0, s_unused);
EI_VERIFY(0x004EBCA0, ListIteratorCopyVerify, 0, s_unused);
EI_VERIFY(0x004EC1A0, ListIteratorCopyVerify, 0, s_unused);
EI_VERIFY(0x004F0510, ListIteratorCopyVerify, 0, s_unused);
EI_VERIFY(0x004F0590, ListIteratorCopyVerify, 0, s_unused);
EI_VERIFY(0x004F05A0, ListIteratorCopyVerify, 0, s_unused);
EI_VERIFY(0x004ECB50, AllocateAlignedBytesVerify, 0, s_unused);
EI_VERIFY(0x004ECB70, AllocateVerticesVerify, 0, s_unused);
EI_VERIFY(0x004EC790, VerticesSetSizeVerify, 0, s_unused);
EI_VERIFY(0x004D9EB0, AlignedArrayDestroyVerify, 0, s_unused);
EI_VERIFY(0x004E7820, LockVertexBufferVerify, 5, s_lockVertexBuffer);
EI_VERIFY(0x004E7E00, AppendBatchIndicesVerify, 0, s_unused);
EI_VERIFY(0x004EF790, DXSurfaceDeleteVerify, 0, s_unused);
EI_VERIFY(0x004E7800, RendererBaseDeleteVerify, 0, s_unused);
EI_VERIFY(0x004F6A00, LoaderBaseDeleteVerify, 0, s_unused);
EI_VERIFY(0x004F1330, AxisSwapMatrixVerify, 5, s_axisSwap);
