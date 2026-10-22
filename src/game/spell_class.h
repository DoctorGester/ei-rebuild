#pragma once

// The spells' and the light's compiled tables (C19, engine_object_class.h) in place of game.exe's 00743A94 (CSpell,
// with its tail 00743AA8), 00743AC8 (CSpellContainer, with its tails 00743ADC / 00743AE8), 007439C8
// (CSpellConstructor) and 00743AFC (CLight): each tag overrides the slots its game.exe table changes with the bodies
// that table reached. The constructor and destructor bodies (spell_lifetime.cpp, spell_manager.cpp,
// party_lifetime.cpp, spell_cast.cpp, light.cpp) store them. The cast spell's 0074399C is spell_lifetime.cpp's
// SpellEffectClass (CSpellEffect::Vtable).

#include "spell.h"

#include "class_names_640.h"
#include "engine_object_class.h"

class CSpellClass final : public EngineObjectClass<CSpellClass, CSpell> {
public:
    int Create() override { return Spell()->Clear(); }                       // 2: 0068B320
    void Destroy() override { Spell()->ClearModifiers(); }                   // 3: 0068B350
    void Load(CStream* stream) override { Spell()->LoadSpell(stream); }      // 9: 0068B860
    void Save(CStream* stream) override { Spell()->SaveSpell(stream); }      // 10: 0068B920
    const char* ClassName() override { return SpellClassName(Object()); }   // 11: 0068B280
    int ClassId() override { return SpellClassId(Object()); }               // 12: 0068B290

private:
    CSpell* Spell() { return As<CSpell>(); }
};

// Slot 5 is 0068BE20, empty as CEngineObject's.
class CSpellContainerClass final : public EngineObjectClass<CSpellContainerClass, CSpellContainer> {
public:
    int Create() override { return Container()->Reset(); }                         // 2: 0068BD80
    void Destroy() override { Container()->Shutdown(); }                            // 3: 0068BDD0
    const char* GetName() override { return Container()->NameField(); }             // 7: 0068BC00
    const char* ClassName() override { return SpellContainerClassName(Container()); }   // 11: 0068BC10
    int ClassId() override { return SpellContainerClassId(Container()); }               // 12: 0068BC20

private:
    CSpellContainer* Container() { return As<CSpellContainer>(); }
};

class CSpellConstructorClass final : public EngineObjectClass<CSpellConstructorClass, CSpellConstructor> {
public:
    int Create() override { return Constructor()->Reset(); }    // 2: 00684120
    void Destroy() override { Constructor()->Detach(); }        // 3: 00684150
    const char* ClassName() override { return SpellConstructorClassName(Constructor()); }   // 11: 00684060
    int ClassId() override { return SpellConstructorClassId(Constructor()); }               // 12: 00684070

private:
    CSpellConstructor* Constructor() { return As<CSpellConstructor>(); }
};

class CLightClass final : public TransformObjectClass<CLightClass, CLight> {
public:
    int Create() override { return LightCreate(Light()); }                   // 2: 0068CFF0
    void Destroy() override { LightDestroy(Light()); }                       // 3: 0068D000
    const char* ClassName() override { return LightClassName(Light()); }    // 11: 0068CFA0
    int ClassId() override { return LightClassId(Light()); }                // 12: 0068CFB0

private:
    CLight* Light() { return As<CLight>(); }
};

inline void* CSpell::Vtable() { return CSpellClass::Vtable(); }
inline void* CSpellContainer::Vtable() { return CSpellContainerClass::Vtable(); }
inline void* CSpellConstructor::Vtable() { return CSpellConstructorClass::Vtable(); }
inline void* CLight::Vtable() { return CLightClass::Vtable(); }
