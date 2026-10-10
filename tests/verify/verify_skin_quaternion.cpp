// Checks for skin_quaternion.cpp (A-U10). 00506C70 is pure: the original writes into a scratch
// quaternion (the inputs untouched, `out` may alias them), ours into `out`; the four floats
// compared within 4 ULPs and the result pointer. Reached by the skin's animation nodes (the fake
// units' skeletons: units, models scenarios) and the hero preview (ui scenarios).
#include "verify.h"

#include "../../src/game/maths.h"

Quaternion* __fastcall MultiplyQuaternions(Quaternion* out, const Quaternion* a, const Quaternion* b);

// 00506C70: PUSH ESI; MOV ESI,[ESP+8] (5 bytes).
static Quaternion*(__fastcall* s_multiply)(Quaternion*, const Quaternion*, const Quaternion*);
static Quaternion* __fastcall MultiplyVerify(Quaternion* out, const Quaternion* a, const Quaternion* b) {
    Quaternion expected;
    s_multiply(&expected, a, b);
    Quaternion* result = MultiplyQuaternions(out, a, b);
    ei::verify::CheckNear("MultiplyQuaternions w", expected.w, out->w, "");
    ei::verify::CheckNear("MultiplyQuaternions x", expected.x, out->x, "");
    ei::verify::CheckNear("MultiplyQuaternions y", expected.y, out->y, "");
    ei::verify::CheckNear("MultiplyQuaternions z", expected.z, out->z, "");
    ei::verify::Check("MultiplyQuaternions result", static_cast<void*>(out), static_cast<void*>(result), "");
    return result;
}
EI_VERIFY(0x00506C70, MultiplyVerify, 5, s_multiply);
