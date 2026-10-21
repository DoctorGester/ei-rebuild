#pragma once

// The CArray<T> instantiations whose elements the game destroys through its own code (ticket S6):
// lib/mfc_array.h's templates over the game's element types, with the element destructors ours
// (each called directly). Registered in game_arrays.cpp; checked in game by verify_mfc_array.cpp until S21d.

#include "lib/mfc_array.h"
#include "lib/mfc_slots.h"
#include "game/engine_object.h"
#include "game/object_tables.h"
#include "game/script.h"
#include "game/unit.h"
#include "game_data.h"

namespace aligned {
std::uint32_t* __fastcall AllocateDwords(int count);   // 0051EF90 (aligned_arrays.cpp)
void __fastcall ArrayAt8Free(void* self);   // 00454380 (aligned_arrays.cpp)
void __fastcall ArrayAt4Free(void* self);   // 004543C0
// 00452C40 (RET 8; aligned_arrays.cpp): SetAtGrow(index, *value) on an aligned dword array.
void __fastcall DwordArraySetAtGrowRef(lib::AlignedArray<std::uint32_t>* a, void*, int index, const std::uint32_t* value);
} // namespace aligned

namespace zone {
void __fastcall ExitDestruct(MapExit* self);           // 00460460 (zone_records.cpp)
void __fastcall ZoneRecordDestruct(MapRecord* self);   // 00460970
void* __fastcall ExitDelete(void* self, void*, unsigned flags);   // 004608B0: scalar deleting destructor
} // namespace zone

namespace gamearrays {

using mfc::impl::ArrayAbi;
using mfc::impl::Bytes;
using mfc::impl::EachElements;

// Records holding an aligned block at +8 (24 bytes) / +4 (20 bytes): the block freed.
inline void DestroyBlockAt8(Bytes<24>* p) { aligned::ArrayAt8Free(p); }
inline void DestroyBlockAt4(Bytes<20>* p) { aligned::ArrayAt4Free(p); }

// Zone exits: the target string empty (004606A0); its destructor.
inline void InitExit(MapExit* p) { ::new (&p->target) mfc::CString; }
inline void DestroyExit(MapExit* p) { zone::ExitDestruct(p); }
inline void DestroyZoneRecord(MapRecord* p) { zone::ZoneRecordDestruct(p); }

// Script groups: the group's vtable, empty (004CC9B0); the destructor body.
inline void InitGroup(ScriptGroup* p);   // defined below the tags
inline void __fastcall DestructGroupBody(ScriptGroup* group) { group->DestructBody(); }   // 004C2290
inline void DestroyGroup(ScriptGroup* p) { p->DestructBody(); }

// Quest objectives: the text empty (004CEE80); the destructor.
inline void InitObjective(ScriptQuestObjective* p) { ::new (&p->text) mfc::CString; }
inline void DestroyObjective(ScriptQuestObjective* p) { ScriptQuestObjectiveDestruct(p); }

// Effect particles: 005124D0's fields; the source released.
inline void InitParticle(EffectParticle* p) {
    p->source = nullptr;
    p->type = 0;
    p->power = 0x10;
    p->present = 0;
}
inline void DestroyParticle(EffectParticle* p) { p->ReleaseSource(); }

// Object references (ScriptObjectRef and the bare CEngineObject* holders): zero filled; destroyed by
// a release through slot 4, the slot cleared (004CC7D0, 0052CEC0) or left (0059C890).
inline void ReleaseAndClear(ScriptObjectRef* p) {
    if (p->object) p->object->Release();
    p->object = nullptr;
}
inline void ReleaseOnly(CEngineObject** p) {
    if (*p) (*p)->Release();
}

// The script's point lights: the light leaves the world's timed objects and is released, the slot
// cleared (004B17F0's body, inlined by 004C5CB0).
inline void DestroyPointLight(ScriptPointLight* p) { p->ScriptPointLightDestruct(); }

using BlocksAt8 = ArrayAbi<Bytes<24>, EachElements<Bytes<24>, nullptr, DestroyBlockAt8>>;
using BlocksAt4 = ArrayAbi<Bytes<20>, EachElements<Bytes<20>, nullptr, DestroyBlockAt4>>;
using Exits = ArrayAbi<MapExit, EachElements<MapExit, InitExit, DestroyExit>>;
// 00573080: SetSize(size, -1) of a CArray<ZoneExit> given by its header (its first member).
inline void ExitsSetSize(void* exits, int size) { Exits::SetSize(static_cast<Exits::Array*>(exits), nullptr, size, -1); }
using ZoneRecords = ArrayAbi<MapRecord, EachElements<MapRecord, nullptr, DestroyZoneRecord>>;
using Groups = ArrayAbi<ScriptGroup, EachElements<ScriptGroup, InitGroup, DestroyGroup>>;
using Objectives = ArrayAbi<ScriptQuestObjective, EachElements<ScriptQuestObjective, InitObjective, DestroyObjective>>;
using Particles = ArrayAbi<EffectParticle, EachElements<EffectParticle, InitParticle, DestroyParticle>>;
using ObjectRefs = ArrayAbi<ScriptObjectRef, EachElements<ScriptObjectRef, nullptr, ReleaseAndClear>>;
using ObjectPtrs = ArrayAbi<CEngineObject*, EachElements<CEngineObject*, nullptr, ReleaseOnly>>;
using PointLights = ArrayAbi<ScriptPointLight, EachElements<ScriptPointLight, nullptr, DestroyPointLight>>;

// The script's areas (each area's parts array: its base vtable back, the storage freed) and
// definitions (S13b).
inline void DestroyArea(ScriptArea* p) {
    mfc::impl::ArrayHeader<mfc::impl::ScriptAreaClass>::Destruct(reinterpret_cast<mfc::CArray<Bytes<1>>*>(&p->parts));
}
inline void DestroyDefinition(ScriptDefinition* p) { ScriptDefinitionDestruct(p); }
using Areas = ArrayAbi<ScriptArea, EachElements<ScriptArea, nullptr, DestroyArea>>;
using Definitions = ArrayAbi<ScriptDefinition, EachElements<ScriptDefinition, nullptr, DestroyDefinition>>;

// CScript's arrays' vtables (their destructors 004CB150, 004CB650, 004CB310).
// +0x00: the compiler's vtable (C19). The areas (+0x194) are mfc::impl::ScriptAreaArrayClass, the definitions (+0x8C)
// recordarrays::DefinitionArrayClass.
struct PointLightArrayClass final
    : mfc::impl::CompiledCollection<PointLightArrayClass, &PointLights::Destruct<PointLightArrayClass>, &mfc::impl::SerializeCArray<PointLights>> {};

// The vtable 0059A100 restores (an array of object pointers, Serialize 0059A170).
struct ObjectPtrArrayBaseClass
    : mfc::impl::CompiledCollection<ObjectPtrArrayBaseClass, &ObjectPtrs::Destruct<ObjectPtrArrayBaseClass>, &mfc::impl::SerializeCArray<ObjectPtrs>> {};

// The particles' draw order (CArray<ParticleDepth>, effects.h; 007A9C50).
struct ParticleDepthArrayClass {
    static void* Vtable() { return ei::GamePointer<void>(0x0073D024); }
};
// The sector spiral's offsets (CArray<SpiralOffset>, sector_map.h; 007C2FF8).
struct SpiralOffsetArrayClass {
    static void* Vtable() { return ei::GamePointer<void>(0x0073FDEC); }
};
// The script quest's objectives (CArray<ScriptQuestObjective>, script.h; 007AA608).
struct ObjectiveArrayClass {
    static void* Vtable() { return ei::GamePointer<void>(0x0073D46C); }
};
// The AI path smoother's node pool (CArray<SmoothNode>, ai_map.h SmoothPool; 007C3038).
struct SmoothNodeArrayClass {
    static void* Vtable() { return ei::GamePointer<void>(0x0073FE34); }
};

// The vtable 004C3B20 restores (CArray<ScriptGroup>).
struct GroupArrayClass final
    : mfc::impl::CompiledCollection<GroupArrayClass, &Groups::Destruct<GroupArrayClass>, &mfc::impl::SerializeCArray<Groups>> {};

} // namespace gamearrays

// The tags lib/mfc_array.h only declares (their slots need the game's elements).
namespace mfc::impl {

// A zone's exits, the table a map record's items (0073C400) and the worlds' exit arrays restore; the destructor
// 00460490 frees each exit's target string.
struct ExitArrayClass
    : CompiledCollection<ExitArrayClass, &ExitTargets::Destruct<ExitArrayClass>, &SerializeCArray<gamearrays::Exits>> {};
// The array inside a map record (constructor 004607E0): derived from ExitArrayClass.
struct MapRecordItemsClass final : ExitArrayClass {
    static void* Vtable() { return CompiledVtable<MapRecordItemsClass>(); }
};
// CArray<ScriptObjectRef> as a script group's base: the destructor body of 004C2290 (the references released, the
// storage freed).
struct ObjectRefArrayClass final
    : CompiledCollection<ObjectRefArrayClass, &gamearrays::DestructGroupBody, &SerializeCArray<gamearrays::ObjectRefs>> {};
// A script group (ScriptGroup): its destructor (004BC9A0) stores ObjectRefArrayClass' table.
struct ScriptGroupClass final
    : CompiledCollection<ScriptGroupClass, &ScriptRefArrayDestruct, &SerializeCArray<gamearrays::ObjectRefs>> {};
// A save's shared object pointers (constructor 005985C0): derived from ObjectPtrArrayBaseClass.
struct ObjectPtrArrayClass final : gamearrays::ObjectPtrArrayBaseClass {
    static void* Vtable() { return CompiledVtable<ObjectPtrArrayClass>(); }
};

// The element policies over them (the lib headers cannot name the complete tags).
// A zone record (92 bytes: eight strings, the exits array at +0x48; constructor 004607E0).
using MapRecords = ArrayAbi<StringsAndArray<0x5C, 8, 0x48, MapRecordItemsClass>,
                            InitElements<StringsAndArray<0x5C, 8, 0x48, MapRecordItemsClass>,
                                         InitStringsAndArray<0x5C, 8, 0x48, MapRecordItemsClass>>>;
// The script's groups (the element 004CC9B0 builds, 20 bytes: the group's array at +0).
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
