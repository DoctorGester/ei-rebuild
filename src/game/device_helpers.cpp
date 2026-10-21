// Small out-of-line helpers of the old renderer's range (A-S11), a few instructions each: counted
// holders (destructors, assignments), 32-byte aligned allocations, list iterator copies, deleting
// destructors, the device's vertex-buffer batch (never used) and the axis swap matrix. None of them
// touches DirectDraw or Direct3D except the COM pointer's Release. Most are byte-identical to
// copies rewritten elsewhere; each says which. See docs/notes/renderer.md (section 12).
#include "clip.h"            // TLVertex
#include "engine_object.h"
#include "model.h"           // CTransformPipeline
#include "texture.h"
#include "ui_widescreen.h"

#include <unknwn.h>

#include "ei/hook.h"
#include "ei/rebirth.h"
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

// 004EBE60 (RET 4; byte-identical to 00429B50 and 0056F1A0, a particle source's): a counted engine
// object pointer (a texture) takes
// `value`: counted first, then the old one released through its slot 4.
void __fastcall TexturePointerAssign(CEngineObject** pointer, void*, CEngineObject* value) {
    CEngineObject* previous = *pointer;
    *pointer = value;
    if (value) ++value->refCount_;
    if (previous) previous->Release();
}

// ---- Deleting destructors ---------------------------------------------------------------------------

// 004EF790 (RET 4): CDXSurface's (vtable 0073D774, slot 0): the destructor body 004EFD20, then the
// block freed when bit 0 of `flags` is set. Returns the object.
void __fastcall DXSurfaceDestroy(CDXSurface* holder);   // dx_surface.cpp
CDXSurface* CDXSurface::DeleteScalar(unsigned flags) {
    DXSurfaceDestroy(this);
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x004EF790, CDXSurface::DeleteScalar);

// 004E7800 (RET 4): the renderer's counted base (vtable 0073D768, slot 0): nothing to destroy. No
// object keeps this vtable while it can be released, so nothing calls it.
struct CRendererBase {
    void* DeleteScalar(unsigned flags);
};
void* CRendererBase::DeleteScalar(unsigned flags) {
    SetRendererBaseVtable(this);
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x004E7800, CRendererBase::DeleteScalar);

// 004F6A00 (RET 4): the figure loader's base class (vtable 0073D824, slot 0; the constructor
// 004F68C0 stores it before the loader's own): the root's vtable left (004F69F0, inlined here),
// then the block freed. Nothing calls it either.
struct CLoaderBase {
    void* DeleteScalar(unsigned flags);
};
void* CLoaderBase::DeleteScalar(unsigned flags) {
    SetRootVtable(this);
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x004F6A00, CLoaderBase::DeleteScalar);

// ---- Matrices ---------------------------------------------------------------------------------------

// 004F1330 (stdcall, RET 4): the axis swap of the projection: x, -y, z, w on the diagonal. Returns
// the matrix. With Rebirth's WideScreen (addon's 19 bytes at 004F135A) x is scaled by width43 /
// width, so the 4:3 picture keeps its proportions on the wide screen (widescreen::AxisScale);
// renderer_init.cpp calls it through its address (rule 9).
Matrix4* __stdcall AxisSwapMatrix(Matrix4* out) {
    std::memset(out, 0, sizeof(*out));
    out->m[15] = 1.0f;
    out->m[10] = 1.0f;
    out->m[0] = widescreen::AxisScale();
    out->m[5] = -1.0f;
    return out;
}
EI_REPLACE(0x004F1330, AxisSwapMatrix);
