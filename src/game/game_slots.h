#pragma once

// The deleting destructors of the MFC collections whose elements the game destroys through its own code.

#include "lib/mfc_slots.h"
#include "game/game_arrays.h"
#include "game/game_lists.h"
#include "game/script.h"
#include "game_data.h"

#include <cstring>
#include <memory>
#include <new>

namespace gameslots {

// ---- Elements -------------------------------------------------------------------------------------

// A unit memory's 8-byte records: an object reference (released by a virtual call, then cleared) and a value.
struct ObjectAndValue {
    ScriptObjectRef ref;
    int value;
};
static_assert(sizeof(ObjectAndValue) == 8);
inline void ReleaseObjectAndValue(ObjectAndValue* p) { gamearrays::ReleaseAndClear(&p->ref); }

// A map object client's 16-byte effect records: an engine object (released, left).
struct EffectObjectRecord {
    std::uint32_t head[2];
    CEngineObject* object;
    std::uint32_t tail;
};
static_assert(sizeof(EffectObjectRecord) == 0x10);
inline void ReleaseEffectObject(EffectObjectRecord* p) { gamearrays::ReleaseOnly(&p->object); }

inline void DestroyCommand(SSCCmd* p) { ScriptCmdDestruct(p); }
inline void DestroyDecl(ScriptDecl* p) { ScriptDeclDestruct(p); }

using mfc::impl::ArrayAbi;
using mfc::impl::EachElements;
using mfc::impl::ListAbi;

using ObjectValues = ArrayAbi<ObjectAndValue, EachElements<ObjectAndValue, nullptr, ReleaseObjectAndValue>>;
using EffectObjects = ArrayAbi<EffectObjectRecord, EachElements<EffectObjectRecord, nullptr, ReleaseEffectObject>>;
using Definitions = ArrayAbi<ScriptDefinition, EachElements<ScriptDefinition, nullptr, gamelists::DestroyDefinition>>;
using Commands = ArrayAbi<SSCCmd, EachElements<SSCCmd, nullptr, DestroyCommand>>;
using DeclList = ListAbi<ScriptDecl, EachElements<ScriptDecl, nullptr, DestroyDecl>>;
using GroupList = ListAbi<ScriptGroup, EachElements<ScriptGroup, nullptr, gamearrays::DestroyGroup>>;

// A command block's commands as SetSize builds them (zeroed, then SSCCmd's constructor).
inline void InitCommand(SSCCmd* p) { p->Construct(); }
using BuiltCommands = ArrayAbi<SSCCmd, EachElements<SSCCmd, InitCommand, DestroyCommand>>;
// The map database's zone records.
using BuiltZoneRecords = mfc::impl::ArrayAbi<MapRecord, MapRecordElements>;
// CArray<MapRecord>, the map database's maps.
struct MapRecordArrayClass final
    : mfc::impl::CompiledCollection<MapRecordArrayClass, &BuiltZoneRecords::Destruct<MapRecordArrayClass>,
                                    &BuiltZoneRecords::Serialize> {};

// ---- The lists whose Serialize loads through a temporary -----------------------------------------
// ListAbi::SerializeThrough's element functions: how each loader builds the temporary (Init), assigns the new
// element from it (Assign) and destroys it (Destroy).

// Script object references: the new object gains a reference, the old one is released.
inline void InitObjectRef(ScriptObjectRef* p) { p->object = nullptr; }
inline void AssignObjectRef(ScriptObjectRef* to, const ScriptObjectRef* from) {
    gamearrays::ReleaseAndClear(to);
    to->Construct(*from);
}
inline void ReleaseObjectRef(ScriptObjectRef* p) { gamearrays::ReleaseOnly(&p->object); }

// The script's effects: copied as bytes (particle sources, lightnings) or by the FX source's
// assignment; destroyed by their destructors (the object leaves the world's timed objects).
inline void InitParticleSource(ScriptParticleSource* p) { p->object = nullptr; }
inline void CopyParticleSource(ScriptParticleSource* to, const ScriptParticleSource* from) {
    std::memcpy(static_cast<void*>(to), from, sizeof(ScriptParticleSource));
}
inline void InitFxSource(ScriptFxSource* p) {
    p->object = nullptr;
    std::construct_at(&p->name);
}
inline void AssignFxSource(ScriptFxSource* to, const ScriptFxSource* from) { to->ScriptFxSourceAssign(from); }
inline void InitLightning(ScriptLightning* p) {
    p->object = nullptr;
    p->id = -1;
}
inline void CopyLightning(ScriptLightning* to, const ScriptLightning* from) { *to = *from; }

// Command blocks, definitions, runs (their four leading fields zero, then the variables), groups.
inline void InitDecl(ScriptDecl* p);   // defined below the tags
inline void AssignDecl(ScriptDecl* to, const ScriptDecl* from) { to->ScriptDeclAssign(from); }
inline void AssignDefinition(ScriptDefinition* to, const ScriptDefinition* from) { to->ScriptDefinitionAssign(from); }
inline void InitRunTemporary(ScriptRun* p) {
    p->block = nullptr;
    p->index = 0;
    p->ticks = 0;
    p->killFlag = nullptr;
    gamelists::InitRun(p);
}
inline void AssignRun(ScriptRun* to, const ScriptRun* from) { to->ScriptRunAssign(from); }
inline void AssignGroup(ScriptGroup* to, const ScriptGroup* from) {
    to->CopyFrom(*from);
}

// Tokens ("None" / -1, the token's assignment, the text destroyed) and defines (two strings).
inline void InitToken(ScriptToken* p) { p->Construct(); }
inline void AssignToken(ScriptToken* to, const ScriptToken* from) { to->Assign(*from); }
inline void InitDefine(ScriptDefine* p) {
    std::construct_at(&p->name);
    std::construct_at(&p->value);
}
inline void AssignDefine(ScriptDefine* to, const ScriptDefine* from) {
    to->name = from->name;
    to->value = from->value;
}
inline void DestroyDefine(ScriptDefine* p) { ScriptDefineElements::Destruct(p, 1); }

// The world's shared objects: the three dwords, then the objects' copy (each counted).
inline void InitSharedObject(SharedObject* p) { mfc::impl::ConstructHeader<mfc::impl::ObjectPtrArrayClass>(&p->objects); }
inline void AssignSharedObject(SharedObject* to, const SharedObject* from) {
    to->objectId = from->objectId;
    to->player = from->player;
    to->money = from->money;
    to->objects.CopyFrom(from->objects);
}

inline void AssignWave(WaveRecord* to, const WaveRecord* from) { to->Assign(from); }

// The FX sources: zero fill, no sound, the name empty (game.exe's CList<ScriptFxSource>::NewNode and its
// ConstructElements).
using FxSourceElements = EachElements<ScriptFxSource, InitFxSource, nullptr>;
using FxSourceNodes = ListAbi<ScriptFxSource, FxSourceElements>;
using LightningNodes = ListAbi<ScriptLightning, EachElements<ScriptLightning, InitLightning, nullptr>>;
using DeclNodes = ListAbi<ScriptDecl, EachElements<ScriptDecl, InitDecl, nullptr>>;
using TokenNodes = ListAbi<ScriptToken, mfc::impl::HelperElements<ScriptToken, ConstructTokens, nullptr>>;
using DefineNodes = ListAbi<ScriptDefine, ScriptDefineElements>;
using SharedObjectNodes = ListAbi<SharedObject, EachElements<SharedObject, InitSharedObject, nullptr>>;

using mfc::impl::ListTemporary;
using ObjectRefsThrough = ListTemporary<gamelists::ObjectRefs, InitObjectRef, AssignObjectRef, ReleaseObjectRef>;
using ParticleSourcesThrough =
    ListTemporary<gamelists::ParticleSources, InitParticleSource, CopyParticleSource, gamelists::DestroyParticleSource>;
using FxSourcesThrough = ListTemporary<FxSourceNodes, InitFxSource, AssignFxSource, gamelists::DestroyFxSource>;
using LightningsThrough = ListTemporary<LightningNodes, InitLightning, CopyLightning, gamelists::DestroyLightning>;
using DeclsThrough = ListTemporary<DeclNodes, InitDecl, AssignDecl, gamelists::DestroyDecl>;
using DefinitionsThrough =
    ListTemporary<gamelists::Definitions, gamelists::InitDefinition, AssignDefinition, gamelists::DestroyDefinition>;
using RunsThrough = ListTemporary<gamelists::Runs, InitRunTemporary, AssignRun, gamelists::DestroyRun>;
using GroupsThrough = ListTemporary<gamelists::Groups, gamearrays::InitGroup, AssignGroup, gamearrays::DestroyGroup>;
using TokensThrough = ListTemporary<TokenNodes, InitToken, AssignToken, gamelists::DestroyToken>;
using DefinesThrough = ListTemporary<DefineNodes, InitDefine, AssignDefine, DestroyDefine>;
using SharedObjectsThrough = ListTemporary<SharedObjectNodes, InitSharedObject, AssignSharedObject, gamelists::DestroySharedObject>;
using WavesThrough = ListTemporary<gamelists::Waves, gamelists::InitWave, AssignWave, gamelists::DestroyWave>;

// ---- The instantiations' vtables ----------------------------------------------
// Each tag is a CompiledCollection over the destructor and Serialize of the table it replaces.

using mfc::impl::CompiledCollection;

struct ObjectRefListClass final
    : CompiledCollection<ObjectRefListClass, &gamelists::ObjectRefs::Destruct<ObjectRefListClass>, &ObjectRefsThrough::Abi::SerializeThrough<ObjectRefsThrough>> {};
struct SharedObjectListClass final
    : CompiledCollection<SharedObjectListClass, &gamelists::SharedObjects::Destruct<SharedObjectListClass>, &SharedObjectsThrough::Abi::SerializeThrough<SharedObjectsThrough>> {};
struct WaveListClass final
    : CompiledCollection<WaveListClass, &gamelists::Waves::Destruct<WaveListClass>, &WavesThrough::Abi::SerializeThrough<WavesThrough>> {};

struct ParticleSourceListClass final
    : CompiledCollection<ParticleSourceListClass, &gamelists::ParticleSources::Destruct<ParticleSourceListClass>, &ParticleSourcesThrough::Abi::SerializeThrough<ParticleSourcesThrough>> {};
struct FxSourceListClass final
    : CompiledCollection<FxSourceListClass, &gamelists::FxSources::Destruct<FxSourceListClass>, &FxSourcesThrough::Abi::SerializeThrough<FxSourcesThrough>> {};
struct LightningListClass final
    : CompiledCollection<LightningListClass, &gamelists::Lightnings::Destruct<LightningListClass>, &LightningsThrough::Abi::SerializeThrough<LightningsThrough>> {};
struct DeclListClass final
    : CompiledCollection<DeclListClass, &DeclList::Destruct<DeclListClass>, &DeclsThrough::Abi::SerializeThrough<DeclsThrough>> {};
struct DefinitionListClass final
    : CompiledCollection<DefinitionListClass, &gamelists::Definitions::Destruct<DefinitionListClass>, &DefinitionsThrough::Abi::SerializeThrough<DefinitionsThrough>> {};
struct RunListClass final
    : CompiledCollection<RunListClass, &gamelists::Runs::Destruct<RunListClass>, &RunsThrough::Abi::SerializeThrough<RunsThrough>> {};
struct GroupListClass final
    : CompiledCollection<GroupListClass, &GroupList::Destruct<GroupListClass>, &GroupsThrough::Abi::SerializeThrough<GroupsThrough>> {};
struct TokenListClass final
    : CompiledCollection<TokenListClass, &gamelists::Tokens::Destruct<TokenListClass>, &TokensThrough::Abi::SerializeThrough<TokensThrough>> {};
struct DefineListClass final
    : CompiledCollection<DefineListClass, &ScriptDefinesDestruct, &DefinesThrough::Abi::SerializeThrough<DefinesThrough>> {};

struct MemoryObjectArrayClass final
    : CompiledCollection<MemoryObjectArrayClass, &ObjectValues::Destruct<MemoryObjectArrayClass>, &ObjectValues::Serialize> {};
struct ClientObjectPtrArrayClass final
    : CompiledCollection<ClientObjectPtrArrayClass, &gamearrays::ObjectPtrs::Destruct<ClientObjectPtrArrayClass>, &gamearrays::ObjectPtrs::Serialize> {};
struct EffectObjectArrayClass final
    : CompiledCollection<EffectObjectArrayClass, &EffectObjects::Destruct<EffectObjectArrayClass>, &gamearrays::Particles::Serialize> {};
struct CommandArrayClass
    : CompiledCollection<CommandArrayClass, &Commands::Destruct<CommandArrayClass>, &BuiltCommands::Serialize> {};

} // namespace gameslots

namespace mfc::impl {
struct ScriptCommandsClass final : gameslots::CommandArrayClass {
    static void* Vtable() { return CompiledVtable<ScriptCommandsClass>(); }
};
// The script blocks (ScriptDecl, 24 bytes: the commands' array at +0) of the command block lists and arrays.
using CommandBlock = WithArray<0x18, 0, ScriptCommandsClass>;
using CommandBlocks = InitElements<CommandBlock, InitWithArray<0x18, 0, ScriptCommandsClass>>;
using CommandBlockList = ListAbi<CommandBlock, CommandBlocks>;
} // namespace mfc::impl

namespace gameslots {
inline void InitDecl(ScriptDecl* p) { mfc::impl::ConstructHeader<mfc::impl::ScriptCommandsClass>(&p->commands); }
} // namespace gameslots
