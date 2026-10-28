// The quaternion product (__fastcall: out in ECX, a in EDX, b on the stack):
// out = a * b (Hamilton) the skin's animation nodes compose their key rotations with
// and the hero preview. All four components are computed
// before any is stored, so `out` may be either input. Returns out. See docs/notes/map_objects.md.
#include "maths.h"

#include "lib/vecmath.h"

Quaternion* __fastcall MultiplyQuaternions(Quaternion* out, const Quaternion* a, const Quaternion* b) {
    Vector3 av = {a->x, a->y, a->z};
    Vector3 bv = {b->x, b->y, b->z};
    Vector3 cross = Cross(av, bv);   // as the game does
    float x = (cross.x + a->w * b->x) + b->w * a->x;
    float y = (cross.y + b->y * a->w) + a->y * b->w;
    float z = (cross.z + a->w * b->z) + a->z * b->w;
    float w = a->w * b->w - DotZYX(av, bv);
    out->w = w;
    out->x = x;
    out->y = y;
    out->z = z;
    return out;
}
