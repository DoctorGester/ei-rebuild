#pragma once

// The CArray<T> instantiations whose elements the game destroys through its own code (ticket S6):
// lib/mfc_array.h's templates over the game's element types, with the element destructors ours
// (each called directly). Registered in game_arrays.cpp; checked in game by verify_mfc_array.cpp until S21d.

#include "lib/mfc_array.h"
#include "game/engine_object.h"
#include "game/object_tables.h"
#include "game/script.h"
#include "game/unit.h"

namespace aligned {
std::uint32_t* __fastcall AllocateDwords(int count);   // 0051EF90 (aligned_arrays.cpp)
void __fastcall ArrayAt8Free(void* self);   // 00454380 (aligned_arrays.cpp)
void __fastcall ArrayAt4Free(void* self);   // 004543C0
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
inline void InitExit(MapExit* p) { p->target = mfc::impl::PchNil(); }
inline void DestroyExit(MapExit* p) { zone::ExitDestruct(p); }
inline void DestroyZoneRecord(MapRecord* p) { zone::ZoneRecordDestruct(p); }

// Script groups: the group's vtable, empty (004CC9B0); the destructor body.
inline void InitGroup(ScriptGroup* p) { mfc::impl::ConstructHeader<mfc::impl::ScriptGroupClass>(p); }
inline void DestroyGroup(ScriptGroup* p) { p->DestructBody(); }

// Quest objectives: the text empty (004CEE80); the destructor.
inline void InitObjective(ScriptQuestObjective* p) {
    *reinterpret_cast<char**>(&p->text) = mfc::impl::PchNil();
}
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
struct PointLightArrayClass {   // +0x00
    static void* Vtable() { return reinterpret_cast<void*>(0x0073D3A0); }
};
struct AreaArrayClass {   // +0x194
    static void* Vtable() { return reinterpret_cast<void*>(0x0073D3C4); }
};
struct DefinitionArrayClass {   // +0x8C
    static void* Vtable() { return reinterpret_cast<void*>(0x0073D3B8); }
};

// The vtable 0059A100 restores (an array of object pointers, Serialize 0059A170).
struct ObjectPtrArrayBaseClass {
    static void* Vtable() { return reinterpret_cast<void*>(0x0073FABC); }
};

// The particles' draw order (CArray<ParticleDepth>, effects.h; 007A9C50).
struct ParticleDepthArrayClass {
    static void* Vtable() { return reinterpret_cast<void*>(0x0073D024); }
};
// The sector spiral's offsets (CArray<SpiralOffset>, sector_map.h; 007C2FF8).
struct SpiralOffsetArrayClass {
    static void* Vtable() { return reinterpret_cast<void*>(0x0073FDEC); }
};
// The script quest's objectives (CArray<ScriptQuestObjective>, script.h; 007AA608).
struct ObjectiveArrayClass {
    static void* Vtable() { return reinterpret_cast<void*>(0x0073D46C); }
};
// The AI path smoother's node pool (CArray<SmoothNode>, ai_map.h SmoothPool; 007C3038).
struct SmoothNodeArrayClass {
    static void* Vtable() { return reinterpret_cast<void*>(0x0073FE34); }
};

// The vtable 004C3B20 restores (CArray<ScriptGroup>).
struct GroupArrayClass {
    static void* Vtable() { return reinterpret_cast<void*>(0x0073D310); }
};

// 004C1290 (RET 4): SetSize to the source's size, then each reference replaced by a referenced copy
// of the source's (a temporary from GetAt, released after).
void __fastcall ObjectRefCopy(ObjectRefs::Array* self, void*, const ObjectRefs::Array* source);
// 004CC890 / 004C3B00 (RET 8): group `index` copy-constructed into `result` (ScriptGroup's vtable,
// then ObjectRefCopy); returns `result`.
ScriptGroup* __fastcall GroupGetAt(const Groups::Array* self, void*, ScriptGroup* result, int index);
// 004C1BE0 (RET 4): SetSize to the source's size, then each group assigned from a copy (GroupGetAt),
// the copy destroyed after.
void __fastcall GroupArrayCopy(Groups::Array* self, void*, const Groups::Array* source);

// 0059E300 / 0059CA40 (RET 8): element `index` into `result` with one more reference; returns
// `result`.
CEngineObject** __fastcall ObjectPtrGetAt(const ObjectPtrs::Array* self, void*, CEngineObject** result, int index);
// 0059B8F0 (RET 4): SetSize to the source's size, then each pointer replaced by the source's (a
// counted temporary from GetAt; the new one referenced before the old one is released).
void __fastcall ObjectPtrCopy(ObjectPtrs::Array* self, void*, const ObjectPtrs::Array* source);

// 004CC7A0 (RET 8): element `index` copied into `result` with one more reference; returns `result`.
// 004C21E0 (RET 8) calls it.
ScriptObjectRef* __fastcall ObjectRefGetAt(const ObjectRefs::Array* self, void*, ScriptObjectRef* result, int index);

} // namespace gamearrays
