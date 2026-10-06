// Small slots of a 3D object's side half (CObject3D + 0x94): the client half's extra scale and
// part materials (CObject3DClientSpecific, 0051AD20 / 0051C970) and the server half's scale
// (CObject3DServer, 0051AC30). See map_object_shadow.h and map_object_figures.h.
#include "map_object_shadow.h"

#include "map_object_figures.h"
#include "world.h"

#include "ei/hook.h"
#include "originals_render.h"

void CObject3DClientSpecific::ExtraScale(float* x, float* y, float* z) {
    *x = figureScale[0];
    *y = figureScale[1];
    *z = figureScale[2];
}
EI_REPLACE_METHOD(0x0051AD20, CObject3DClientSpecific::ExtraScale);

void CObject3DClientSpecific::RegisterPartMaterials() {
    void* materials = static_cast<CWorld*>(ManagerContext())->MaterialManager();
    if (drawnPartCount <= 0) return;
    drawnPartCursor = 0;
    for (FigurePart* part = drawnParts[0]; part;) {
        MaterialRegisterOriginal(materials, nullptr, part, static_cast<std::uint32_t>(part->type_));
        ++drawnPartCursor;
        if (drawnPartCount <= drawnPartCursor) break;
        part = drawnParts[drawnPartCursor];
    }
}
EI_REPLACE_METHOD(0x0051C970, CObject3DClientSpecific::RegisterPartMaterials);

namespace {
constexpr float kNoScale = 1.0f;
} // namespace

// 0051AC30. CObject3DServer's slot 18 (thiscall, RET 0xC; `this` unused): the server draws
// nothing, so the extra scale is 1, 1, 1.
void __fastcall ServerExtraScale(void*, void*, float* x, float* y, float* z) {
    *x = kNoScale;
    *y = kNoScale;
    *z = kNoScale;
}
EI_REPLACE(0x0051AC30, ServerExtraScale);
