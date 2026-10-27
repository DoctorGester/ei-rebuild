// The scalar deleting destructors of the MFC collections that destroy game elements: each calls its
// class's destructor (ours, registered elsewhere) directly.
#include "game/game_slots.h"

#include "game/record_arrays.h"
#include "game/world.h"

#include "ei/hook.h"

namespace gameslots {

using mfc::impl::ScalarDeleting;


EI_REWRITTEN(0x004BE250, gamearrays::PointLights::Destruct<gamearrays::PointLightArrayClass>);
EI_REWRITTEN(0x004BE2E0, Definitions::Destruct<recordarrays::DefinitionArrayClass>);
EI_REWRITTEN(0x004C18E0, Commands::Destruct<CommandArrayClass>);
EI_REWRITTEN(0x00598F40, gamelists::Refs::Destruct<RefListAClass>);
EI_REWRITTEN(0x00599D60, gamelists::Refs::Destruct<RefListBClass>);
EI_REWRITTEN(0x00570180, gamearrays::ObjectPtrs::Destruct<ClientObjectPtrArrayClass>);
EI_REWRITTEN(0x005989D0, gamearrays::ObjectPtrs::Destruct<gamearrays::ObjectPtrArrayBaseClass>);
EI_REWRITTEN(0x00556AD0, ObjectValues::Destruct<MemoryObjectArrayClass>);
EI_REWRITTEN(0x00556D90, ObjectValues::Destruct<MemoryObjectArrayClass>);
EI_REWRITTEN(0x00511BB0, EffectObjects::Destruct<EffectObjectArrayClass>);
EI_REWRITTEN(0x00558070, gamelists::Holders::Destruct<ArmourSpellListClass>);
EI_REWRITTEN(0x00427600, gamelists::EngineObjects::Destruct<ItemObjectListClass>);
EI_REWRITTEN(0x006BC870, gamelists::Waves::Destruct<WaveListClass>);
EI_REWRITTEN(0x00571F10, gamelists::EngineObjects::Destruct<ClientObjectList2Class>);
EI_REWRITTEN(0x0052C6B0, gamelists::ObjectRefs::Destruct<VictimListClass>);
// ---- Scalar deleting destructors -------------------------------------------------------------------

// Over the destructors above.
EI_REWRITTEN(0x00429160, ScalarDeleting<&gamelists::EngineObjects::Destruct<ItemObjectListClass>>::Delete);
EI_REWRITTEN(0x00571EF0, ScalarDeleting<&gamelists::EngineObjects::Destruct<ClientObjectList2Class>>::Delete);
EI_REWRITTEN(0x004CB6D0, ScalarDeleting<&gamelists::ObjectRefs::Destruct<ObjectRefListClass>>::Delete);
EI_REWRITTEN(0x0052CC30, ScalarDeleting<&gamelists::ObjectRefs::Destruct<VictimListClass>>::Delete);
EI_REWRITTEN(0x00571E70, ScalarDeleting<&gamearrays::ObjectPtrs::Destruct<ClientObjectPtrArrayClass>>::Delete);
EI_REWRITTEN(0x005989B0, ScalarDeleting<&gamearrays::ObjectPtrs::Destruct<gamearrays::ObjectPtrArrayBaseClass>>::Delete);
EI_REWRITTEN(0x00556AB0, ScalarDeleting<&ObjectValues::Destruct<MemoryObjectArrayClass>>::Delete);
EI_REWRITTEN(0x00558700, ScalarDeleting<&ObjectValues::Destruct<MemoryObjectArrayClass>>::Delete);
EI_REWRITTEN(0x00511FF0, ScalarDeleting<&EffectObjects::Destruct<EffectObjectArrayClass>>::Delete);
EI_REWRITTEN(0x00558820, ScalarDeleting<&gamelists::Holders::Destruct<ArmourSpellListClass>>::Delete);
EI_REWRITTEN(0x006BD190, ScalarDeleting<&gamelists::Waves::Destruct<WaveListClass>>::Delete);
EI_REWRITTEN(0x0059B3F0, ScalarDeleting<&gamelists::Refs::Destruct<RefListAClass>>::Delete);
EI_REWRITTEN(0x0059B490, ScalarDeleting<&gamelists::Refs::Destruct<RefListBClass>>::Delete);
EI_REWRITTEN(0x004C1A20, ScalarDeleting<&gamearrays::PointLights::Destruct<gamearrays::PointLightArrayClass>>::Delete);
EI_REWRITTEN(0x004CB130, ScalarDeleting<&gamearrays::PointLights::Destruct<gamearrays::PointLightArrayClass>>::Delete);
EI_REWRITTEN(0x004CB1E0, ScalarDeleting<&gamelists::ParticleSources::Destruct<ParticleSourceListClass>>::Delete);
EI_REWRITTEN(0x004CB200, ScalarDeleting<&gamelists::FxSources::Destruct<FxSourceListClass>>::Delete);
EI_REWRITTEN(0x004CB290, ScalarDeleting<&gamelists::Lightnings::Destruct<LightningListClass>>::Delete);
EI_REWRITTEN(0x004C1A40, ScalarDeleting<&Definitions::Destruct<recordarrays::DefinitionArrayClass>>::Delete);
EI_REWRITTEN(0x004CB2F0, ScalarDeleting<&Definitions::Destruct<recordarrays::DefinitionArrayClass>>::Delete);
EI_REWRITTEN(0x004CB440, ScalarDeleting<&gamelists::Definitions::Destruct<DefinitionListClass>>::Delete);
EI_REWRITTEN(0x004CB550, ScalarDeleting<&gamelists::Runs::Destruct<RunListClass>>::Delete);
EI_REWRITTEN(0x004CB6B0, ScalarDeleting<&GroupList::Destruct<GroupListClass>>::Delete);
EI_REWRITTEN(0x004CB2B0, ScalarDeleting<&DeclList::Destruct<DeclListClass>>::Delete);
EI_REWRITTEN(0x004C18C0, ScalarDeleting<&Commands::Destruct<CommandArrayClass>>::Delete);
EI_REWRITTEN(0x004CB0F0, ScalarDeleting<&Commands::Destruct<CommandArrayClass>>::Delete);

// Over destructors registered elsewhere.

// The script's variables, groups, tokens and defines.
EI_REWRITTEN(0x004C1840, ScalarDeleting<&ScriptRefArrayDestruct>::Delete);
EI_REWRITTEN(0x004C1860, ScalarDeleting<&ScriptNamesDestruct>::Delete);
EI_REWRITTEN(0x004C18A0, ScalarDeleting<&ScriptGroupArrayDestruct>::Delete);
EI_REWRITTEN(0x004CAFB0, ScalarDeleting<&gamearrays::DestructGroupBody>::Delete);
EI_REWRITTEN(0x004CB010, ScalarDeleting<&gamearrays::Groups::Destruct<gamearrays::GroupArrayClass>>::Delete);
EI_REWRITTEN(0x004CB7B0, ScalarDeleting<&gamelists::Tokens::Destruct<TokenListClass>>::Delete);
EI_REWRITTEN(0x004D6590, ScalarDeleting<&ScriptDefinesDestruct>::Delete);

// A map object controller's counted pointers, the world's shared object pointers.
EI_REWRITTEN(0x00558780, ScalarDeleting<&gamelists::OwnedRefs::Destruct<OwnedRefListClass>>::Delete);
EI_REWRITTEN(0x0059B4D0, ScalarDeleting<&gamearrays::ObjectPtrs::Destruct<gamearrays::ObjectPtrArrayBaseClass>>::Delete);

// ---- Serialize -------------------------------------------------------------------------------------

// Arrays whose elements SetSize builds and destroys through the game's code.
EI_REWRITTEN(0x005701F0, gamearrays::ObjectPtrs::Serialize);   // the client maps' object pointers
EI_REWRITTEN(0x0059A170, gamearrays::ObjectPtrs::Serialize);   // the world's shared object pointers
EI_REWRITTEN(0x004C2310, gamearrays::ObjectRefs::Serialize);   // a script group (ScriptGroup::Serialize)
EI_REWRITTEN(0x004C3BE0, gamearrays::Groups::Serialize);       // the script's group variables
EI_REWRITTEN(0x004C5E60, gamearrays::PointLights::Serialize);  // the script's point lights
EI_REWRITTEN(0x004C9E20, gamearrays::Objectives::Serialize);   // a quest's objectives
EI_REWRITTEN(0x00460270, gamearrays::Exits::Serialize);        // a zone's exits
EI_REWRITTEN(0x0045FEB0, BuiltZoneRecords::Serialize);         // the map database's zones
EI_REWRITTEN(0x00511C30, gamearrays::Particles::Serialize);    // a map object client's effect particles
EI_REWRITTEN(0x00556E10, ObjectValues::Serialize);             // a unit memory's object records
EI_REWRITTEN(0x004C55C0, BuiltCommands::Serialize);            // a command block

// Lists whose loader builds, assigns and releases a temporary.
EI_REWRITTEN(0x00427690, EngineObjectsThrough::Abi::SerializeThrough<EngineObjectsThrough>);    // ItemObjectListClass
EI_REWRITTEN(0x00571420, EngineObjectsThrough::Abi::SerializeThrough<EngineObjectsThrough>);    // ClientObjectList2Class
EI_REWRITTEN(0x004C9350, ObjectRefsThrough::Abi::SerializeThrough<ObjectRefsThrough>);       // ObjectRefListClass
EI_REWRITTEN(0x0052C740, VictimsThrough::Abi::SerializeThrough<VictimsThrough>);          // VictimListClass
EI_REWRITTEN(0x005576C0, OwnedRefsThrough::Abi::SerializeThrough<OwnedRefsThrough>);        // gamelists::OwnedRefListClass
EI_REWRITTEN(0x00598FD0, RefsThrough::Abi::SerializeThrough<RefsThrough>);             // RefListAClass
EI_REWRITTEN(0x00599DF0, RefsThrough::Abi::SerializeThrough<RefsThrough>);             // RefListBClass
EI_REWRITTEN(0x00558100, ArmourSpellsThrough::Abi::SerializeThrough<ArmourSpellsThrough>);     // ArmourSpellListClass
EI_REWRITTEN(0x004C61C0, ParticleSourcesThrough::Abi::SerializeThrough<ParticleSourcesThrough>);  // ParticleSourceListClass
EI_REWRITTEN(0x004C64C0, FxSourcesThrough::Abi::SerializeThrough<FxSourcesThrough>);        // FxSourceListClass
EI_REWRITTEN(0x004C6800, LightningsThrough::Abi::SerializeThrough<LightningsThrough>);       // LightningListClass
EI_REWRITTEN(0x004C7640, DeclsThrough::Abi::SerializeThrough<DeclsThrough>);            // DeclListClass
EI_REWRITTEN(0x004C7F60, DefinitionsThrough::Abi::SerializeThrough<DefinitionsThrough>);      // DefinitionListClass
EI_REWRITTEN(0x004C8640, RunsThrough::Abi::SerializeThrough<RunsThrough>);             // RunListClass
EI_REWRITTEN(0x004C8F00, GroupsThrough::Abi::SerializeThrough<GroupsThrough>);           // GroupListClass
EI_REWRITTEN(0x004CA2E0, TokensThrough::Abi::SerializeThrough<TokensThrough>);           // gamelists::TokenListClass
EI_REWRITTEN(0x004D63E0, DefinesThrough::Abi::SerializeThrough<DefinesThrough>);          // DefineListClass
EI_REWRITTEN(0x0059A4A0, SharedObjectsThrough::Abi::SerializeThrough<SharedObjectsThrough>);    // gamelists::SharedObjectListClass
EI_REWRITTEN(0x006BC920, WavesThrough::Abi::SerializeThrough<WavesThrough>);            // the sound cache's CList<WaveRecord>

} // namespace gameslots

// The lib templates over tags the game's headers define: the registrations that need the complete tag.
namespace mfc::impl {

EI_REWRITTEN(0x004CEB20, CommandBlockArray::ConstructElements);
EI_REWRITTEN(0x004CD6D0, CommandBlockList::NewNode);
EI_REWRITTEN(0x00460470, ScalarDeleting<&ExitTargets::Destruct<ExitArrayClass>>::Delete);
EI_REWRITTEN(0x00460A60, ScalarDeleting<&ExitTargets::Destruct<ExitArrayClass>>::Delete);

} // namespace mfc::impl
