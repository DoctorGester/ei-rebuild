// Checks for class_names_640.cpp (A-H24): every getter against the original (P; MOV EAX,imm32 is the
// five stolen bytes). Reached through the vtables (the base manager's Save / Load and the debug
// listings read slot 12; the effect, item and spell classes are made by every new game).
#include "verify.h"

#include "../../src/game/class_names_640.h"

namespace {

template <const char*(__fastcall* Ours)(CEngineObject*)>
struct NameCheck {
    static inline const char*(__fastcall* original)(CEngineObject*);
    static const char* __fastcall Verify(CEngineObject* self) {
        const char* expected = original(self);
        ei::verify::Check("class name slot", expected, Ours(self), "%p", static_cast<void*>(self));
        return expected;
    }
};

template <int(__fastcall* Ours)(CEngineObject*)>
struct IdCheck {
    static inline int(__fastcall* original)(CEngineObject*);
    static int __fastcall Verify(CEngineObject* self) {
        int expected = original(self);
        ei::verify::Check("class id slot", expected, Ours(self), "%p", static_cast<void*>(self));
        return expected;
    }
};

} // namespace

EI_VERIFY(0x0065F560, NameCheck<PartyClassName>::Verify, 5, NameCheck<PartyClassName>::original);
EI_VERIFY(0x0065F570, IdCheck<PartyClassId>::Verify, 5, IdCheck<PartyClassId>::original);
EI_VERIFY(0x00676060, NameCheck<EffectSoundClassName>::Verify, 5, NameCheck<EffectSoundClassName>::original);
EI_VERIFY(0x00676070, IdCheck<EffectSoundClassId>::Verify, 5, IdCheck<EffectSoundClassId>::original);
EI_VERIFY(0x00676940, NameCheck<EffectSoundSeqClassName>::Verify, 5, NameCheck<EffectSoundSeqClassName>::original);
EI_VERIFY(0x00676950, IdCheck<EffectSoundSeqClassId>::Verify, 5, IdCheck<EffectSoundSeqClassId>::original);
EI_VERIFY(0x006776A0, NameCheck<EffectAmbientSoundClassName>::Verify, 5, NameCheck<EffectAmbientSoundClassName>::original);
EI_VERIFY(0x006776B0, IdCheck<EffectAmbientSoundClassId>::Verify, 5, IdCheck<EffectAmbientSoundClassId>::original);
EI_VERIFY(0x00677F80, NameCheck<EffectPointLightClassName>::Verify, 5, NameCheck<EffectPointLightClassName>::original);
EI_VERIFY(0x00677F90, IdCheck<EffectPointLightClassId>::Verify, 5, IdCheck<EffectPointLightClassId>::original);
EI_VERIFY(0x00678950, NameCheck<EffectParticleClassName>::Verify, 5, NameCheck<EffectParticleClassName>::original);
EI_VERIFY(0x00678960, IdCheck<EffectParticleClassId>::Verify, 5, IdCheck<EffectParticleClassId>::original);
EI_VERIFY(0x006799A0, NameCheck<EffectWallClassName>::Verify, 5, NameCheck<EffectWallClassName>::original);
EI_VERIFY(0x006799B0, IdCheck<EffectWallClassId>::Verify, 5, IdCheck<EffectWallClassId>::original);
EI_VERIFY(0x0067A070, NameCheck<EffectLightningClassName>::Verify, 5, NameCheck<EffectLightningClassName>::original);
EI_VERIFY(0x0067A080, IdCheck<EffectLightningClassId>::Verify, 5, IdCheck<EffectLightningClassId>::original);
EI_VERIFY(0x0067B180, NameCheck<EffectArrowClassName>::Verify, 5, NameCheck<EffectArrowClassName>::original);
EI_VERIFY(0x0067B190, IdCheck<EffectArrowClassId>::Verify, 5, IdCheck<EffectArrowClassId>::original);
EI_VERIFY(0x0067BC90, NameCheck<EffectArrowFigureClassName>::Verify, 5, NameCheck<EffectArrowFigureClassName>::original);
EI_VERIFY(0x0067BCA0, IdCheck<EffectArrowFigureClassId>::Verify, 5, IdCheck<EffectArrowFigureClassId>::original);
EI_VERIFY(0x0067C450, NameCheck<EffectShowVisibilityClassName>::Verify, 5, NameCheck<EffectShowVisibilityClassName>::original);
EI_VERIFY(0x0067C460, IdCheck<EffectShowVisibilityClassId>::Verify, 5, IdCheck<EffectShowVisibilityClassId>::original);
EI_VERIFY(0x0067CAB0, NameCheck<EffectMoshkaClassName>::Verify, 5, NameCheck<EffectMoshkaClassName>::original);
EI_VERIFY(0x0067CAC0, IdCheck<EffectMoshkaClassId>::Verify, 5, IdCheck<EffectMoshkaClassId>::original);
EI_VERIFY(0x0067D7C0, NameCheck<EffectTornadoClassName>::Verify, 5, NameCheck<EffectTornadoClassName>::original);
EI_VERIFY(0x0067D7D0, IdCheck<EffectTornadoClassId>::Verify, 5, IdCheck<EffectTornadoClassId>::original);
EI_VERIFY(0x0068B280, NameCheck<SpellClassName>::Verify, 5, NameCheck<SpellClassName>::original);
EI_VERIFY(0x0068B290, IdCheck<SpellClassId>::Verify, 5, IdCheck<SpellClassId>::original);
EI_VERIFY(0x00691910, NameCheck<TextureClassName>::Verify, 5, NameCheck<TextureClassName>::original);
EI_VERIFY(0x00691920, IdCheck<TextureClassId>::Verify, 5, IdCheck<TextureClassId>::original);
EI_VERIFY(0x006A0570, NameCheck<ItemWeaponClassName>::Verify, 5, NameCheck<ItemWeaponClassName>::original);
EI_VERIFY(0x006A0580, IdCheck<ItemWeaponClassId>::Verify, 5, IdCheck<ItemWeaponClassId>::original);
EI_VERIFY(0x006A2F80, NameCheck<ItemArmorClassName>::Verify, 5, NameCheck<ItemArmorClassName>::original);
EI_VERIFY(0x006A2F90, IdCheck<ItemArmorClassId>::Verify, 5, IdCheck<ItemArmorClassId>::original);
EI_VERIFY(0x006A3AF0, NameCheck<ItemQuickClassName>::Verify, 5, NameCheck<ItemQuickClassName>::original);
EI_VERIFY(0x006A3B00, IdCheck<ItemQuickClassId>::Verify, 5, IdCheck<ItemQuickClassId>::original);
EI_VERIFY(0x006A43E0, NameCheck<ItemQuestClassName>::Verify, 5, NameCheck<ItemQuestClassName>::original);
EI_VERIFY(0x006A43F0, IdCheck<ItemQuestClassId>::Verify, 5, IdCheck<ItemQuestClassId>::original);
EI_VERIFY(0x006A49C0, NameCheck<ItemLootClassName>::Verify, 5, NameCheck<ItemLootClassName>::original);
EI_VERIFY(0x006A49D0, IdCheck<ItemLootClassId>::Verify, 5, IdCheck<ItemLootClassId>::original);
EI_VERIFY(0x006A5700, NameCheck<ItemSpellClassName>::Verify, 5, NameCheck<ItemSpellClassName>::original);
EI_VERIFY(0x006A5710, IdCheck<ItemSpellClassId>::Verify, 5, IdCheck<ItemSpellClassId>::original);
EI_VERIFY(0x006A5AE0, NameCheck<ItemConstructorClassName>::Verify, 5, NameCheck<ItemConstructorClassName>::original);
EI_VERIFY(0x006A5AF0, IdCheck<ItemConstructorClassId>::Verify, 5, IdCheck<ItemConstructorClassId>::original);
