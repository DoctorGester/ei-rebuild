// Checks for the zone's unit skins (unit_skins.cpp).
#include "verify.h"

#include "../../src/lib/stlp.h"

#include <cstdint>
#include <cstring>

#include "../../src/game/world.h"

// 00568800 builds textures and figures (texture cache, figure maker): ours only. Checked: the two
// record vectors are left as they were.
static void* s_skins;
static void __fastcall BuildUnitSkinsVerify(CWorldClient* self, void*, const void* othersVector, const void* dressedVector) {
    const auto* others = static_cast<const char* const*>(othersVector);
    const auto* dressed = static_cast<const char* const*>(dressedVector);
    const char* before[4] = {others[0], others[1], dressed[0], dressed[1]};
    self->BuildUnitSkins(othersVector, dressedVector);
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

// 005685C0 (A-W14) the texture record's constructor: MOV EAX,ECX; OR ECX,-1 (5 bytes). Differential:
// the original on a scratch record, ours on the caller's. Its only caller, 005683D0, is rewritten and
// builds the record itself, so no scenario reaches it.
void* __fastcall TextureRecordConstruct(void* self);
static void*(__fastcall* s_recordConstruct)(void*);
static void* __fastcall TextureRecordConstructVerify(void* self) {
    TextureRecord12 scratch = {1, 2, 3};
    void* expected = s_recordConstruct(&scratch);
    void* result = TextureRecordConstruct(self);
    ei::verify::Check("TextureRecordConstruct", true,
                      expected == &scratch && result == self && std::memcmp(&scratch, self, 12) == 0, "%p", self);
    return result;
}
EI_VERIFY(0x005685C0, TextureRecordConstructVerify, 5, s_recordConstruct);
