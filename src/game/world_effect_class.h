#pragma once

// The world effects' compiled tables (C19, engine_object_class.h) in place of game.exe's: the base 007434AC, the
// sound 007433EC, the sound sequence 0074344C, the ambient sound 0074350C, the point light 0074356C (with its tail
// 007435B4), the particle source 007435D0 (with 007435D8 / 00743618), the wall 00743630 (+ 00743678), the lightning
// 00743690 (+ 007436D8), the missile base 00743754, the arrow 007436F0 (+ 00743738), the arrow figure 007437B8, the
// visibility 0074381C, the bird 0074387C and the tornado 007438F8. EffectClass declares CEffect's slots 18-23 over
// CTransformObject's; each class's tag overrides the slots its game.exe table changes with the bodies that table
// reached. The constructor and destructor bodies (world_effect_lifetime.cpp, world_effect_missile.cpp,
// world_effect_visibility.cpp, world_effect_moshka.cpp, world_effect_tornado.cpp) store them.

#include "world_effect.h"

#include "class_names_640.h"
#include "engine_object_class.h"

template <typename Self, typename T>
class EffectClass : public TransformObjectClass<Self, T> {
public:
    int Create() override { return Effect()->CreateEffect(); }    // 2: 00675D10
    void Destroy() override { Effect()->DestroyEffect(); }        // 3: 00675D40
    int RunTick() override { return Effect()->TickDefault(); }    // 6: 00676900
    virtual void Reset() { Effect()->ResetBase(); }               // 18: 00675BE0
    virtual double Radius() { return Effect()->DefaultRadius(); }   // 19: 006768F0
    virtual Vector3* Position(void* group) { return Effect()->Position(group); }   // 20: 00675FA0
    virtual int Unpack(int, DeltaField*) { return _purecall(); }   // 21: pure
    virtual void Pack(DeltaField*) { _purecall(); }                // 22: pure
    virtual void Start(int) { _purecall(); }                       // 23: pure

protected:
    CEffect* Effect() { return this->template ObjectAs<CEffect>(); }
    T* Target() { return this->template ObjectAs<T>(); }
};

// The abstract base (slots 11, 12, 21-23 pure).
struct CEffectBaseClass final : EffectClass<CEffectBaseClass, CEffect> {};

class CEffectSoundClass final : public EffectClass<CEffectSoundClass, CEffectSound> {
public:
    int RunTick() override { return Target()->Tick(); }                                  // 6: 006762F0
    void Load(CStream* stream) override { Target()->LoadState(stream); }                 // 9: 006765F0
    void Save(CStream* stream) override { Target()->SaveState(stream); }                 // 10: 006766D0
    const char* ClassName() override { return EffectSoundClassName(Object()); }        // 11: 00676060
    int ClassId() override { return EffectSoundClassId(Object()); }                    // 12: 00676070
    void Reset() override { Target()->Reset(); }                                         // 18: 00676200
    double Radius() override { return Target()->Radius(); }                              // 19: 00676050
    int Unpack(int update, DeltaField* field) override { return Target()->Unpack(update, field); }   // 21: 00676430
    void Pack(DeltaField* field) override { Target()->Pack(field); }                     // 22: 00676550
    void Start(int update) override { Target()->Start(update); }                         // 23: 006763A0
};

class CEffectSoundSeqClass final : public EffectClass<CEffectSoundSeqClass, CEffectSoundSeq> {
public:
    int RunTick() override { return Target()->Tick(); }                                  // 6: 00677250
    void Load(CStream* stream) override { Target()->LoadState(stream); }                 // 9: 006772D0
    void Save(CStream* stream) override { Target()->SaveState(stream); }                 // 10: 00677430
    const char* ClassName() override { return EffectSoundSeqClassName(Object()); }     // 11: 00676940
    int ClassId() override { return EffectSoundSeqClassId(Object()); }                 // 12: 00676950
    void Reset() override { Target()->Reset(); }                                         // 18: 00676B10
    double Radius() override { return Target()->Radius(); }                              // 19: 00676930
    int Unpack(int update, DeltaField* field) override { return Target()->Unpack(update, field); }   // 21: 00676FD0
    void Pack(DeltaField* field) override { Target()->Pack(field); }                     // 22: 00677180
    void Start(int update) override { Target()->Start(update); }                         // 23: 00676C00
};

// Slot 6 is the base's.
class CEffectAmbientSoundClass final : public EffectClass<CEffectAmbientSoundClass, CEffectAmbientSound> {
public:
    void Load(CStream* stream) override { Target()->LoadState(stream); }                 // 9: 00677BF0
    void Save(CStream* stream) override { Target()->SaveState(stream); }                 // 10: 00677C60
    const char* ClassName() override { return EffectAmbientSoundClassName(Object()); }   // 11: 006776A0
    int ClassId() override { return EffectAmbientSoundClassId(Object()); }               // 12: 006776B0
    void Reset() override { Target()->Reset(); }                                         // 18: 00677870
    double Radius() override { return Target()->Radius(); }                              // 19: 00677690
    int Unpack(int update, DeltaField* field) override { return Target()->Unpack(update, field); }   // 21: 006779D0
    void Pack(DeltaField* field) override { Target()->Pack(field); }                     // 22: 00677B20
    void Start(int update) override { Target()->Start(update); }                         // 23: 00677950
};

// Slot 19 is the base's; the light's own radius is a slot of its own (24).
class CEffectPointLightClass final : public EffectClass<CEffectPointLightClass, CEffectPointLight> {
public:
    int RunTick() override { return Target()->Tick(); }                                  // 6: 006781B0
    void Load(CStream* stream) override { Target()->LoadState(stream); }                 // 9: 00678690
    void Save(CStream* stream) override { Target()->SaveState(stream); }                 // 10: 00678740
    const char* ClassName() override { return EffectPointLightClassName(Object()); }   // 11: 00677F80
    int ClassId() override { return EffectPointLightClassId(Object()); }               // 12: 00677F90
    void Reset() override { Target()->Reset(); }                                         // 18: 006780C0
    int Unpack(int update, DeltaField* field) override { return Target()->Unpack(update, field); }   // 21: 00678520
    void Pack(DeltaField* field) override { Target()->Pack(field); }                     // 22: 00678610
    void Start(int update) override { Target()->Start(update); }                         // 23: 006782D0
    virtual double LightRadius() { return Target()->Radius(); }                          // 24: 00677F70
};

// The particle source's slots; the wall keeps 6, 20 and 23.
template <typename Self, typename T>
class ParticleClass : public EffectClass<Self, T> {
public:
    int RunTick() override { return Particle()->Tick(); }                                    // 6: 006795C0
    void Load(CStream* stream) override { Particle()->LoadState(stream); }                   // 9: 006793E0
    void Save(CStream* stream) override { Particle()->SaveState(stream); }                   // 10: 006794E0
    const char* ClassName() override { return EffectParticleClassName(this->Object()); }    // 11: 00678950
    int ClassId() override { return EffectParticleClassId(this->Object()); }                // 12: 00678960
    void Reset() override { Particle()->Reset(); }                                           // 18: 00678A90
    Vector3* Position(void* group) override { return Particle()->Position(group); }         // 20: 00678BA0
    int Unpack(int update, DeltaField* field) override { return Particle()->Unpack(update, field); }   // 21: 00678D30
    void Pack(DeltaField* field) override { Particle()->Pack(field); }                       // 22: 006791A0
    void Start(int update) override { Particle()->Start(update); }                           // 23: 00678CF0

protected:
    CEffectParticle* Particle() { return this->template ObjectAs<CEffectParticle>(); }
};

struct CEffectParticleClass final : ParticleClass<CEffectParticleClass, CEffectParticle> {};

class CEffectWallClass final : public ParticleClass<CEffectWallClass, CEffectWall> {
public:
    void Load(CStream* stream) override { Target()->LoadState(stream); }                 // 9: 00679EA0
    void Save(CStream* stream) override { Target()->SaveState(stream); }                 // 10: 00679ED0
    const char* ClassName() override { return EffectWallClassName(Object()); }         // 11: 006799A0
    int ClassId() override { return EffectWallClassId(Object()); }                     // 12: 006799B0
    void Reset() override { Target()->Reset(); }                                         // 18: 00679AF0
    int Unpack(int update, DeltaField* field) override { return Target()->Unpack(update, field); }   // 21: 00679C70
    void Pack(DeltaField* field) override { Target()->Pack(field); }                     // 22: 00679DB0
};

class CEffectLightningClass final : public EffectClass<CEffectLightningClass, CEffectLightning> {
public:
    int RunTick() override { return Target()->Tick(); }                                  // 6: 0067A8F0
    void Load(CStream* stream) override { Target()->LoadState(stream); }                 // 9: 0067A770
    void Save(CStream* stream) override { Target()->SaveState(stream); }                 // 10: 0067A7F0
    const char* ClassName() override { return EffectLightningClassName(Object()); }    // 11: 0067A070
    int ClassId() override { return EffectLightningClassId(Object()); }                // 12: 0067A080
    void Reset() override { Target()->Reset(); }                                         // 18: 0067A1B0
    int Unpack(int update, DeltaField* field) override { return Target()->Unpack(update, field); }   // 21: 0067A490
    void Pack(DeltaField* field) override { Target()->Pack(field); }                     // 22: 0067A600
    void Start(int update) override { Target()->Start(update); }                         // 23: 0067A370
};

// The missiles (slots 21-24 pure in the abstract base; its slot 6 is the arrow's tick).
template <typename Self, typename T>
class MissileClass : public EffectClass<Self, T> {
public:
    int RunTick() override { return this->template ObjectAs<CEffectArrow>()->Tick(); }   // 6: 0067AC90
    void Reset() override { this->template ObjectAs<CEffectMissile>()->ResetMissile(); }   // 18: 0067AA00
    virtual void MoveParts() { _purecall(); }                                            // 24: pure
};

struct CEffectMissileClass final : MissileClass<CEffectMissileClass, CEffectMissile> {};

class CEffectArrowClass final : public MissileClass<CEffectArrowClass, CEffectArrow> {
public:
    void Load(CStream* stream) override { Target()->LoadState(stream); }                 // 9: 0067B8E0
    void Save(CStream* stream) override { Target()->SaveState(stream); }                 // 10: 0067B9A0
    const char* ClassName() override { return EffectArrowClassName(Object()); }        // 11: 0067B180
    int ClassId() override { return EffectArrowClassId(Object()); }                    // 12: 0067B190
    void Reset() override { Target()->Reset(); }                                         // 18: 0067AD10
    int Unpack(int update, DeltaField* field) override { return Target()->Unpack(update, field); }   // 21: 0067B690
    void Pack(DeltaField* field) override { Target()->Pack(field); }                     // 22: 0067B800
    void Start(int update) override { Target()->Start(update); }                         // 23: 0067B4E0
    void MoveParts() override { Target()->MoveParts(); }                                 // 24: 0067B450
};

class CEffectArrowFigureClass final : public MissileClass<CEffectArrowFigureClass, CEffectArrowFigure> {
public:
    int RunTick() override { return Target()->Tick(); }                                  // 6: 0067BE20
    void Load(CStream* stream) override { Target()->LoadState(stream); }                 // 9: 0067C200
    void Save(CStream* stream) override { Target()->SaveState(stream); }                 // 10: 0067C290
    const char* ClassName() override { return EffectArrowFigureClassName(Object()); }  // 11: 0067BC90
    int ClassId() override { return EffectArrowFigureClassId(Object()); }              // 12: 0067BCA0
    void Reset() override { Target()->Reset(); }                                         // 18: 0067BAA0
    int Unpack(int update, DeltaField* field) override { return Target()->Unpack(update, field); }   // 21: 0067C080
    void Pack(DeltaField* field) override { Target()->Pack(field); }                     // 22: 0067C170
    void Start(int update) override { Target()->Start(update); }                         // 23: 0067BF20
    void MoveParts() override { Target()->MoveFigure(); }                                // 24: 0067BEF0
};

class CEffectShowVisibilityClass final : public EffectClass<CEffectShowVisibilityClass, CEffectShowVisibility> {
public:
    int RunTick() override { return Target()->Tick(); }                                  // 6: 0067C840
    void Load(CStream* stream) override { Target()->LoadState(stream); }                 // 9: 0067C8E0
    void Save(CStream* stream) override { Target()->SaveState(stream); }                 // 10: 0067C910
    const char* ClassName() override { return EffectShowVisibilityClassName(Object()); }   // 11: 0067C450
    int ClassId() override { return EffectShowVisibilityClassId(Object()); }               // 12: 0067C460
    void Reset() override { Target()->Reset(); }                                         // 18: 0067C5F0
    int Unpack(int update, DeltaField* field) override { return Target()->Unpack(update, field); }   // 21: 0067C6B0
    void Pack(DeltaField* field) override { Target()->Pack(field); }                     // 22: 0067C7A0
    void Start(int update) override { Target()->Start(update); }                         // 23: 0067C660
};

// Load / Save are the base's; Pack and Start do nothing (0067D650 / 0067D660, RET 4).
class CEffectMoshkaClass final : public EffectClass<CEffectMoshkaClass, CEffectMoshka> {
public:
    int RunTick() override { return Target()->Tick(); }                                  // 6: 0067CD70
    const char* ClassName() override { return EffectMoshkaClassName(Object()); }       // 11: 0067CAB0
    int ClassId() override { return EffectMoshkaClassId(Object()); }                   // 12: 0067CAC0
    void Reset() override { Target()->Reset(); }                                         // 18: 0067CC90
    int Unpack(int update, DeltaField* field) override { return Target()->Unpack(update, field); }   // 21: 0067D640
    void Pack(DeltaField*) override {}                                                  // 22: 0067D650
    void Start(int) override {}                                                         // 23: 0067D660
};

class CEffectTornadoClass final : public EffectClass<CEffectTornadoClass, CEffectTornado> {
public:
    int RunTick() override { return Target()->Tick(); }                                  // 6: 0067DBD0
    void Load(CStream* stream) override { Target()->LoadState(stream); }                 // 9: 0067E020
    void Save(CStream* stream) override { Target()->SaveState(stream); }                 // 10: 0067E070
    const char* ClassName() override { return EffectTornadoClassName(Object()); }      // 11: 0067D7C0
    int ClassId() override { return EffectTornadoClassId(Object()); }                  // 12: 0067D7D0
    void Reset() override { Target()->Reset(); }                                         // 18: 0067D970
    int Unpack(int update, DeltaField* field) override { return Target()->Unpack(update, field); }   // 21: 0067DDD0
    void Pack(DeltaField* field) override { Target()->Pack(field); }                     // 22: 0067DEB0
    void Start(int update) override { Target()->Start(update); }                         // 23: 0067DF10
};

inline void* CEffect::BaseVtable() { return CEffectBaseClass::Vtable(); }
inline void* CEffectSound::Vtable() { return CEffectSoundClass::Vtable(); }
inline void* CEffectSoundSeq::Vtable() { return CEffectSoundSeqClass::Vtable(); }
inline void* CEffectAmbientSound::Vtable() { return CEffectAmbientSoundClass::Vtable(); }
inline void* CEffectPointLight::Vtable() { return CEffectPointLightClass::Vtable(); }
inline void* CEffectParticle::Vtable() { return CEffectParticleClass::Vtable(); }
inline void* CEffectWall::Vtable() { return CEffectWallClass::Vtable(); }
inline void* CEffectLightning::Vtable() { return CEffectLightningClass::Vtable(); }
inline void* CEffectMissile::Vtable() { return CEffectMissileClass::Vtable(); }
inline void* CEffectArrow::Vtable() { return CEffectArrowClass::Vtable(); }
inline void* CEffectArrowFigure::Vtable() { return CEffectArrowFigureClass::Vtable(); }
inline void* CEffectShowVisibility::Vtable() { return CEffectShowVisibilityClass::Vtable(); }
inline void* CEffectMoshka::Vtable() { return CEffectMoshkaClass::Vtable(); }
inline void* CEffectTornado::Vtable() { return CEffectTornadoClass::Vtable(); }
