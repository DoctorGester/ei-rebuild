#pragma once

// The scalar deleting destructors (and the destructors they call) of the MFC collections whose
// elements the game destroys through its own code (ticket S6a; the library-only ones are
// lib/mfc_slots.h's). Registered in game_slots.cpp; checked in game by verify_mfc_slots.cpp until S21d.

#include "lib/mfc_slots.h"
#include "game/game_arrays.h"
#include "game/game_lists.h"
#include "game/script.h"
#include "game_data.h"

#include <cstring>
#include <new>

struct PlatformSet;   // game/world.h

namespace gameslots {

// A script group's destructor body as a free function (004C2290).
void __fastcall GroupDestruct(ScriptGroup* group);

// 00561840: CBridgeManager's slot 3 (Destroy): the face list emptied (Clear's body without its 1).
void __fastcall BridgeManagerDestroy(PlatformSet* set);

// ---- Elements -------------------------------------------------------------------------------------

// A unit memory's 8-byte records: an object reference (released through slot 4, then cleared) and
// a value.
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

// A command block's commands as SetSize builds them (004CE920: zeroed, then SSCCmd's constructor).
inline void InitCommand(SSCCmd* p) { p->Construct(); }
using BuiltCommands = ArrayAbi<SSCCmd, EachElements<SSCCmd, InitCommand, DestroyCommand>>;
// The map database's zone records: built as 004607E0 builds them, destroyed by ZoneRecordDestruct.
using ZoneRecordElement = mfc::impl::StringsAndArray<0x5C, 8, 0x48, mfc::impl::MapRecordItemsClass>;
inline void DestroyZoneRecordElement(ZoneRecordElement* p) { zone::ZoneRecordDestruct(reinterpret_cast<MapRecord*>(p)); }
using BuiltZoneRecords =
    ArrayAbi<ZoneRecordElement, EachElements<ZoneRecordElement, mfc::impl::InitStringsAndArray<0x5C, 8, 0x48, mfc::impl::MapRecordItemsClass>,
                                             DestroyZoneRecordElement>>;

// ---- The lists whose Serialize loads through a temporary (ticket S6d) -----------------------------
//
// SerializeCListThrough's element functions for each instantiation: how its loader builds the
// temporary (Init), assigns the new element from it (Assign) and destroys it (Destroy). The lists'
// NewNode is the instantiation's (the ABIs below where game_lists.h's build the element otherwise).

// Engine object pointers (00427690, 00571420): the temporary null; the holder's assignment (the new
// object gains a reference, the old one released); a release through slot 4.
inline void InitObjectPtr(CEngineObject** p) { *p = nullptr; }
inline void AssignObjectPtr(CEngineObject** to, CEngineObject* const* from) { ObjectHolderAssign(to, nullptr, *from); }
// Script object references (004C9350): the old one released and cleared, then the copy constructor.
inline void InitObjectRef(ScriptObjectRef* p) { p->object = nullptr; }
inline void AssignObjectRef(ScriptObjectRef* to, const ScriptObjectRef* from) {
    gamearrays::ReleaseAndClear(to);
    to->Construct(*from);
}
inline void ReleaseObjectRef(ScriptObjectRef* p) { gamearrays::ReleaseOnly(&p->object); }
// A magic trap's victims (0052C740): unless the same object, the old one released and cleared
// (0052B030), the new one gains a reference.
inline void AssignVictim(ScriptObjectRef* to, const ScriptObjectRef* from) {
    CEngineObject* object = from->object;
    if (object == to->object) return;
    gamearrays::ReleaseAndClear(to);
    if (object) ++object->refCount_;
    to->object = object;
}
// Counted pointers: the owner's (005576C0: ShareAssign, then the owner's release) and the plain
// ones (00598FD0, 00599DF0: Assign, then the plain release).
inline void InitRef(RefPtr* p) { p->ptr = nullptr; }
inline void ShareAssignRef(RefPtr* to, const RefPtr* from) { to->ShareAssign(from->ptr); }
inline void AssignRef(RefPtr* to, const RefPtr* from) { to->Assign(from->ptr); }

// The armour spells' 12-byte records (00558100): an engine object, a dword and a byte the
// assignment copies (the three bytes after it are left).
struct ArmourSpellRecord {
    CEngineObject* object;
    std::uint32_t value;
    std::uint8_t flag;
    std::uint8_t pad[3];
};
static_assert(sizeof(ArmourSpellRecord) == 12);
inline void InitArmourSpell(ArmourSpellRecord* p) {
    p->object = nullptr;
    p->value = 0;
}
inline void AssignArmourSpell(ArmourSpellRecord* to, const ArmourSpellRecord* from) {
    ObjectHolderAssign(&to->object, nullptr, from->object);
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
inline void InitDecl(ScriptDecl* p) { mfc::impl::ConstructHeader<mfc::impl::ScriptCommandsClass>(&p->commands); }
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

// The sound cache's wave records: the record's assignment (006BD1B0).
inline void AssignWave(WaveRecord* to, const WaveRecord* from) { to->Assign(from); }

// The lists whose NewNode builds the element otherwise than game_lists.h's (the zero fill, then:
// the name empty (004CEA80), 0 / -1, the commands' header (004CEB20), "None" / -1 (004CF380), both
// strings empty (004D6840), the objects' header (0059E330)).
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

// ---- The instantiations' vtables (C19: the compiler's) ----------------------------------------------
//
// Each tag is a CompiledCollection (lib/mfc_slots.h) over the destructor and Serialize registered for the game.exe
// table it replaces; the script's lists and arrays (game_lists.h / game_arrays.h had their own tags for the same
// addresses: one tag each now).

using mfc::impl::CompiledCollection;
using mfc::impl::SerializeCArray;
using mfc::impl::SerializeCListThrough;

// Counted pointer lists of the world server (00598F40): left to a later batch (its table is not derivable yet).
struct RefListAClass {
    static void* Vtable() { return ei::GamePointer<void>(0x0073FA4C); }
};
// A list of engine objects released through slot 4 (00427600).
struct ItemObjectListClass final
    : CompiledCollection<ItemObjectListClass, &gamelists::EngineObjects::Destruct<ItemObjectListClass>, &SerializeCListThrough<EngineObjectsThrough>> {};
// The same in the client maps (00571F10), and the lightning manager's new bolts.
struct ClientObjectList2Class final
    : CompiledCollection<ClientObjectList2Class, &gamelists::EngineObjects::Destruct<ClientObjectList2Class>, &SerializeCListThrough<EngineObjectsThrough>> {};
// CList<ScriptObjectRef> (CScript's temporary objects, +0x1CC).
struct ObjectRefListClass final
    : CompiledCollection<ObjectRefListClass, &gamelists::ObjectRefs::Destruct<ObjectRefListClass>, &SerializeCListThrough<ObjectRefsThrough>> {};
// A magic trap's victims (counted references).
struct VictimListClass final
    : CompiledCollection<VictimListClass, &gamelists::ObjectRefs::Destruct<VictimListClass>, &SerializeCListThrough<VictimsThrough>> {};
// Counted pointer lists of the world server (00599D60).
struct RefListBClass final
    : CompiledCollection<RefListBClass, &gamelists::Refs::Destruct<RefListBClass>, &SerializeCListThrough<RefsThrough>> {};
// A map object controller's counted pointer list (005575F0).
struct OwnedRefListClass final
    : CompiledCollection<OwnedRefListClass, &gamelists::OwnedRefs::Destruct<OwnedRefListClass>, &SerializeCListThrough<OwnedRefsThrough>> {};
// The world server's shared objects (0059A3C0).
struct SharedObjectListClass final
    : CompiledCollection<SharedObjectListClass, &gamelists::SharedObjects::Destruct<SharedObjectListClass>, &SerializeCListThrough<SharedObjectsThrough>> {};
// The armour spells (12-byte records holding an engine object).
struct ArmourSpellListClass final
    : CompiledCollection<ArmourSpellListClass, &gamelists::Holders::Destruct<ArmourSpellListClass>, &SerializeCListThrough<ArmourSpellsThrough>> {};
// The sound cache's CList<WaveRecord> (destructor 006BC870).
struct WaveListClass final
    : CompiledCollection<WaveListClass, &gamelists::Waves::Destruct<WaveListClass>, &SerializeCListThrough<WavesThrough>> {};

// CScript's lists (their destructors, S13b: 004C92C0, 004C8E20, 004C7560, 004C8520, 004C7E30, 004C6760, 004C6410,
// 004C6120).
struct ParticleSourceListClass final
    : CompiledCollection<ParticleSourceListClass, &gamelists::ParticleSources::Destruct<ParticleSourceListClass>, &SerializeCListThrough<ParticleSourcesThrough>> {};
struct FxSourceListClass final
    : CompiledCollection<FxSourceListClass, &gamelists::FxSources::Destruct<FxSourceListClass>, &SerializeCListThrough<FxSourcesThrough>> {};
struct LightningListClass final
    : CompiledCollection<LightningListClass, &gamelists::Lightnings::Destruct<LightningListClass>, &SerializeCListThrough<LightningsThrough>> {};
// The command blocks (+0x70 and +0x178).
struct DeclListClass final
    : CompiledCollection<DeclListClass, &DeclList::Destruct<DeclListClass>, &SerializeCListThrough<DeclsThrough>> {};
// The script instances (+0x140).
struct DefinitionListClass final
    : CompiledCollection<DefinitionListClass, &gamelists::Definitions::Destruct<DefinitionListClass>, &SerializeCListThrough<DefinitionsThrough>> {};
struct RunListClass final
    : CompiledCollection<RunListClass, &gamelists::Runs::Destruct<RunListClass>, &SerializeCListThrough<RunsThrough>> {};
// CScript's temporary groups (+0x1B0).
struct GroupListClass final
    : CompiledCollection<GroupListClass, &GroupList::Destruct<GroupListClass>, &SerializeCListThrough<GroupsThrough>> {};
// The script parser's tokens (004CA260).
struct TokenListClass final
    : CompiledCollection<TokenListClass, &gamelists::Tokens::Destruct<TokenListClass>, &SerializeCListThrough<TokensThrough>> {};
// The script's #define list (004D6340).
struct DefineListClass final
    : CompiledCollection<DefineListClass, &ScriptDefinesDestruct, &SerializeCListThrough<DefinesThrough>> {};

// A unit memory's object records.
struct MemoryObjectArrayClass final
    : CompiledCollection<MemoryObjectArrayClass, &ObjectValues::Destruct<MemoryObjectArrayClass>, &SerializeCArray<ObjectValues>> {};
// The client maps' engine object pointers.
struct ClientObjectPtrArrayClass final
    : CompiledCollection<ClientObjectPtrArrayClass, &gamearrays::ObjectPtrs::Destruct<ClientObjectPtrArrayClass>, &SerializeCArray<gamearrays::ObjectPtrs>> {};
// A map object client's effect records.
struct EffectObjectArrayClass final
    : CompiledCollection<EffectObjectArrayClass, &EffectObjects::Destruct<EffectObjectArrayClass>, &SerializeCArray<gamearrays::Particles>> {};
// CArray<SSCCmd> (a command block).
struct CommandArrayClass
    : CompiledCollection<CommandArrayClass, &Commands::Destruct<CommandArrayClass>, &SerializeCArray<BuiltCommands>> {};

} // namespace gameslots
