// Checks for the battle HUD's unit info texts (ui_hud_unit_info.cpp).
#include "verify.h"

#include "../../src/game/ui.h"
#include "../../src/game/unit.h"

#include <cstdint>
#include <cstring>

void __fastcall DrawUnitInfo(CHudUnitPanel* self, void*, CUnit* unit);

// 00647040 only draws text: ours only. The unit's values it reads (health and mana, packedVitals
// +0x17C..+0x186) and the panel's surface and label table stay as they were.
static void* s_unitInfo;
static void __fastcall DrawUnitInfoVerify(CHudUnitPanel* self, void*, CUnit* unit) {
    std::uint8_t before[10];
    std::memcpy(before, &unit->packedVitals, sizeof(before));
    void* surface = self->surfaces_.data;
    mfc::CString* labels = self->texts_.data;
    DrawUnitInfo(self, nullptr, unit);
    bool ok = std::memcmp(before, &unit->packedVitals, sizeof(before)) == 0 && surface == self->surfaces_.data &&
              labels == self->texts_.data;
    ei::verify::Check("DrawUnitInfo", true, ok, "unit %p", static_cast<void*>(unit));
}
EI_VERIFY(0x00647040, DrawUnitInfoVerify, 0, s_unitInfo);
