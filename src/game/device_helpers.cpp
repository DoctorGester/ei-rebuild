// Small out-of-line helpers of the old renderer's range, a few instructions each: counted
// holders (destructors, assignments), 32-byte aligned allocations, list iterator copies, deleting
// destructors, the device's vertex-buffer batch (never used) and the axis swap matrix. None of them
// touches DirectDraw or Direct3D except the COM pointer's Release. Most are identical to
// copies written elsewhere; each says which. See docs/notes/renderer.md (section 12).
#include "clip.h"            // TLVertex
#include "core_refcount.h"
#include "engine_object.h"
#include "model.h"           // CTransformPipeline
#include "texture.h"
#include "ui_widescreen.h"

#include <unknwn.h>

#include "ei/rebirth.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/vecmath.h"

#include <cstdint>
#include <cstring>

// ---- Deleting destructors ---------------------------------------------------------------------------

// The renderer's counted base: nothing to destroy. Its deleting destructor is reached by no object.
CRendererBase::~CRendererBase() = default;


// (The figure loader's fieldless abstract base is not modelled: its deleting destructor is reached by no object.)

// ---- Matrices ---------------------------------------------------------------------------------------

// The axis swap of the projection: x, -y, z, w on the diagonal. Returns the matrix. With Rebirth's WideScreen
// x is scaled by width43 / width, so the 4:3 picture keeps its proportions on the wide screen
// (widescreen::AxisScale); renderer_init.cpp calls it through its address (rule 9).
Matrix4* AxisSwapMatrix(Matrix4* out) {
    std::memset(out, 0, sizeof(*out));
    out->m[15] = 1.0f;
    out->m[10] = 1.0f;
    out->m[0] = widescreen::AxisScale();
    out->m[5] = -1.0f;
    return out;
}
