// Checks for object3d_figure_setup.cpp (A-U4).
// - SetUpAnimatedFigure (005179E0): ours only (it loads the model, plays and releases the animation
//   state's parts: a second run would find no parts). Checked: one box and one part name per part
//   the state animated before, every box flagged 4, the state's part list empty, both bounds flags
//   set, and the bounds recomputed here from the boxes (heroes: x / y -0.5..0.5) agree (CheckNear).
// Reached by: the zone loader (005927C0) for every animated object and unit of a zone, so every
// scenario that starts a game (units, unit_create, world_load); levers (Lever::SetState) too.
#include "verify.h"

#include "../../src/game/ai_map.h"
#include "../../src/game/map_object_figures.h"

#include <cfloat>
#include <cmath>

namespace {
void* s_unusedSetUp;

bool Near(float a, float b) { return ei::verify::NearlyEqual(a, b, 8, 1e-5f); }
} // namespace

static void __fastcall SetUpAnimatedFigureVerify(CObject3D* self, void*, const char* model) {
    int parts = self->animation ? self->animation->parts.count : 0;
    bool hadAnimation = self->animation != nullptr;
    self->SetUpAnimatedFigure(model);

    bool ok = (self->boundsFlags & 3) == 3;
    if (hadAnimation) {
        ok = ok && self->partBoxCount == parts && self->partNameCount == parts && self->animation->parts.count == 0 &&
             !self->animation->parts.head;
    }
    float high[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
    float low[3] = {FLT_MAX, FLT_MAX, FLT_MAX};
    if (hadAnimation && ok) {
        for (int i = 0; i < parts; ++i) {
            const FootprintPart& box = self->partBoxes[i];
            ok = ok && (box.flags & 4) != 0;
            const float pivot[3] = {box.pivot.x, box.pivot.y, box.pivot.z};
            const float mn[3] = {box.boxMin.x, box.boxMin.y, box.boxMin.z};
            const float mx[3] = {box.boxMax.x, box.boxMax.y, box.boxMax.z};
            const float off[3] = {box.offset.x, box.offset.y, box.offset.z};
            for (int k = 0; k < 3; ++k) {
                float h = pivot[k] + mx[k] + off[k];
                float l = pivot[k] + mn[k] + off[k];
                if (!(high[k] >= h)) high[k] = h;
                if (!(l >= low[k])) low[k] = l;
            }
        }
    }
    if (self->type_ == 0x32) {
        high[0] = high[1] = 0.5f;
        low[0] = low[1] = -0.5f;
    }
    for (int k = 0; ok && k < 3; ++k) {
        float c = (low[k] + high[k]) * 0.5f;
        ok = Near(c, self->center[k]) && Near(low[k] - c, self->below[k]) && Near(high[k] - c, self->above[k]);
    }
    ei::verify::Check("CObject3D::SetUpAnimatedFigure", true, ok, "%s: %d parts, centre %g %g %g", model ? model : "-",
                      parts, self->center[0], self->center[1], self->center[2]);
}
EI_VERIFY(0x005179E0, SetUpAnimatedFigureVerify, 0, s_unusedSetUp);
