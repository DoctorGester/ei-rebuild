// The scalar deleting destructors of the MFC collections that destroy game elements (game_slots.h,
// ticket S6a): each calls its class's destructor (ours, registered elsewhere) directly.
#include "game/game_slots.h"

#include "ei/hook.h"

namespace gameslots {

using mfc::impl::ScalarDelete;

void __fastcall GroupDestruct(ScriptGroup* group) { group->DestructBody(); }

// ---- Destructors ----------------------------------------------------------------------------------

// Engine object lists (released through slot 4) and object reference lists (released and cleared).
EI_REPLACE(0x00427600, gamelists::EngineObjects::Destruct<ItemObjectListClass>);
EI_REPLACE(0x00571F10, gamelists::EngineObjects::Destruct<ClientObjectList2Class>);
EI_REPLACE(0x004C92C0, gamelists::ObjectRefs::Destruct<ObjectRefListClass>);
EI_REPLACE(0x0052C6B0, gamelists::ObjectRefs::Destruct<VictimListClass>);

// Object pointers and records holding objects.
EI_REPLACE(0x00570180, gamearrays::ObjectPtrs::Destruct<ClientObjectPtrArrayClass>);
EI_REPLACE(0x005989D0, gamearrays::ObjectPtrs::Destruct<gamearrays::ObjectPtrArrayBaseClass>);
EI_REPLACE(0x00556AD0, ObjectValues::Destruct<MemoryObjectArrayClass>);
EI_REPLACE(0x00556D90, ObjectValues::Destruct<MemoryObjectArrayClass>);
EI_REPLACE(0x00511BB0, EffectObjects::Destruct<EffectObjectArrayClass>);
EI_REPLACE(0x00558070, gamelists::Holders::Destruct<ArmourSpellListClass>);

// Counted pointer lists (the plain release).
EI_REPLACE(0x00598F40, gamelists::Refs::Destruct<RefListAClass>);
EI_REPLACE(0x00599D60, gamelists::Refs::Destruct<RefListBClass>);

// The script's point lights, effects, definitions, runs, groups and command blocks.
EI_REPLACE(0x004BE250, gamearrays::PointLights::Destruct<PointLightArrayClass>);
EI_REPLACE(0x004CB150, gamearrays::PointLights::Destruct<PointLightArrayClass>);
EI_REPLACE(0x004C6120, gamelists::ParticleSources::Destruct<ParticleSourceListClass>);
EI_REPLACE(0x004C6410, gamelists::FxSources::Destruct<FxSourceListClass>);
EI_REPLACE(0x004C6760, gamelists::Lightnings::Destruct<LightningListClass>);
EI_REPLACE(0x004BE2E0, Definitions::Destruct<DefinitionArrayClass>);
EI_REPLACE(0x004CB310, Definitions::Destruct<DefinitionArrayClass>);
EI_REPLACE(0x004C7E30, gamelists::Definitions::Destruct<DefinitionListClass>);
EI_REPLACE(0x004C8520, gamelists::Runs::Destruct<RunListClass>);
EI_REPLACE(0x004C8E20, GroupList::Destruct<GroupListClass>);
EI_REPLACE(0x004C7560, DeclList::Destruct<DeclListClass>);
EI_REPLACE(0x004C18E0, Commands::Destruct<CommandArrayClass>);
EI_REPLACE(0x004C54A0, Commands::Destruct<CommandArrayClass>);

// ---- Scalar deleting destructors (vtable slot 1) ---------------------------------------------------

// Over the destructors above.
EI_REPLACE(0x00429160, ScalarDelete<&gamelists::EngineObjects::Destruct<ItemObjectListClass>>);
EI_REPLACE(0x00571EF0, ScalarDelete<&gamelists::EngineObjects::Destruct<ClientObjectList2Class>>);
EI_REPLACE(0x004CB6D0, ScalarDelete<&gamelists::ObjectRefs::Destruct<ObjectRefListClass>>);
EI_REPLACE(0x0052CC30, ScalarDelete<&gamelists::ObjectRefs::Destruct<VictimListClass>>);
EI_REPLACE(0x00571E70, ScalarDelete<&gamearrays::ObjectPtrs::Destruct<ClientObjectPtrArrayClass>>);
EI_REPLACE(0x005989B0, ScalarDelete<&gamearrays::ObjectPtrs::Destruct<gamearrays::ObjectPtrArrayBaseClass>>);
EI_REPLACE(0x00556AB0, ScalarDelete<&ObjectValues::Destruct<MemoryObjectArrayClass>>);
EI_REPLACE(0x00558700, ScalarDelete<&ObjectValues::Destruct<MemoryObjectArrayClass>>);
EI_REPLACE(0x00511FF0, ScalarDelete<&EffectObjects::Destruct<EffectObjectArrayClass>>);
EI_REPLACE(0x00558820, ScalarDelete<&gamelists::Holders::Destruct<ArmourSpellListClass>>);
EI_REPLACE(0x0059B3F0, ScalarDelete<&gamelists::Refs::Destruct<RefListAClass>>);
EI_REPLACE(0x0059B490, ScalarDelete<&gamelists::Refs::Destruct<RefListBClass>>);
EI_REPLACE(0x004C1A20, ScalarDelete<&gamearrays::PointLights::Destruct<PointLightArrayClass>>);
EI_REPLACE(0x004CB130, ScalarDelete<&gamearrays::PointLights::Destruct<PointLightArrayClass>>);
EI_REPLACE(0x004CB1E0, ScalarDelete<&gamelists::ParticleSources::Destruct<ParticleSourceListClass>>);
EI_REPLACE(0x004CB200, ScalarDelete<&gamelists::FxSources::Destruct<FxSourceListClass>>);
EI_REPLACE(0x004CB290, ScalarDelete<&gamelists::Lightnings::Destruct<LightningListClass>>);
EI_REPLACE(0x004C1A40, ScalarDelete<&Definitions::Destruct<DefinitionArrayClass>>);
EI_REPLACE(0x004CB2F0, ScalarDelete<&Definitions::Destruct<DefinitionArrayClass>>);
EI_REPLACE(0x004CB440, ScalarDelete<&gamelists::Definitions::Destruct<DefinitionListClass>>);
EI_REPLACE(0x004CB550, ScalarDelete<&gamelists::Runs::Destruct<RunListClass>>);
EI_REPLACE(0x004CB6B0, ScalarDelete<&GroupList::Destruct<GroupListClass>>);
EI_REPLACE(0x004CB2B0, ScalarDelete<&DeclList::Destruct<DeclListClass>>);
EI_REPLACE(0x004C18C0, ScalarDelete<&Commands::Destruct<CommandArrayClass>>);
EI_REPLACE(0x004CB0F0, ScalarDelete<&Commands::Destruct<CommandArrayClass>>);

// Over destructors registered elsewhere.

// The script's variables, groups, tokens and defines.
EI_REPLACE(0x004C1840, ScalarDelete<&ScriptRefArrayDestruct>);   // 004BC9A0
EI_REPLACE(0x004C1860, ScalarDelete<&ScriptNamesDestruct>);   // 004BC810
EI_REPLACE(0x004C18A0, ScalarDelete<&ScriptGroupArrayDestruct>);   // 004BCA20
EI_REPLACE(0x004CAFB0, ScalarDelete<&GroupDestruct>);   // 004C2290
EI_REPLACE(0x004CB010, ScalarDelete<&gamearrays::Groups::Destruct<gamearrays::GroupArrayClass>>);   // 004C3B20
EI_REPLACE(0x004CB7B0, ScalarDelete<&gamelists::Tokens::Destruct<gamelists::TokenListClass>>);   // 004CA260
EI_REPLACE(0x004D6590, ScalarDelete<&ScriptDefinesDestruct>);   // 004D6340

// A map object controller's counted pointers, the world's shared object pointers.
EI_REPLACE(0x00558780, ScalarDelete<&gamelists::OwnedRefs::Destruct<gamelists::OwnedRefListClass>>);   // 005575F0
EI_REPLACE(0x0059B4D0, ScalarDelete<&gamearrays::ObjectPtrs::Destruct<gamearrays::ObjectPtrArrayBaseClass>>);   // 0059A100

} // namespace gameslots
