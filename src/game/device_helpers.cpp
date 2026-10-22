// Small out-of-line helpers of the old renderer's range (A-S11), a few instructions each: counted
// holders (destructors, assignments), 32-byte aligned allocations, list iterator copies, deleting
// destructors, the device's vertex-buffer batch (never used) and the axis swap matrix. None of them
// touches DirectDraw or Direct3D except the COM pointer's Release. Most are byte-identical to
// copies rewritten elsewhere; each says which. See docs/notes/renderer.md (section 12).
#include "clip.h"            // TLVertex
#include "core_refcount.h"
#include "engine_object.h"
#include "model.h"           // CTransformPipeline
#include "texture.h"
#include "ui_widescreen.h"

#include <unknwn.h>

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/vecmath.h"

#include <cstdint>
#include <cstring>

// ---- Counted holders ------------------------------------------------------------------------------

// (HeldEngineObject::Assign, 004EBE60: refptr_copies.cpp.)
EI_REWRITTEN(0x004EC790, lib::AlignedArray<TLVertex>::SetSize);

// ---- Deleting destructors ---------------------------------------------------------------------------

// The renderer's counted base (texture.h): nothing to destroy. 004E7800 (RET 4), its deleting destructor, is
// reached by no object.
CRendererBase::~CRendererBase() = default;
EI_REWRITTEN_SPECIAL(0x004E7800, CRendererBase::~CRendererBase);

void* CRendererBase::operator new(std::size_t size) noexcept { return gamecrt::operator_new(size); }
void CRendererBase::operator delete(void* block) { gamecrt::operator_delete(block); }

// (The figure loader's fieldless abstract base, 0073D824, is not modelled: its deleting destructor 004F6A00 is
// reached by no object.)

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
EI_REWRITTEN(0x004F1330, AxisSwapMatrix);
