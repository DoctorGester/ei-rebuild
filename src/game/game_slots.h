#pragma once

// The scalar deleting destructors (and the destructors they call) of the MFC collections whose elements the game
// destroys through its own code (the library-only ones are lib/mfc_slots.h's). Registered in game_slots.cpp.

#include "lib/mfc_slots.h"
#include "game/game_arrays.h"
#include "game/game_lists.h"
#include "game/script.h"
#include "game_data.h"

#include <cstring>
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

// A map object client's 16-byte effect records: an engine object at +8 (released, left).
struct EffectObjectRecord {
    std::uint32_t head[2];
    CEngineObject* object;   // +0x08
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
// The map database's zone records: built as the record's constructor builds them, destroyed by
// ZoneRecordDestruct.
using ZoneRecordElement = mfc::impl::StringsAndArray<0x5C, 8, 0x48, mfc::impl::MapRecordItemsClass>;
inline void DestroyZoneRecordElement(ZoneRecordElement* p) { zone::ZoneRecordDestruct(reinterpret_cast<MapRecord*>(p)); }
using BuiltZoneRecords =
    ArrayAbi<ZoneRecordElement, EachElements<ZoneRecordElement, mfc::impl::InitStringsAndArray<0x5C, 8, 0x48, mfc::impl::MapRecordItemsClass>,
                                             DestroyZoneRecordElement>>;
// CArray<MapRecord> (the map database's maps, CMapDatabase +0x14): the compiler's table.
struct MapRecordArrayClass final
    : mfc::impl::CompiledCollection<MapRecordArrayClass, &BuiltZoneRecords::Destruct<MapRecordArrayClass>,
                                    &BuiltZoneRecords::Serialize> {};

// ---- The lists whose Serialize loads through a temporary -----------------------------------------
// ListAbi::SerializeThrough's element functions for each instantiation: how its loader builds the temporary (Init),
// assigns the new element from it (Assign) and destroys it (Destroy). The lists' NewNode is the
// instantiation's (the
// ABIs below where game_lists.h's build the element otherwise).

// Engine object pointers: the temporary null; the holder's assignment (the new object gains a reference, the
// old one
// released); a virtual release.
inline void InitObjectPtr(CEngineObject** p) { *p = nullptr; }
inline void AssignObjectPtr(CEngineObject** to, CEngineObject* const* from) { HeldEngineObject::Of(to)->Assign(*from); }
// Script object references: the old one released and cleared, then the copy constructor.
inline void InitObjectRef(ScriptObjectRef* p) { p->object = nullptr; }
inline void AssignObjectRef(ScriptObjectRef* to, const ScriptObjectRef* from) {
    gamearrays::ReleaseAndClear(to);
    to->Construct(*from);
}
inline void ReleaseObjectRef(ScriptObjectRef* p) { gamearrays::ReleaseOnly(&p->object); }
// A magic trap's victims: unless the same object, the old one released and cleared
// the new one gains a reference.
inline void AssignVictim(ScriptObjectRef* to, const ScriptObjectRef* from) {
    CEngineObject* object = from->object;
    if (object == to->object) return;
    gamearrays::ReleaseAndClear(to);
    if (object) ++object->refCount_;
    to->object = object;
}
// Counted pointers: the owner's (ShareAssign, then the owner's release) and the plain ones (Assign, then the plain
// release).
inline void InitRef(RefPtr* p) { p->ptr = nullptr; }
inline void ShareAssignRef(RefPtr* to, const RefPtr* from) { to->ShareAssign(from->ptr); }
inline void AssignRef(RefPtr* to, const RefPtr* from) { to->Assign(from->ptr); }

// The armour spells' records (ArmourSpellRecord, game_lists.h).
inline void InitArmourSpell(ArmourSpellRecord* p) {
    p->object = nullptr;
    p->value = 0;
}
inline void AssignArmourSpell(ArmourSpellRecord* to, const ArmourSpellRecord* from) {
    HeldEngineObject::Of(&to->object)->Assign(from->object);
    to->value = from->value;
    to->flag = from->flag;
}
inline void ReleaseArmourSpell(ArmourSpellRecord* p) { gamearrays::ReleaseOnly(&p->object); }

// The script's effects: copied as bytes (particle sources, lightnings) or by the FX source's
// assignment; destroyed by their destructors (the object leaves the world's timed objects).
inline void InitParticleSource(ScriptParticleSource* p) { p->object = nullptr; }
inline void CopyParticleSource(ScriptParticleSource* to, const ScriptParticleSource* from) {
    std::memcpy(static_cast<void*>(to), from, sizeof(ScriptParticleSource));
}
inline void InitFxSource(ScriptFxSource* p) {
    p->object = nullptr;
    new (&p->name) mfc::CString;
}
inline void AssignFxSource(ScriptFxSource* to, const ScriptFxSource* from) { to->ScriptFxSourceAssign(from); }
inline void InitLightning(ScriptLightning* p) {
    p->object = nullptr;
    p->id = -1;
}
inline void CopyLightning(ScriptLightning* to, const ScriptLightning* from) { *to = *from; }

// Command blocks (the commands' array header, then ScriptDecl's assignment), definitions, runs
// (their four leading fields zero, then the variables), groups.
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
    new (&p->name) mfc::CString;
    new (&p->value) mfc::CString;
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

// The sound cache's wave records: the record's assignment.
inline void AssignWave(WaveRecord* to, const WaveRecord* from) { to->Assign(from); }

// The lists whose NewNode builds the element otherwise than game_lists.h's (the zero fill, then:
// the name empty, 0 / -1, the commands' header, "None" / -1, both
// strings empty, the objects' header).
using ArmourSpells = ListAbi<ArmourSpellRecord>;
using FxSourceNodes = ListAbi<ScriptFxSource, EachElements<ScriptFxSource, InitFxSource, nullptr>>;
using LightningNodes = ListAbi<ScriptLightning, EachElements<ScriptLightning, InitLightning, nullptr>>;
using DeclNodes = ListAbi<ScriptDecl, EachElements<ScriptDecl, InitDecl, nullptr>>;
using TokenNodes = ListAbi<ScriptToken, mfc::impl::HelperElements<ScriptToken, ConstructTokens, nullptr>>;
using DefineNodes = ListAbi<ScriptDefine, ScriptDefineElements>;
using SharedObjectNodes = ListAbi<SharedObject, EachElements<SharedObject, InitSharedObject, nullptr>>;

// The loaders' element functions by instantiation (game_slots.cpp registers each Serialize).
using mfc::impl::ListTemporary;
using EngineObjectsThrough = ListTemporary<gamelists::EngineObjects, InitObjectPtr, AssignObjectPtr, gamearrays::ReleaseOnly>;
using ObjectRefsThrough = ListTemporary<gamelists::ObjectRefs, InitObjectRef, AssignObjectRef, ReleaseObjectRef>;
using VictimsThrough = ListTemporary<gamelists::ObjectRefs, InitObjectRef, AssignVictim, ReleaseObjectRef>;
using OwnedRefsThrough = ListTemporary<gamelists::OwnedRefs, InitRef, ShareAssignRef, gamelists::DestroyRef>;
using RefsThrough = ListTemporary<gamelists::Refs, InitRef, AssignRef, gamelists::ReleaseRef>;
using ArmourSpellsThrough = ListTemporary<ArmourSpells, InitArmourSpell, AssignArmourSpell, ReleaseArmourSpell>;
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
// Each tag is a CompiledCollection (lib/mfc_slots.h) over the destructor and Serialize registered for the table it
// replaces; the script's lists and arrays (game_lists.h / game_arrays.h) share one tag each.

using mfc::impl::CompiledCollection;

// Counted pointer lists of the world server: the connections AcceptNewPlayers takes.
struct RefListAClass final
    : CompiledCollection<RefListAClass, &gamelists::Refs::Destruct<RefListAClass>, &RefsThrough::Abi::SerializeThrough<RefsThrough>> {};
// A list of engine objects released by a virtual call.
struct ItemObjectListClass final
    : CompiledCollection<ItemObjectListClass, &gamelists::EngineObjects::Destruct<ItemObjectListClass>, &EngineObjectsThrough::Abi::SerializeThrough<EngineObjectsThrough>> {};
// The same in the client maps, and the lightning manager's new bolts.
struct ClientObjectList2Class final
    : CompiledCollection<ClientObjectList2Class, &gamelists::EngineObjects::Destruct<ClientObjectList2Class>, &EngineObjectsThrough::Abi::SerializeThrough<EngineObjectsThrough>> {};
// CList<ScriptObjectRef> (CScript's temporary objects, +0x1CC).
struct ObjectRefListClass final
    : CompiledCollection<ObjectRefListClass, &gamelists::ObjectRefs::Destruct<ObjectRefListClass>, &ObjectRefsThrough::Abi::SerializeThrough<ObjectRefsThrough>> {};
// A magic trap's victims (counted references).
struct VictimListClass final
    : CompiledCollection<VictimListClass, &gamelists::ObjectRefs::Destruct<VictimListClass>, &VictimsThrough::Abi::SerializeThrough<VictimsThrough>> {};
// Counted pointer lists of the world server.
struct RefListBClass final
    : CompiledCollection<RefListBClass, &gamelists::Refs::Destruct<RefListBClass>, &RefsThrough::Abi::SerializeThrough<RefsThrough>> {};
// A map object controller's counted pointer list.
struct OwnedRefListClass final
    : CompiledCollection<OwnedRefListClass, &gamelists::OwnedRefs::Destruct<OwnedRefListClass>, &OwnedRefsThrough::Abi::SerializeThrough<OwnedRefsThrough>> {};
// The world server's shared objects.
struct SharedObjectListClass final
    : CompiledCollection<SharedObjectListClass, &gamelists::SharedObjects::Destruct<SharedObjectListClass>, &SharedObjectsThrough::Abi::SerializeThrough<SharedObjectsThrough>> {};
// The armour spells (12-byte records holding an engine object).
struct ArmourSpellListClass final
    : CompiledCollection<ArmourSpellListClass, &gamelists::Holders::Destruct<ArmourSpellListClass>, &ArmourSpellsThrough::Abi::SerializeThrough<ArmourSpellsThrough>> {};
// The sound cache's CList<WaveRecord>.
struct WaveListClass final
    : CompiledCollection<WaveListClass, &gamelists::Waves::Destruct<WaveListClass>, &WavesThrough::Abi::SerializeThrough<WavesThrough>> {};

// CScript's lists.
struct ParticleSourceListClass final
    : CompiledCollection<ParticleSourceListClass, &gamelists::ParticleSources::Destruct<ParticleSourceListClass>, &ParticleSourcesThrough::Abi::SerializeThrough<ParticleSourcesThrough>> {};
struct FxSourceListClass final
    : CompiledCollection<FxSourceListClass, &gamelists::FxSources::Destruct<FxSourceListClass>, &FxSourcesThrough::Abi::SerializeThrough<FxSourcesThrough>> {};
struct LightningListClass final
    : CompiledCollection<LightningListClass, &gamelists::Lightnings::Destruct<LightningListClass>, &LightningsThrough::Abi::SerializeThrough<LightningsThrough>> {};
// The command blocks (+0x70 and +0x178).
struct DeclListClass final
    : CompiledCollection<DeclListClass, &DeclList::Destruct<DeclListClass>, &DeclsThrough::Abi::SerializeThrough<DeclsThrough>> {};
// The script instances (+0x140).
struct DefinitionListClass final
    : CompiledCollection<DefinitionListClass, &gamelists::Definitions::Destruct<DefinitionListClass>, &DefinitionsThrough::Abi::SerializeThrough<DefinitionsThrough>> {};
struct RunListClass final
    : CompiledCollection<RunListClass, &gamelists::Runs::Destruct<RunListClass>, &RunsThrough::Abi::SerializeThrough<RunsThrough>> {};
// CScript's temporary groups (+0x1B0).
struct GroupListClass final
    : CompiledCollection<GroupListClass, &GroupList::Destruct<GroupListClass>, &GroupsThrough::Abi::SerializeThrough<GroupsThrough>> {};
// The script parser's tokens.
struct TokenListClass final
    : CompiledCollection<TokenListClass, &gamelists::Tokens::Destruct<TokenListClass>, &TokensThrough::Abi::SerializeThrough<TokensThrough>> {};
// The script's #define list.
struct DefineListClass final
    : CompiledCollection<DefineListClass, &ScriptDefinesDestruct, &DefinesThrough::Abi::SerializeThrough<DefinesThrough>> {};

// A unit memory's object records.
struct MemoryObjectArrayClass final
    : CompiledCollection<MemoryObjectArrayClass, &ObjectValues::Destruct<MemoryObjectArrayClass>, &ObjectValues::Serialize> {};
// The client maps' engine object pointers.
struct ClientObjectPtrArrayClass final
    : CompiledCollection<ClientObjectPtrArrayClass, &gamearrays::ObjectPtrs::Destruct<ClientObjectPtrArrayClass>, &gamearrays::ObjectPtrs::Serialize> {};
// A map object client's effect records.
struct EffectObjectArrayClass final
    : CompiledCollection<EffectObjectArrayClass, &EffectObjects::Destruct<EffectObjectArrayClass>, &gamearrays::Particles::Serialize> {};
// CArray<SSCCmd> (a command block).
struct CommandArrayClass
    : CompiledCollection<CommandArrayClass, &Commands::Destruct<CommandArrayClass>, &BuiltCommands::Serialize> {};

} // namespace gameslots

namespace mfc::impl {
// A script block's commands: derived from CommandArrayClass.
struct ScriptCommandsClass final : gameslots::CommandArrayClass {
    static void* Vtable() { return CompiledVtable<ScriptCommandsClass>(); }
};
// The script blocks (ScriptDecl, 24 bytes: the commands' array at +0) of the command block lists and arrays.
using CommandBlock = WithArray<0x18, 0, ScriptCommandsClass>;
using CommandBlocks = InitElements<CommandBlock, InitWithArray<0x18, 0, ScriptCommandsClass>>;
using CommandBlockList = ListAbi<CommandBlock, CommandBlocks>;
using CommandBlockArray = ArrayAbi<CommandBlock, CommandBlocks>;
} // namespace mfc::impl

namespace gameslots {
inline void InitDecl(ScriptDecl* p) { mfc::impl::ConstructHeader<mfc::impl::ScriptCommandsClass>(&p->commands); }
} // namespace gameslots
