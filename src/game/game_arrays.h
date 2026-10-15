#pragma once

// The CArray<T> instantiations whose elements the game destroys through its own code (ticket S6):
// lib/mfc_array.h's templates over the game's element types, with the element destructors ours
// (each called directly). Registered in game_arrays.cpp; checked by tests/verify/verify_mfc_array.cpp.

#include "lib/mfc_array.h"
#include "game/object_tables.h"
#include "game/script.h"
#include "game/unit.h"

namespace aligned {
void __fastcall ArrayAt8Free(void* self);   // 00454380 (aligned_arrays.cpp)
void __fastcall ArrayAt4Free(void* self);   // 004543C0
} // namespace aligned

namespace zone {
void __fastcall ExitDestruct(MapExit* self);           // 00460460 (zone_records.cpp)
void __fastcall ZoneRecordDestruct(MapRecord* self);   // 00460970
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

using BlocksAt8 = ArrayAbi<Bytes<24>, EachElements<Bytes<24>, nullptr, DestroyBlockAt8>>;
using BlocksAt4 = ArrayAbi<Bytes<20>, EachElements<Bytes<20>, nullptr, DestroyBlockAt4>>;
using Exits = ArrayAbi<MapExit, EachElements<MapExit, InitExit, DestroyExit>>;
using ZoneRecords = ArrayAbi<MapRecord, EachElements<MapRecord, nullptr, DestroyZoneRecord>>;
using Groups = ArrayAbi<ScriptGroup, EachElements<ScriptGroup, InitGroup, DestroyGroup>>;
using Objectives = ArrayAbi<ScriptQuestObjective, EachElements<ScriptQuestObjective, InitObjective, DestroyObjective>>;
using Particles = ArrayAbi<EffectParticle, EachElements<EffectParticle, InitParticle, DestroyParticle>>;

} // namespace gamearrays
