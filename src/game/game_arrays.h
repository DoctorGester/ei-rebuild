#pragma once

// The CArray<T> instantiations whose elements the game destroys through its own code: lib/mfc_array.h's templates
// over the game's element types, with the element destructors ours (each called directly). Registered in

#include "lib/mfc_array.h"
#include "lib/mfc_slots.h"
#include "game/engine_object.h"
#include "game/object_tables.h"
#include "game/script.h"
#include "game/unit.h"
#include "game_data.h"

#include <memory>

namespace aligned {
std::uint32_t* __fastcall AllocateDwords(int count);
void __fastcall ArrayAt8Free(void* self);
void __fastcall ArrayAt4Free(void* self);
} // namespace aligned

namespace zone {
void __fastcall ZoneRecordDestruct(MapRecord* self);
} // namespace zone

namespace gamearrays {

using mfc::impl::ArrayAbi;
using mfc::impl::Bytes;
using mfc::impl::EachElements;

// Records holding an aligned block at +8 (24 bytes) / +4 (20 bytes): the block freed.
inline void DestroyBlockAt8(Bytes<24>* p) { aligned::ArrayAt8Free(p); }
inline void DestroyBlockAt4(Bytes<20>* p) { aligned::ArrayAt4Free(p); }

inline void DestroyZoneRecord(MapRecord* p) { zone::ZoneRecordDestruct(p); }

inline void InitGroup(ScriptGroup* p);   // defined below the tags
inline void __fastcall DestructGroupBody(ScriptGroup* group) { group->DestructBody(); }
inline void DestroyGroup(ScriptGroup* p) { p->DestructBody(); }

inline void InitObjective(ScriptQuestObjective* p) { std::construct_at(&p->text); }
inline void DestroyObjective(ScriptQuestObjective* p) { ScriptQuestObjectiveDestruct(p); }

inline void InitParticle(EffectParticle* p) {
    p->source = nullptr;
    p->type = 0;
    p->power = 0x10;
    p->present = 0;
}
inline void DestroyParticle(EffectParticle* p) { p->ReleaseSource(); }

// Object references (ScriptObjectRef and the bare CEngineObject* holders): zero filled; destroyed by a release
// through virtual 4, the slot cleared or left.
inline void ReleaseAndClear(ScriptObjectRef* p) {
    if (p->object) p->object->Release();
    p->object = nullptr;
}
inline void ReleaseOnly(CEngineObject** p) {
    if (*p) (*p)->Release();
}

// The script's point lights: the light leaves the world's timed objects and is released, the slot cleared.
inline void DestroyPointLight(ScriptPointLight* p) { p->ScriptPointLightDestruct(); }

using BlocksAt8 = ArrayAbi<Bytes<24>, EachElements<Bytes<24>, nullptr, DestroyBlockAt8>>;
using BlocksAt4 = ArrayAbi<Bytes<20>, EachElements<Bytes<20>, nullptr, DestroyBlockAt4>>;
using Exits = ArrayAbi<ZoneExit, ZoneExitElements>;
using ZoneRecords = ArrayAbi<MapRecord, EachElements<MapRecord, nullptr, DestroyZoneRecord>>;
using Groups = ArrayAbi<ScriptGroup, EachElements<ScriptGroup, InitGroup, DestroyGroup>>;
using Objectives = ArrayAbi<ScriptQuestObjective, EachElements<ScriptQuestObjective, InitObjective, DestroyObjective>>;
using Particles = ArrayAbi<EffectParticle, EachElements<EffectParticle, InitParticle, DestroyParticle>>;
using ObjectRefs = ArrayAbi<ScriptObjectRef, EachElements<ScriptObjectRef, nullptr, ReleaseAndClear>>;
using ObjectPtrs = ArrayAbi<CEngineObject*, EachElements<CEngineObject*, nullptr, ReleaseOnly>>;
using PointLights = ArrayAbi<ScriptPointLight, EachElements<ScriptPointLight, nullptr, DestroyPointLight>>;

// The script's areas (each area's parts array: its base vtable back, the storage freed) and definitions.
inline void DestroyArea(ScriptArea* p) {
    mfc::impl::ArrayHeader<mfc::impl::ScriptAreaClass>::Destruct(reinterpret_cast<mfc::CArray<Bytes<1>>*>(&p->parts));
}
inline void DestroyDefinition(ScriptDefinition* p) { ScriptDefinitionDestruct(p); }
using Areas = ArrayAbi<ScriptArea, EachElements<ScriptArea, nullptr, DestroyArea>>;
using Definitions = ArrayAbi<ScriptDefinition, EachElements<ScriptDefinition, nullptr, DestroyDefinition>>;

// CScript's arrays' vtables. +0x00: the compiler's vtable. The areas (+0x194) are
// mfc::impl::ScriptAreaArrayClass, the definitions (+0x8C) recordarrays::DefinitionArrayClass.
struct PointLightArrayClass final
    : mfc::impl::CompiledCollection<PointLightArrayClass, &PointLights::Destruct<PointLightArrayClass>, &PointLights::Serialize> {};

struct ObjectPtrArrayBaseClass
    : mfc::impl::CompiledCollection<ObjectPtrArrayBaseClass, &ObjectPtrs::Destruct<ObjectPtrArrayBaseClass>, &ObjectPtrs::Serialize> {};

// The global arrays the static initialisers construct (their constructors and the exit functions' destructors are
// registered in game_arrays.cpp, so static_init.cpp calls them).
// The particles' draw order (CArray<ParticleDepth>, effects.h).
struct ParticleDepthArrayClass final
    : mfc::impl::CompiledCollection<ParticleDepthArrayClass, &mfc::impl::ArrayHeader<ParticleDepthArrayClass>::Destruct, &mfc::impl::Plain8::Serialize> {};
struct SpiralOffsetArrayClass final
    : mfc::impl::CompiledCollection<SpiralOffsetArrayClass, &mfc::impl::ArrayHeader<SpiralOffsetArrayClass>::Destruct, &mfc::impl::Plain4::Serialize> {};
struct ObjectiveArrayClass final
    : mfc::impl::CompiledCollection<ObjectiveArrayClass, &Objectives::Destruct<ObjectiveArrayClass>, &Objectives::Serialize> {};
// The AI path smoother's node pool (CArray<SmoothNode>, ai_map.h SmoothPool).
struct SmoothNodeArrayClass final
    : mfc::impl::CompiledCollection<SmoothNodeArrayClass, &mfc::impl::ArrayHeader<SmoothNodeArrayClass>::Destruct, &mfc::impl::Plain8::Serialize> {};

struct GroupArrayClass final
    : mfc::impl::CompiledCollection<GroupArrayClass, &Groups::Destruct<GroupArrayClass>, &Groups::Serialize> {};

// The script variables' string and group arrays (script_arrays.cpp's constructors; the float values are
// mfc::impl::ScriptFloatValuesClass): their destructors store the plain arrays' tables back (StringArrayClass,
// GroupArrayClass). The strings' also serve every variable set's names.
struct ScriptStringValuesClass final
    : mfc::impl::CompiledCollection<ScriptStringValuesClass, &ScriptNamesDestruct, &mfc::impl::Strings::Serialize> {};
struct ScriptGroupValuesClass final
    : mfc::impl::CompiledCollection<ScriptGroupValuesClass, &ScriptGroupArrayDestruct, &Groups::Serialize> {};

} // namespace gamearrays

// The tags lib/mfc_array.h only declares (their slots need the game's elements).
namespace mfc::impl {

// A zone's exits, the table a map record's items and the worlds' exit arrays restore; the destructor frees each
// exit's target string.
struct ExitArrayClass
    : CompiledCollection<ExitArrayClass, &ExitTargets::Destruct<ExitArrayClass>, &gamearrays::Exits::Serialize> {};
// The array inside a map record: derived from ExitArrayClass.
struct MapRecordItemsClass final : ExitArrayClass {
    static void* Vtable() { return CompiledVtable<MapRecordItemsClass>(); }
};
// CArray<ScriptObjectRef> as a script group's base: the destructor body (the references released, the storage
// freed).
struct ObjectRefArrayClass final
    : CompiledCollection<ObjectRefArrayClass, &gamearrays::DestructGroupBody, &gamearrays::ObjectRefs::Serialize> {};
// A script group (ScriptGroup): its destructor stores ObjectRefArrayClass' table.
struct ScriptGroupClass final
    : CompiledCollection<ScriptGroupClass, &ScriptRefArrayDestruct, &gamearrays::ObjectRefs::Serialize> {};
// A save's shared object pointers: derived from ObjectPtrArrayBaseClass.
struct ObjectPtrArrayClass final : gamearrays::ObjectPtrArrayBaseClass {
    static void* Vtable() { return CompiledVtable<ObjectPtrArrayClass>(); }
};

// The element policies over them (the lib headers cannot name the complete tags).
// A zone record (92 bytes: eight strings, the exits array at +0x48).
using MapRecords = ArrayAbi<StringsAndArray<0x5C, 8, 0x48, MapRecordItemsClass>,
                            InitElements<StringsAndArray<0x5C, 8, 0x48, MapRecordItemsClass>,
                                         InitStringsAndArray<0x5C, 8, 0x48, MapRecordItemsClass>>>;
// The script's groups (the element is 20 bytes: the group's array at +0).
using ScriptGroups = ArrayAbi<WithArray<0x14, 0, ScriptGroupClass>,
                              InitElements<WithArray<0x14, 0, ScriptGroupClass>, InitWithArray<0x14, 0, ScriptGroupClass>>>;
// The world's shared objects (game/world.h SharedObject): the three dwords, then the objects' array at +0xC.
using SharedObjectElement = WithArray<0x20, 0xC, ObjectPtrArrayClass>;
using SharedObjectElements = InitElements<SharedObjectElement, InitWithArray<0x20, 0xC, ObjectPtrArrayClass>>;
using SharedObjectNewList = ListAbi<SharedObjectElement, SharedObjectElements>;
using SharedObjectArray = ArrayAbi<SharedObjectElement, SharedObjectElements>;

} // namespace mfc::impl

namespace gamearrays {
inline void InitGroup(ScriptGroup* p) { mfc::impl::ConstructHeader<mfc::impl::ScriptGroupClass>(p); }
} // namespace gamearrays
