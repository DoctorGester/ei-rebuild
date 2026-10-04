// Checks for the battle HUD's unit info texts (ui_hud_unit_info.cpp).
#include "verify.h"

#include <cstdint>
#include <cstring>

void __fastcall DrawUnitInfo(char* self, void*, char* unit);

// 00647040 only draws text: ours only. The unit's values it reads (health and mana, +0x17C..+0x186)
// and the panel's surface and label table stay as they were.
static void* s_unitInfo;
static void __fastcall DrawUnitInfoVerify(char* self, void*, char* unit) {
    std::uint8_t before[10];
    std::memcpy(before, unit + 0x17C, sizeof(before));
    void* surface = *reinterpret_cast<void**>(self + 0x118);
    void* labels = *reinterpret_cast<void**>(self + 0x1A0);
    DrawUnitInfo(self, nullptr, unit);
    bool ok = std::memcmp(before, unit + 0x17C, sizeof(before)) == 0 && surface == *reinterpret_cast<void**>(self + 0x118) &&
              labels == *reinterpret_cast<void**>(self + 0x1A0);
    ei::verify::Check("DrawUnitInfo", true, ok, "unit %p", static_cast<void*>(unit));
}
EI_VERIFY(0x00647040, DrawUnitInfoVerify, 0, s_unitInfo);
