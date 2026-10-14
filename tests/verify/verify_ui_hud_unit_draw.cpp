// Check for the battle HUD's unit panel draw (ui_hud_unit_draw.cpp).
//
// 006462A0, ours only: it draws through Direct3D and steps the model's animation, so two runs
// cannot be compared. What it must leave: 1 in EAX; the shown unit's position and orientation as
// they were (put back from the copies taken before the model was placed in the panel), the
// "interface draws it" flag 0x800 clear, its figure's extra scale back at 1 when the model was
// drawn; texture stage 0's mip filter as it was; the panel offset 0 (WideScreen, W3: the draw
// sets it and clears it before the base draw). Rebirth-patched for WideScreen, which the rewrite
// reproduces behind the flag: hooked in a Rebirth launch too (every one is wide here).
#include "verify.h"

#include "../../src/game/device.h"
#include "../../src/game/map_object_figures.h"
#include "../../src/game/map_object_shadow.h"
#include "../../src/game/texture.h"
#include "../../src/game/ui.h"
#include "../../src/game/ui_widescreen.h"
#include "../../src/game/unit.h"

#include <d3d.h>

#include <cstdint>
#include <cstring>

namespace {

DWORD MipFilter() {
    DWORD value = 0;
    CTextureDevice* device = RenderDeviceObject();
    if (device && device->d3dDevice_)
        device::GetTextureStageState(device::D3D(device), 0, D3DTSS_MIPFILTER, &value);
    return value;
}

void* s_unused;
BOOL __fastcall UnitPanelDrawVerify(CHudUnitPanel* self, void*, int pass) {
    // The unit the draw will show (the hover unit, else the selected one; none when slid away).
    CUnit* unit = self->slide_ < 1.0f ? (self->hoverUnit_ ? self->hoverUnit_ : self->shownUnit_) : nullptr;
    if (unit && !unit->Object3D()->attached) unit = nullptr;
    Vector3 position = {};
    Quaternion orientation = {};
    if (unit) {
        position = unit->Object3D()->position;
        orientation = unit->Object3D()->orientation;
    }
    const DWORD filter = MipFilter();
    const bool drawn = unit && self->bodyPart_ != 2;

    BOOL result = self->DrawVanilla(pass);

    bool kept = true;
    bool scaleBack = true;
    bool flagClear = true;
    if (unit) {
        kept = std::memcmp(&position, &unit->Object3D()->position, sizeof(position)) == 0 &&
               std::memcmp(&orientation, &unit->Object3D()->orientation, sizeof(orientation)) == 0;
        flagClear = (unit->flags_ & 0x800) == 0;
        if (drawn) {
            const Vector3& scale = static_cast<CObject3DClientSpecific*>(unit->Object3D()->attached)->figureScale;
            scaleBack = scale.x == 1.0f && scale.y == 1.0f && scale.z == 1.0f;
        }
    }
    const DWORD filterAfter = MipFilter();
    const bool ok = result == 1 && kept && scaleBack && flagClear && filter == filterAfter && widescreen::PanelOffset() == 0;
    ei::verify::Check("CHudUnitPanel::DrawVanilla", true, ok,
                      "result %d, unit %p page %d: transform kept %d, scale back %d, flag clear %d, mip filter %lu -> %lu, "
                      "panel offset %d",
                      result, static_cast<void*>(unit), self->bodyPart_, kept, scaleBack, flagClear, filter, filterAfter,
                      widescreen::PanelOffset());
    return result;
}
EI_VERIFY(0x006462A0, UnitPanelDrawVerify, 0, s_unused);

} // namespace
