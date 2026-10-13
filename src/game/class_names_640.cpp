// The CEngineObject class name / id slots (11 / 12, each a MOV EAX,imm32) of the classes in
// 0x640000-0x6C0000 whose files do not have them (A-H24; class_names_640.h). The names are the
// exe's own strings, returned by address as the originals do.
#include "class_names_640.h"

#include "ei/hook.h"

#include <cstdint>

namespace {
const char* Text(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }
} // namespace

// 0065F560 / 0065F570: "CParty", 9.
const char* __fastcall PartyClassName(CEngineObject*) { return Text(0x00794C78); }
int __fastcall PartyClassId(CEngineObject*) { return 0x9; }
EI_REPLACE(0x0065F560, PartyClassName);
EI_REPLACE(0x0065F570, PartyClassId);

// The world's effects (world_effect_*.cpp).
// 00676060 / 00676070: "CEffectSound", 0x48.
const char* __fastcall EffectSoundClassName(CEngineObject*) { return Text(0x00795398); }
int __fastcall EffectSoundClassId(CEngineObject*) { return 0x48; }
EI_REPLACE(0x00676060, EffectSoundClassName);
EI_REPLACE(0x00676070, EffectSoundClassId);
// 00676940 / 00676950: "CEffectSoundSeq", 0x63.
const char* __fastcall EffectSoundSeqClassName(CEngineObject*) { return Text(0x007953A8); }
int __fastcall EffectSoundSeqClassId(CEngineObject*) { return 0x63; }
EI_REPLACE(0x00676940, EffectSoundSeqClassName);
EI_REPLACE(0x00676950, EffectSoundSeqClassId);
// 006776A0 / 006776B0: "CEffectAmbientSound", 0x49.
const char* __fastcall EffectAmbientSoundClassName(CEngineObject*) { return Text(0x007953B8); }
int __fastcall EffectAmbientSoundClassId(CEngineObject*) { return 0x49; }
EI_REPLACE(0x006776A0, EffectAmbientSoundClassName);
EI_REPLACE(0x006776B0, EffectAmbientSoundClassId);
// 00677F80 / 00677F90: "CEffectPointLight", 0x4B.
const char* __fastcall EffectPointLightClassName(CEngineObject*) { return Text(0x007953CC); }
int __fastcall EffectPointLightClassId(CEngineObject*) { return 0x4b; }
EI_REPLACE(0x00677F80, EffectPointLightClassName);
EI_REPLACE(0x00677F90, EffectPointLightClassId);
// 00678950 / 00678960: "CEffectParticle", 0x4A.
const char* __fastcall EffectParticleClassName(CEngineObject*) { return Text(0x007953E0); }
int __fastcall EffectParticleClassId(CEngineObject*) { return 0x4a; }
EI_REPLACE(0x00678950, EffectParticleClassName);
EI_REPLACE(0x00678960, EffectParticleClassId);
// 006799A0 / 006799B0: "CEffectWall", 0x5A.
const char* __fastcall EffectWallClassName(CEngineObject*) { return Text(0x007953F0); }
int __fastcall EffectWallClassId(CEngineObject*) { return 0x5a; }
EI_REPLACE(0x006799A0, EffectWallClassName);
EI_REPLACE(0x006799B0, EffectWallClassId);
// 0067A070 / 0067A080: "CEffectLightning", 0x4D.
const char* __fastcall EffectLightningClassName(CEngineObject*) { return Text(0x007953FC); }
int __fastcall EffectLightningClassId(CEngineObject*) { return 0x4d; }
EI_REPLACE(0x0067A070, EffectLightningClassName);
EI_REPLACE(0x0067A080, EffectLightningClassId);
// 0067B180 / 0067B190: "CEffectArrow", 0x4E.
const char* __fastcall EffectArrowClassName(CEngineObject*) { return Text(0x00795410); }
int __fastcall EffectArrowClassId(CEngineObject*) { return 0x4e; }
EI_REPLACE(0x0067B180, EffectArrowClassName);
EI_REPLACE(0x0067B190, EffectArrowClassId);
// 0067BC90 / 0067BCA0: "CEffectArrowFigure", 0x5B.
const char* __fastcall EffectArrowFigureClassName(CEngineObject*) { return Text(0x00795420); }
int __fastcall EffectArrowFigureClassId(CEngineObject*) { return 0x5b; }
EI_REPLACE(0x0067BC90, EffectArrowFigureClassName);
EI_REPLACE(0x0067BCA0, EffectArrowFigureClassId);
// 0067C450 / 0067C460: "CEffectShowVisibility", 0x5E.
const char* __fastcall EffectShowVisibilityClassName(CEngineObject*) { return Text(0x00795444); }
int __fastcall EffectShowVisibilityClassId(CEngineObject*) { return 0x5e; }
EI_REPLACE(0x0067C450, EffectShowVisibilityClassName);
EI_REPLACE(0x0067C460, EffectShowVisibilityClassId);
// 0067CAB0 / 0067CAC0: "CEffectMoshka", 0x64.
const char* __fastcall EffectMoshkaClassName(CEngineObject*) { return Text(0x0079545C); }
int __fastcall EffectMoshkaClassId(CEngineObject*) { return 0x64; }
EI_REPLACE(0x0067CAB0, EffectMoshkaClassName);
EI_REPLACE(0x0067CAC0, EffectMoshkaClassId);
// 0067D7C0 / 0067D7D0: "CEffectTornado", 0x65.
const char* __fastcall EffectTornadoClassName(CEngineObject*) { return Text(0x0079547C); }
int __fastcall EffectTornadoClassId(CEngineObject*) { return 0x65; }
EI_REPLACE(0x0067D7C0, EffectTornadoClassName);
EI_REPLACE(0x0067D7D0, EffectTornadoClassId);

// 0068B280 / 0068B290: "CSpell", 0x4F.
const char* __fastcall SpellClassName(CEngineObject*) { return Text(0x007959C0); }
int __fastcall SpellClassId(CEngineObject*) { return 0x4f; }
EI_REPLACE(0x0068B280, SpellClassName);
EI_REPLACE(0x0068B290, SpellClassId);

// 00691910 / 00691920: "CTexture", 6 (its slot 7, 00691900, is 4 bytes: too short to hook).
const char* __fastcall TextureClassName(CEngineObject*) { return Text(0x00795D68); }
int __fastcall TextureClassId(CEngineObject*) { return 0x6; }
EI_REPLACE(0x00691910, TextureClassName);
EI_REPLACE(0x00691920, TextureClassId);

// The items (item.h).
// 006A0570 / 006A0580: "CItemWeapon", 0x11.
const char* __fastcall ItemWeaponClassName(CEngineObject*) { return Text(0x007963C4); }
int __fastcall ItemWeaponClassId(CEngineObject*) { return 0x11; }
EI_REPLACE(0x006A0570, ItemWeaponClassName);
EI_REPLACE(0x006A0580, ItemWeaponClassId);
// 006A2F80 / 006A2F90: "CItemArmor", 0x12.
const char* __fastcall ItemArmorClassName(CEngineObject*) { return Text(0x00796528); }
int __fastcall ItemArmorClassId(CEngineObject*) { return 0x12; }
EI_REPLACE(0x006A2F80, ItemArmorClassName);
EI_REPLACE(0x006A2F90, ItemArmorClassId);
// 006A3AF0 / 006A3B00: "CItemQuick", 0x13.
const char* __fastcall ItemQuickClassName(CEngineObject*) { return Text(0x00796560); }
int __fastcall ItemQuickClassId(CEngineObject*) { return 0x13; }
EI_REPLACE(0x006A3AF0, ItemQuickClassName);
EI_REPLACE(0x006A3B00, ItemQuickClassId);
// 006A43E0 / 006A43F0: "CItemQuest", 0x14.
const char* __fastcall ItemQuestClassName(CEngineObject*) { return Text(0x00796598); }
int __fastcall ItemQuestClassId(CEngineObject*) { return 0x14; }
EI_REPLACE(0x006A43E0, ItemQuestClassName);
EI_REPLACE(0x006A43F0, ItemQuestClassId);
// 006A49C0 / 006A49D0: "CItemLoot", 0x15.
const char* __fastcall ItemLootClassName(CEngineObject*) { return Text(0x007965A4); }
int __fastcall ItemLootClassId(CEngineObject*) { return 0x15; }
EI_REPLACE(0x006A49C0, ItemLootClassName);
EI_REPLACE(0x006A49D0, ItemLootClassId);
// 006A5700 / 006A5710: "CItemSpell", 0x16.
const char* __fastcall ItemSpellClassName(CEngineObject*) { return Text(0x00796618); }
int __fastcall ItemSpellClassId(CEngineObject*) { return 0x16; }
EI_REPLACE(0x006A5700, ItemSpellClassName);
EI_REPLACE(0x006A5710, ItemSpellClassId);
// 006A5AE0 / 006A5AF0: "CItemConstructor", 0x1C.
const char* __fastcall ItemConstructorClassName(CEngineObject*) { return Text(0x00796624); }
int __fastcall ItemConstructorClassId(CEngineObject*) { return 0x1c; }
EI_REPLACE(0x006A5AE0, ItemConstructorClassName);
EI_REPLACE(0x006A5AF0, ItemConstructorClassId);
