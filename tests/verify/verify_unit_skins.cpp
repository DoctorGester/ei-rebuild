// Checks for the zone's unit skins (unit_skins.cpp).
#include "verify.h"

#include "../../src/lib/stlp.h"

#include <cstdint>
#include <cstring>

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

// 005683D0 UnitTextureRecords: SUB ESP,0xC; PUSH EBX; PUSH EBP; PUSH ESI; MOV ESI,[ESP+0x1C] (10 bytes).
// Differential: the original fills a scratch vector (empty), ours the caller's; sizes and the
// records are compared bitwise. Reached by BuildUnitSkins (00568800) at every zone load with
// dressed units (`newgame`).
namespace {
struct TextureRecord12 {
    std::uint32_t kind, id, itemType;
};
} // namespace
void __fastcall UnitTextureRecords(char body, const void* record, void* list);
static void(__fastcall* s_textureRecords)(char, const void*, void*);
static void __fastcall UnitTextureRecordsVerify(char body, const void* record, void* list) {
    stlp::vector<TextureRecord12> scratch;
    s_textureRecords(body, record, &scratch);
    UnitTextureRecords(body, record, list);
    auto* ours = static_cast<stlp::vector<TextureRecord12>*>(list);
    bool same = scratch.size() == ours->size() &&
                (scratch.size() == 0 || std::memcmp(scratch.begin(), ours->begin(), scratch.size() * 12) == 0);
    ei::verify::Check("UnitTextureRecords", true, same, "body %d: %d/%d records", body, static_cast<int>(scratch.size()),
                      static_cast<int>(ours->size()));
    scratch.FreeStorage();
}
EI_VERIFY(0x005683D0, UnitTextureRecordsVerify, 10, s_textureRecords);
