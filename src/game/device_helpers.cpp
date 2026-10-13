// Small out-of-line helpers of the old renderer's range (A-S11), a few instructions each: counted
// holders (destructors, assignments), 32-byte aligned allocations, list iterator copies, deleting
// destructors, the device's vertex-buffer batch (never used) and the axis swap matrix. None of them
// touches DirectDraw or Direct3D except the COM pointer's Release. Most are byte-identical to
// copies rewritten elsewhere; each says which. See docs/notes/renderer.md (section 12).
#include "clip.h"            // TLVertex
#include "engine_object.h"
#include "model.h"           // CTransformPipeline
#include "texture.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/vecmath.h"

#include <cstdint>
#include <cstring>

namespace {

// The vtables the destructors leave: the engine's counted root (0073C368) and the renderer's own
// counted base (0073D768: CDXSurface, the device object and the pipeline derive from it).
void SetRootVtable(void* object) { ei::SetVtable(object, 0x0073C368); }
void SetRendererBaseVtable(void* object) { ei::SetVtable(object, 0x0073D768); }

} // namespace

// ---- Counted holders ------------------------------------------------------------------------------

// 004E6BA0: one reference less on a renderer object (the count at +4); the deleting destructor
// (slot 0, with 1) at zero. Surface holders mostly; the device object and the pipeline have the
// same header. (Byte-identical to 00457720.)
void __fastcall SurfaceHolderRelease(CDXSurface* holder) { holder->Release(); }
EI_REPLACE(0x004E6BA0, SurfaceHolderRelease);

// 004EBBB0, 004EBBD0, 004EBDF0, 004EBE10, 004FAED0, 004FAEF0, 004FDF70 (byte-identical to 00504E00):
// the destructor of a counted pointer to such an object: released, the pointer left as it is.
// Unwind funclets call them, and 004E72A0 / 004F77C0 for a local.
void __fastcall SurfacePointerRelease(CDXSurface** pointer) {
    if (*pointer) (*pointer)->Release();
}
EI_REPLACE(0x004EBBB0, SurfacePointerRelease);
EI_REPLACE(0x004EBBD0, SurfacePointerRelease);
EI_REPLACE(0x004EBDF0, SurfacePointerRelease);
EI_REPLACE(0x004EBE10, SurfacePointerRelease);
EI_REPLACE(0x004FAED0, SurfacePointerRelease);
EI_REPLACE(0x004FAEF0, SurfacePointerRelease);
EI_REPLACE(0x004FDF70, SurfacePointerRelease);

// 004EBE30 (RET 4; byte-identical to 00519580): a counted surface pointer takes `value`: the new
// one counted first, then the old one released.
void __fastcall SurfacePointerAssign(CDXSurface** pointer, void*, CDXSurface* value) {
    CDXSurface* previous = *pointer;
    *pointer = value;
    if (value) ++value->refCount;
    if (previous) previous->Release();
}
EI_REPLACE(0x004EBE30, SurfacePointerAssign);

// 004EBE80 (RET 4): the same code for the device's pipeline pointer (+0xAC).
void __fastcall PipelinePointerAssign(CTransformPipeline** pointer, void*, CTransformPipeline* value) {
    CTransformPipeline* previous = *pointer;
    *pointer = value;
    if (value) ++value->refs_;
    if (previous) previous->Release();
}
EI_REPLACE(0x004EBE80, PipelinePointerAssign);

// 004EBE60 (RET 4; byte-identical to 00429B50): a counted engine object pointer (a texture) takes
// `value`: counted first, then the old one released through its slot 4.
void __fastcall TexturePointerAssign(CEngineObject** pointer, void*, CEngineObject* value) {
    CEngineObject* previous = *pointer;
    *pointer = value;
    if (value) ++value->refCount_;
    if (previous) previous->Release();
}
EI_REPLACE(0x004EBE60, TexturePointerAssign);

// 004C17E0, 004D9E60, 004D9E70 (byte-identical to 00421350): the destructor of such a pointer: the
// object released through slot 4. Unwind funclets only.
void __fastcall EngineObjectPointerRelease(CEngineObject** pointer) {
    if (*pointer) (*pointer)->Release();
}
EI_REPLACE(0x004C17E0, EngineObjectPointerRelease);
EI_REPLACE(0x004D9E60, EngineObjectPointerRelease);
EI_REPLACE(0x004D9E70, EngineObjectPointerRelease);

// 004FDED0 (byte-identical to 00458540): a COM pointer's destructor (IUnknown::Release, slot 2).
// Unwind funclets of the state block manager's constructor only.
void __fastcall ComPointerDestroy(void** pointer) {
    void* object = *pointer;
    if (!object) return;
    using Release = unsigned long(__stdcall*)(void*);
    reinterpret_cast<Release>((*static_cast<void***>(object))[2])(object);
}
EI_REPLACE(0x004FDED0, ComPointerDestroy);

// ---- List iterators --------------------------------------------------------------------------------

// 004EBCA0, 004EC1A0, 004F0510, 004F0590, 004F05A0 (RET 4; byte-identical to 004E1540): an STLport
// list iterator (one node pointer) copied into `out`; returns `out`. The device's lists (texture
// stages, depth formats) and the driver list's.
void** __fastcall ListIteratorCopy(void** iterator, void*, void** out) {
    *out = *iterator;
    return out;
}
EI_REPLACE(0x004EBCA0, ListIteratorCopy);
EI_REPLACE(0x004EC1A0, ListIteratorCopy);
EI_REPLACE(0x004F0510, ListIteratorCopy);
EI_REPLACE(0x004F0590, ListIteratorCopy);
EI_REPLACE(0x004F05A0, ListIteratorCopy);

// ---- Aligned blocks --------------------------------------------------------------------------------

// 004ECB50 (the count in ECX): a 32-byte aligned block of `bytes` bytes (the texture code's and
// the mesh draws' byte buffers).
std::uint8_t* __fastcall AllocateAlignedBytes(int bytes) { return lib::AlignedArray<std::uint8_t>::Allocate(bytes); }
EI_REPLACE(0x004ECB50, AllocateAlignedBytes);

// 004ECB70 (the count in ECX; byte-identical to 00452DA0 and 004E5710): the same for `count` TL
// vertices.
TLVertex* __fastcall AllocateVertices(int count) { return lib::AlignedArray<TLVertex>::Allocate(count); }
EI_REPLACE(0x004ECB70, AllocateVertices);

// 004EC790 (RET 8; byte-identical to 00504F70): the TL vertex array's SetSize(count, growBy).
EI_REPLACE_METHOD(0x004EC790, lib::AlignedArray<TLVertex>::SetSize);

// 004D9EB0 (byte-identical to 0044F1B0): an aligned array's destructor: the block freed, the
// fields left as they were. Unwind funclets only.
void __fastcall AlignedArrayDestroy(lib::AlignedArray<std::uint8_t>* array) { array->FreeData(); }
EI_REPLACE(0x004D9EB0, AlignedArrayDestroy);

// ---- The device's vertex-buffer batch ---------------------------------------------------------------

// 004E7820 (RET 4): no vertex buffer for the batch, whatever the vertex count: the mesh draws
// (004E9010, 004E9A50, 004EA4A0) take the null and draw their own vertices.
void* CTextureDevice::LockVertexBuffer(int) { return nullptr; }
EI_REPLACE_METHOD(0x004E7820, CTextureDevice::LockVertexBuffer);

// 004E7E00 (RET 8). Original details kept: with a base vertex the loop runs to the end pointer
// inclusive (one index more than `count` is read and written); the indices are written past the
// array's size before it grows, so a reallocation would drop them.
void CTextureDevice::AppendBatchIndices(const std::uint16_t* indices, int count) {
    std::uint16_t* out = batchIndices_.data + batchIndices_.size;
    if (batchBaseVertex_ != 0) {
        for (const std::uint16_t* in = indices; in <= indices + count; ++in)
            *out++ = static_cast<std::uint16_t>(*in + static_cast<std::uint16_t>(batchBaseVertex_));
    } else {
        std::memcpy(out, indices, static_cast<std::size_t>(count) * sizeof(std::uint16_t));
    }
    batchIndices_.SetSize(batchIndices_.size + count);
}
EI_REPLACE_METHOD(0x004E7E00, CTextureDevice::AppendBatchIndices);

// ---- Deleting destructors ---------------------------------------------------------------------------

// 004EF790 (RET 4): CDXSurface's (vtable 0073D774, slot 0): the destructor body 004EFD20, then the
// block freed when bit 0 of `flags` is set. Returns the object.
void __fastcall DXSurfaceDestroy(CDXSurface* holder);   // dx_surface.cpp
CDXSurface* __fastcall DXSurfaceDelete(CDXSurface* holder, void*, unsigned flags) {
    DXSurfaceDestroy(holder);
    if (flags & 1) gamecrt::operator_delete(holder);
    return holder;
}
EI_REPLACE(0x004EF790, DXSurfaceDelete);

// 004E7800 (RET 4): the renderer's counted base (vtable 0073D768, slot 0): nothing to destroy. No
// object keeps this vtable while it can be released, so nothing calls it.
void* __fastcall RendererBaseDelete(void* object, void*, unsigned flags) {
    SetRendererBaseVtable(object);
    if (flags & 1) gamecrt::operator_delete(object);
    return object;
}
EI_REPLACE(0x004E7800, RendererBaseDelete);

// 004F6A00 (RET 4): the figure loader's base class (vtable 0073D824, slot 0; the constructor
// 004F68C0 stores it before the loader's own): the root's vtable left (004F69F0, inlined here),
// then the block freed. Nothing calls it either.
void* __fastcall LoaderBaseDelete(void* object, void*, unsigned flags) {
    SetRootVtable(object);
    if (flags & 1) gamecrt::operator_delete(object);
    return object;
}
EI_REPLACE(0x004F6A00, LoaderBaseDelete);

// ---- Matrices ---------------------------------------------------------------------------------------

// 004F1330 (stdcall, RET 4): the axis swap of the projection: x, -y, z, w on the diagonal. Returns
// the matrix. Rebirth patches the function's tail (WideScreen), so this is the vanilla function,
// hooked in the vanilla launch only; renderer_init.cpp calls it through its address (rule 9).
Matrix4* __stdcall AxisSwapMatrix(Matrix4* out) {
    std::memset(out, 0, sizeof(*out));
    out->m[15] = 1.0f;
    out->m[10] = 1.0f;
    out->m[0] = 1.0f;
    out->m[5] = -1.0f;
    return out;
}
EI_REPLACE(0x004F1330, AxisSwapMatrix);
