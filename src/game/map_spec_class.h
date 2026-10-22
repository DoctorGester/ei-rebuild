#pragma once

// The zone records' compiled tables (C19; docs/notes/standalone.md, "Compiler-made vtables") in place of game.exe's
// 0073C5C4 (the base record), 0073C5F0 (objects; 0073FA58, the quick item effect's record, is a twin with the same
// slots), 0073C620 (flames), 0073C650 (magic traps), 0073C6B0 (levers), 0073C6E0 (units), 0073C728 (the effect
// records' base), 0073C758 (particle sources), 0073C788 (lights) and 0073C7B8 (sounds). A record keeps game.exe's
// layout with its vtable as the first dword of its Chunk (map_spec.h: the record structs have no C++ virtuals); its
// constructor and destructor bodies store `X::Vtable()`, and the calls through the table (SpecRecord's view, the
// chunk reader's slot 0) reach a tag's slots, which call the bodies game.exe's table reached (map_spec.cpp,
// map_spec_effect.cpp, map_spec_unit.cpp). The slots are listed at the top of map_spec.h.

#include "chunk_stream.h"
#include "map_spec.h"

#include "lib/compiled_vtable.h"
#include "lib/mfc.h"

namespace mapspec {

// The base record's slots (game.exe's 0073C5C4): 0, 7 and 9 empty, 3 an empty RET 4, the rest the base record's
// bodies; slot 10 is `Delete`, T's deleting destructor.
template <typename Self, typename T, auto Delete>
class SpecClass : public ei::CompiledTag<Self> {
public:
    virtual void OnChunk() {}                                                                 // 0: 00469E20 (empty)
    virtual mfc::CFileVirtuals* File() { return this->template ObjectAs<::Chunk>()->::Chunk::File(); }       // 1: 0042BF40
    virtual void* WorldObject() { return Base()->SpecWorldObject(); }                         // 2: 00469F20
    virtual void Nothing(int) {}                                                              // 3: 00469DA0 (RET 4)
    virtual mfc::CString* TypeName(mfc::CString* out) { return Base()->SpecTypeName(out); }   // 4: 00469DB0
    virtual void Move(float dx, float dy, float dz) { Base()->SpecMove(dx, dy, dz); }         // 5: 00469F40
    virtual void Remove() { Base()->SpecRemove(); }                                           // 6: 00469DE0
    virtual void Apply() {}                                                                   // 7: 00469DF0 (empty)
    virtual void Refresh() { Base()->SpecRefresh(); }                                         // 8: 00469E00
    virtual void Save() {}                                                                    // 9: 00469E10 (empty)
    virtual void* DeleteRecord(unsigned flags) { return (Record()->*Delete)(flags); }         // 10

protected:
    T* Record() { return this->template ObjectAs<T>(); }
    Spec* Base() { return this->template ObjectAs<Spec>(); }
};

// The object records' slots (game.exe's 0073C5F0) over the base's: 0, 4, 6-9 the object record's, 11 WriteFields.
template <typename Self, typename T, auto Delete>
class ObjectSpecSlots : public SpecClass<Self, T, Delete> {
public:
    void OnChunk() override { Object()->ObjectSpecOnChunk(); }                                          // 0: 0046AB20
    mfc::CString* TypeName(mfc::CString* out) override { return Object()->ObjectSpecTypeName(out); }    // 4: 0046B090
    void Remove() override { Object()->ObjectSpecRemove(); }                                            // 6: 0046AF70
    void Apply() override { Object()->ObjectSpecApply(); }                                              // 7: 0046AFB0
    void Refresh() override { Object()->ObjectSpecRefresh(); }                                          // 8: 0046AFA0
    void Save() override { Object()->ObjectSpecSave(); }                                                // 9: 0046A920
    virtual void WriteFields() { Object()->ObjectSpecWriteFields(); }                                   // 11: 0046A940

protected:
    ObjectSpec* Object() { return this->template ObjectAs<ObjectSpec>(); }
};

// The effect records' slots (game.exe's 0073C728) over the base's: 2 and 5 the effect record's, 11 SetVisible.
template <typename Self, typename T, auto Delete>
class EffectSpecSlots : public SpecClass<Self, T, Delete> {
public:
    void* WorldObject() override { return Effect()->EffectSpecWorldObject(); }                     // 2: 0046D3D0
    void Move(float dx, float dy, float dz) override { Effect()->EffectSpecMove(dx, dy, dz); }     // 5: 0046D420
    virtual void SetVisible(std::uint8_t visible) { Effect()->EffectSpecSetVisible(visible); }     // 11: 0046D3F0

protected:
    EffectSpec* Effect() { return this->template ObjectAs<EffectSpec>(); }
};

struct SpecBaseClass final : SpecClass<SpecBaseClass, Spec, &Spec::SpecDelete> {};
struct ObjectSpecClass final : ObjectSpecSlots<ObjectSpecClass, ObjectSpec, &ObjectSpec::ObjectSpecDelete> {};

struct FlameSpecClass final : ObjectSpecSlots<FlameSpecClass, FlameSpec, &FlameSpec::FlameSpecDelete> {
    void OnChunk() override { Record()->FlameSpecOnChunk(); }   // 0: 0046B3C0
    void Apply() override { Record()->FlameSpecApply(); }       // 7: 0046B310
    void Save() override { Record()->FlameSpecSave(); }         // 9: 0046B340
};

struct TrapSpecClass final : ObjectSpecSlots<TrapSpecClass, TrapSpec, &TrapSpec::TrapSpecDelete> {
    void OnChunk() override { Record()->TrapSpecOnChunk(); }                                        // 0: 0046B7D0
    mfc::CString* TypeName(mfc::CString* out) override { return Record()->TrapSpecTypeName(out); }  // 4: 0046B520
    void Save() override { Record()->TrapSpecSave(); }                                              // 9: 0046B680
};

struct LeverSpecClass final : ObjectSpecSlots<LeverSpecClass, LeverSpec, &LeverSpec::LeverSpecDelete> {
    void OnChunk() override { Record()->LeverSpecOnChunk(); }   // 0: 0046BF20
    void Save() override { Record()->LeverSpecSave(); }         // 9: 0046BE80
};

// Slot 3 is 0046C4C0, an empty RET 4 as the base's.
struct UnitSpecClass final : ObjectSpecSlots<UnitSpecClass, UnitSpec, &UnitSpec::DeleteScalar> {
    void OnChunk() override { Record()->UnitSpecOnChunk(); }   // 0: 0046D080
    void Apply() override { Record()->UnitSpecApply(); }       // 7: 0046C4D0
    void Save() override { Record()->UnitSpecSave(); }         // 9: 0046CF10
};

struct EffectSpecClass final : EffectSpecSlots<EffectSpecClass, EffectSpec, &EffectSpec::EffectSpecDelete> {};

struct ParticleSpecClass final : EffectSpecSlots<ParticleSpecClass, ParticleSpec, &ParticleSpec::ParticleSpecDelete> {
    void OnChunk() override { Record()->ParticleSpecOnChunk(); }                                        // 0: 0046D730
    mfc::CString* TypeName(mfc::CString* out) override { return Record()->ParticleSpecTypeName(out); }  // 4: 0046D550
    void Apply() override { Record()->ParticleSpecApply(); }                                            // 7: 0046D870
    void Save() override { Record()->ParticleSpecSave(); }                                              // 9: 0046D680
};

struct LightSpecClass final : EffectSpecSlots<LightSpecClass, LightSpec, &LightSpec::LightSpecDelete> {
    void OnChunk() override { Record()->LightSpecOnChunk(); }                                        // 0: 0046DBF0
    mfc::CString* TypeName(mfc::CString* out) override { return Record()->LightSpecTypeName(out); }  // 4: 0046D9D0
    void Apply() override { Record()->LightSpecApply(); }                                            // 7: 0046DD70
    void Save() override { Record()->LightSpecSave(); }                                              // 9: 0046DB20
};

// Slot 11 is 0046E490, byte-identical to the effect record's 0046D3F0.
struct SoundSpecClass final : EffectSpecSlots<SoundSpecClass, SoundSpec, &SoundSpec::SoundSpecDelete> {
    void OnChunk() override { Record()->SoundSpecOnChunk(); }                                        // 0: 0046E190
    mfc::CString* TypeName(mfc::CString* out) override { return Record()->SoundSpecTypeName(out); }  // 4: 0046DF20
    void Apply() override { Record()->SoundSpecApply(); }                                            // 7: 0046E380
    void Save() override { Record()->SoundSpecSave(); }                                              // 9: 0046E090
};

inline void* Spec::Vtable() { return SpecBaseClass::Vtable(); }
inline void* ObjectSpec::Vtable() { return ObjectSpecClass::Vtable(); }
inline void* FlameSpec::Vtable() { return FlameSpecClass::Vtable(); }
inline void* TrapSpec::Vtable() { return TrapSpecClass::Vtable(); }
inline void* LeverSpec::Vtable() { return LeverSpecClass::Vtable(); }
inline void* UnitSpec::Vtable() { return UnitSpecClass::Vtable(); }
inline void* EffectSpec::Vtable() { return EffectSpecClass::Vtable(); }
inline void* ParticleSpec::Vtable() { return ParticleSpecClass::Vtable(); }
inline void* LightSpec::Vtable() { return LightSpecClass::Vtable(); }
inline void* SoundSpec::Vtable() { return SoundSpecClass::Vtable(); }

} // namespace mapspec
