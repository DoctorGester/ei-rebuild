#pragma once

// The CArray<T> instantiations whose elements the game destroys through its own code (lib/mfc_array.h's
// templates over the game's element types).

#include "lib/mfc_array.h"
#include "lib/mfc_slots.h"
#include "game/engine_object.h"
#include "game/object_tables.h"
#include "game/script.h"
#include "game/unit.h"
#include "game_data.h"

#include <memory>

namespace aligned {
void ArrayAt8Free(void* self);
} // namespace aligned

namespace gamearrays {

using mfc::impl::ArrayAbi;
using mfc::impl::Bytes;
using mfc::impl::EachElements;

// Records holding an aligned block at +8 (24 bytes): the block freed.
inline void DestroyBlockAt8(Bytes<24>* p) { aligned::ArrayAt8Free(p); }

inline void InitGroup(ScriptGroup* p);   // defined below the tags
inline void DestructGroupBody(ScriptGroup* group) { group->DestructBody(); }
inline void DestroyGroup(ScriptGroup* p) { p->DestructBody(); }

inline void InitObjective(ScriptQuestObjective* p) { std::construct_at(&p->text); }
inline void DestroyObjective(ScriptQuestObjective* p) { ScriptQuestObjectiveDestruct(p); }


// Object references (ScriptObjectRef and the bare CEngineObject* holders): zero filled; destroyed by a Release,
// the slot cleared or left.
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
using Groups = ArrayAbi<ScriptGroup, EachElements<ScriptGroup, InitGroup, DestroyGroup>>;
using Objectives = ArrayAbi<ScriptQuestObjective, EachElements<ScriptQuestObjective, InitObjective, DestroyObjective>>;
using ObjectRefs = ArrayAbi<ScriptObjectRef, EachElements<ScriptObjectRef, nullptr, ReleaseAndClear>>;
using PointLights = ArrayAbi<ScriptPointLight, EachElements<ScriptPointLight, nullptr, DestroyPointLight>>;

// The script's areas (each area's parts array: its base vtable back, the storage freed) and definitions.
inline void DestroyArea(ScriptArea* p) {
    mfc::impl::ArrayAbi<ScriptAreaPart>::Destruct<mfc::impl::ScriptAreaClass>(&p->parts);
}
inline void DestroyDefinition(ScriptDefinition* p) { ScriptDefinitionDestruct(p); }
using Areas = ArrayAbi<ScriptArea, EachElements<ScriptArea, nullptr, DestroyArea>>;
using Definitions = ArrayAbi<ScriptDefinition, EachElements<ScriptDefinition, nullptr, DestroyDefinition>>;

struct PointLightArrayClass final
    : mfc::impl::CompiledCollection<PointLightArrayClass, &PointLights::Destruct<PointLightArrayClass>, &PointLights::Serialize> {};

// The global arrays the static initialisers construct.
struct ObjectiveArrayClass final
    : mfc::impl::CompiledCollection<ObjectiveArrayClass, &Objectives::Destruct<ObjectiveArrayClass>, &Objectives::Serialize> {};

struct GroupArrayClass final
    : mfc::impl::CompiledCollection<GroupArrayClass, &Groups::Destruct<GroupArrayClass>, &Groups::Serialize> {};

// The script variables' string and group arrays: their destructors store the plain arrays' tables back. The
// strings' also serve every variable set's names.
struct ScriptStringValuesClass final
    : mfc::impl::CompiledCollection<ScriptStringValuesClass, &ScriptNamesDestruct, &mfc::impl::Strings::Serialize> {};
struct ScriptGroupValuesClass final
    : mfc::impl::CompiledCollection<ScriptGroupValuesClass, &ScriptGroupArrayDestruct, &Groups::Serialize> {};

} // namespace gamearrays

// The tags lib/mfc_array.h only declares (their slots need the game's elements).
namespace mfc::impl {

// CArray<ScriptObjectRef> as a script group's base: the references released, the storage freed.
struct ObjectRefArrayClass final
    : CompiledCollection<ObjectRefArrayClass, &gamearrays::DestructGroupBody, &gamearrays::ObjectRefs::Serialize> {};
// A script group (ScriptGroup): its destructor stores ObjectRefArrayClass' table.
struct ScriptGroupClass final
    : CompiledCollection<ScriptGroupClass, &ScriptRefArrayDestruct, &gamearrays::ObjectRefs::Serialize> {};

// The element policies over them (the lib headers cannot name the complete tags).
// The script's groups (the element is 20 bytes: the group's array at +0).
using ScriptGroups = ArrayAbi<WithArray<0x14, 0, ScriptGroupClass>,
                              InitElements<WithArray<0x14, 0, ScriptGroupClass>, InitWithArray<0x14, 0, ScriptGroupClass>>>;

} // namespace mfc::impl

namespace gamearrays {
inline void InitGroup(ScriptGroup* p) { mfc::impl::ConstructHeader<mfc::impl::ScriptGroupClass>(p); }
} // namespace gamearrays
