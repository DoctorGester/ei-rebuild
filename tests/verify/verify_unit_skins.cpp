// Checks for the zone's unit skins (unit_skins.cpp).
#include "verify.h"

#include <cstdint>

class CWorldClient;
void __fastcall BuildUnitSkins(CWorldClient* self, void*, const void* othersVector, const void* dressedVector);

// 00568800 builds textures and figures (texture cache, figure maker): ours only. Checked: the two
// record vectors are left as they were.
static void* s_skins;
static void __fastcall BuildUnitSkinsVerify(CWorldClient* self, void*, const void* othersVector, const void* dressedVector) {
    const auto* others = static_cast<const char* const*>(othersVector);
    const auto* dressed = static_cast<const char* const*>(dressedVector);
    const char* before[4] = {others[0], others[1], dressed[0], dressed[1]};
    BuildUnitSkins(self, nullptr, othersVector, dressedVector);
    bool same = before[0] == others[0] && before[1] == others[1] && before[2] == dressed[0] && before[3] == dressed[1];
    ei::verify::Check("BuildUnitSkins", true, same, "dressed %d others %d",
                      static_cast<int>((dressed[1] - dressed[0]) / 0x24), static_cast<int>((others[1] - others[0]) / 0x3C));
}
EI_VERIFY(0x00568800, BuildUnitSkinsVerify, 0, s_skins);
