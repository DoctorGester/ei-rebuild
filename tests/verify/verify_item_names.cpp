// Checks for the item name builders (item_names.cpp). They only build a new CString: the
// original builds the caller's result (returned), ours a local copy; the texts are compared.
#include "verify.h"

#include "../../src/game/item.h"

#include <cstring>

namespace {
template <typename Item>
void CompareNames(const char* name, Item* item, const mfc::CString& expected, const mfc::CString& actual) {
    bool same = std::strcmp(expected, actual) == 0;
    ei::verify::Check(name, true, same, "kind %X id %08X: \"%.40s\" vs \"%.40s\"", item->type_, item->unknown_18,
                      static_cast<const char*>(expected), static_cast<const char*>(actual));
}
} // namespace

// One wrapper per method: the original into the caller's result, ours into a local.
#define EI_NAME_WRAPPER(Class, Method)                                                                          \
    static mfc::CString*(__fastcall* s_##Class##Method)(Class*, void*, mfc::CString*);                        \
    static mfc::CString* __fastcall Class##Method##Verify(Class* self, void*, mfc::CString* result) {         \
        s_##Class##Method(self, nullptr, result);                                                              \
        mfc::CString actual = self->Method();                                                                  \
        CompareNames(#Class "::" #Method, self, *result, actual);                                              \
        return result;                                                                                         \
    }

EI_NAME_WRAPPER(CItemWeapon, ModelName)
EI_NAME_WRAPPER(CItemWeapon, FirstListName)
EI_NAME_WRAPPER(CItemWeapon, FigureName)
EI_NAME_WRAPPER(CItemWeapon, PictureName)
EI_NAME_WRAPPER(CItemArmor, ModelName)
EI_NAME_WRAPPER(CItemArmor, FirstListName)
EI_NAME_WRAPPER(CItemArmor, FigureName)
EI_NAME_WRAPPER(CItemArmor, PictureName)
EI_NAME_WRAPPER(CItemQuick, FigureName)
EI_NAME_WRAPPER(CItemQuick, PictureName)
EI_NAME_WRAPPER(CItemQuest, FigureName)
EI_NAME_WRAPPER(CItemQuest, PictureName)
EI_NAME_WRAPPER(CItemSpell, FigureName)
EI_NAME_WRAPPER(CItemLoot, FigureName)
EI_NAME_WRAPPER(CItemLoot, PictureName)

// Stolen bytes: PUSH -1; PUSH imm32 (7), or PUSH ECX; PUSH ESI; MOV ESI,[imm32] (8) for 006A0F70
// and PUSH ECX; MOV EDX,[imm32]; PUSH ESI (8) for 006A36A0.
EI_VERIFY(0x006A0840, CItemWeaponModelNameVerify, 7, s_CItemWeaponModelName);
EI_VERIFY(0x006A0F70, CItemWeaponFirstListNameVerify, 8, s_CItemWeaponFirstListName);
EI_VERIFY(0x006A2750, CItemWeaponFigureNameVerify, 7, s_CItemWeaponFigureName);
EI_VERIFY(0x006A27E0, CItemWeaponPictureNameVerify, 7, s_CItemWeaponPictureName);
EI_VERIFY(0x006A3240, CItemArmorModelNameVerify, 7, s_CItemArmorModelName);
EI_VERIFY(0x006A36A0, CItemArmorFirstListNameVerify, 8, s_CItemArmorFirstListName);
EI_VERIFY(0x006A3720, CItemArmorFigureNameVerify, 7, s_CItemArmorFigureName);
EI_VERIFY(0x006A37B0, CItemArmorPictureNameVerify, 7, s_CItemArmorPictureName);
EI_VERIFY(0x006A3CD0, CItemQuickFigureNameVerify, 7, s_CItemQuickFigureName);
EI_VERIFY(0x006A4040, CItemQuickPictureNameVerify, 7, s_CItemQuickPictureName);
EI_VERIFY(0x006A45C0, CItemQuestFigureNameVerify, 7, s_CItemQuestFigureName);
EI_VERIFY(0x006A47B0, CItemQuestPictureNameVerify, 7, s_CItemQuestPictureName);
EI_VERIFY(0x006A5820, CItemSpellFigureNameVerify, 7, s_CItemSpellFigureName);
EI_VERIFY(0x006A4B90, CItemLootFigureNameVerify, 7, s_CItemLootFigureName);
EI_VERIFY(0x006A5030, CItemLootPictureNameVerify, 7, s_CItemLootPictureName);

// 006A0CC0 (the weapon's base figure; reached from the unit effect point 00532860, scenarios effects
// and spells_cast): the original into the caller's CString, ours into a non-empty local; the
// results (AL) and the texts compared. Stolen bytes: SUB ESP,0x24; PUSH EBX; PUSH ESI (5).
using WeaponBaseFigureFn = bool(__fastcall*)(std::uint32_t, mfc::CString*);
static WeaponBaseFigureFn s_weaponBaseFigure;
static bool __fastcall WeaponBaseFigureVerify(std::uint32_t index, mfc::CString* out) {
    bool expected = s_weaponBaseFigure(index, out);
    mfc::CString actual("?");
    bool result = WeaponBaseFigure(index, &actual);
    ei::verify::Check("WeaponBaseFigure", true, expected == result && std::strcmp(*out, actual) == 0,
                      "index %u: %d \"%.40s\" vs %d \"%.40s\"", index, expected, static_cast<const char*>(*out), result,
                      static_cast<const char*>(actual));
    return expected;
}
EI_VERIFY(0x006A0CC0, WeaponBaseFigureVerify, 5, s_weaponBaseFigure);
