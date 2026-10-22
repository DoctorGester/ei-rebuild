#pragma once

// The CArray<T> instantiations of game records that S6 left (ticket S6b): the figure's parts, the
// scene's meshes, nodes, face groups and materials, the network delta fields, the music themes, the
// sound cache's wave records, unit behaviours, patrols and zone exits by value, the global map's
// records and the script's command arrays. lib/mfc_array.h's templates over the game's element
// types, each element's constructor and destructor ours, called directly. Registered in
// record_arrays.cpp; checked in game by verify_record_arrays.cpp until S21d.

#include "lib/mfc_array.h"
#include "lib/mfc_slots.h"
#include "game/figure_mesh.h"
#include "game/game_lists.h"
#include "game/map_spec.h"
#include "game/material.h"
#include "game/net_delta.h"
#include "game/scene_loader.h"
#include "game/sector_map.h"
#include "game/script.h"
#include "game/sound.h"
#include "game/terrain_sector.h"
#include "game/world.h"
#include "game_data.h"

namespace recordarrays {

using mfc::impl::ArrayAbi;
using mfc::impl::Bytes;
using mfc::impl::DeleteThroughFirst;
using mfc::impl::EachElements;

// ---- Scene records -------------------------------------------------------------------------------

// The fake unit's arrays (scene_loader.h has their element types): the figure's 0x220-byte parts
// (00453240), the scene's 0x9C-byte materials (00454E40), meshes (004545C0) and nodes (00454F80).
using FigureParts = ArrayAbi<CPlainFigureMesh, scene::FigurePartElements>;
using Materials = ArrayAbi<CMaterial, scene::HelperElements>;
using SceneMeshes = ArrayAbi<scene::SceneMesh, scene::MeshElements>;
using SceneNodes = ArrayAbi<scene::SceneNode, scene::NodeElements>;

// A mesh's face groups: built by FaceGroup::ConstructElements (00454DB0), each destroyed by
// FaceGroup::Destruct (00454C50; 00476F20 and 00474DB0 call it).
using FaceGroups = ArrayAbi<scene::FaceGroup, scene::FaceGroupElements>;
// CArray<FaceGroup> (a mesh's +0x148; constructor 00454A50), its vtable the compiler's (C19, mfc_slots.h).
struct FaceGroupArrayClass final
    : mfc::impl::CompiledCollection<FaceGroupArrayClass, &scene::FaceGroupArrayDestruct, &FaceGroups::Serialize> {};

// ---- Records -------------------------------------------------------------------------------------

// Network delta fields (0xA0 bytes; 005101A0): DeltaField::Construct (00677EC0's body), destroyed by
// DeltaField::Destruct (00510340).
inline void InitDeltaField(DeltaField* p) { p->Construct(); }
inline void DestroyDeltaField(DeltaField* p) { p->Destruct(); }
using DeltaFields = ArrayAbi<DeltaField, EachElements<DeltaField, InitDeltaField, DestroyDeltaField>>;

// Music themes (0x38 bytes): the names empty, both name arrays constructed (006B5BC0); destroyed
// from the last member to the first (006B4D90).
void InitTheme(MusicTheme* p);
void __fastcall DestroyTheme(MusicTheme* p);
using Themes = ArrayAbi<MusicTheme, EachElements<MusicTheme, InitTheme, DestroyTheme>>;
// CArray<MusicTheme> (CMusicSystem +0x1C).
struct ThemeArrayClass final
    : mfc::impl::CompiledCollection<ThemeArrayClass, &Themes::Destruct<ThemeArrayClass>, &Themes::Serialize> {};

// The sound cache's wave records (0x1C bytes): the name an empty string with room for 8 bytes
// (006BE240), its buffer freed (006B90C0, the record's destructor; 006BE210 calls it).
void __fastcall DestroyWave(WaveRecord* p);
using Waves = ArrayAbi<WaveRecord, EachElements<WaveRecord, gamelists::InitWave, gamelists::DestroyWave>>;

// A unit's behaviour records (0x4C bytes): built by the constructor (0045D5B0; ConstructElements
// 00558E10), destroyed by the destructor body (0046C290; 00557890 inlines it).
inline void InitBehaviour(mapspec::Behaviour* p) { mapspec::BehaviourConstruct(p); }
inline void DestroyBehaviour(mapspec::Behaviour* p) { mapspec::BehaviourDestruct(p); }
using Behaviours = ArrayAbi<mapspec::Behaviour, EachElements<mapspec::Behaviour, InitBehaviour, DestroyBehaviour>>;
// CArray<Behaviour> (the server unit's brain).
struct BehaviourArrayClass final
    : mfc::impl::CompiledCollection<BehaviourArrayClass, &Behaviours::Destruct<BehaviourArrayClass>, &Behaviours::Serialize> {};

// A fake unit's 0x220-byte figure parts, 0x18C-byte scene records and 0x9C-byte records (FakeUnitConstruct).
struct FakeUnitPartArrayClass final
    : mfc::impl::CompiledCollection<FakeUnitPartArrayClass, &mfc::impl::FigureParts::Destruct<FakeUnitPartArrayClass>, &FigureParts::Serialize> {};
struct FakeUnitSceneRecordArrayClass final
    : mfc::impl::CompiledCollection<FakeUnitSceneRecordArrayClass, &mfc::impl::SceneRecords::Destruct<FakeUnitSceneRecordArrayClass>,
                                    &SceneMeshes::Serialize> {};
struct FakeUnitRecord9CArrayClass final
    : mfc::impl::CompiledCollection<FakeUnitRecord9CArrayClass, &mfc::impl::FakeUnitRecords9C::Destruct<FakeUnitRecord9CArrayClass>,
                                    &Materials::Serialize> {};

// CStringArray over the part names of a map object (the server companion's, the object's own). 004698C0
// (and 004698A0 calling it; RET 8, S16): GetAt by value, the element copy-constructed into `result`,
// which is returned.
struct PartNames : mfc::CArray<mfc::CString> {
    mfc::CString* CopyAt(mfc::CString* result, int index) const;
};

// The global map's 0x18-byte records (mfc::impl::GlobalMapPointRecords): the destructor, the
// strings at +0x14 and +0x10 (005FBD80 / 005FBDD0).
void __fastcall DestroyGlobalMapRecord(Bytes<0x18>* p);
void __fastcall DestroyGlobalMapZone(Bytes<0x10>* p);   // 005FBD30

// ---- The script's command arrays ------------------------------------------------------------------

// Commands (0x94 bytes): SSCCmd's constructor values (004CE920), its destructor (004BF640).
inline void InitCmd(SSCCmd* p) { p->Construct(); }
inline void DestroyCmd(SSCCmd* p) { ScriptCmdDestruct(p); }
using Cmds = ArrayAbi<SSCCmd, EachElements<SSCCmd, InitCmd, DestroyCmd>>;
// Nested calls (12 bytes): type and argument 0, a new constructed command (null when operator new
// fails; 004CD130); destroyed with their command deleted (004CE230 with 0).
void InitSubCall(ScriptSubCall* p);
inline void DestroySubCall(ScriptSubCall* p) { p->DestroyElement(0); }
using SubCalls = ArrayAbi<ScriptSubCall, EachElements<ScriptSubCall, InitSubCall, DestroySubCall>>;
using Bindings = ArrayAbi<ScriptBinding>;
// A command's binding arrays (copy constructor 004CCCB0): derived from BindingsBaseClass (mfc_slots.h).
struct BindingArrayClass final : mfc::impl::BindingsBaseClass {
    static void* Vtable() { return mfc::impl::CompiledVtable<BindingArrayClass>(); }
};
// Definitions (built by 004CEB80, destroyed by 004BE9D0), runs (004BCAE0) and command blocks
// (004BF7E0).
using Definitions = ArrayAbi<ScriptDefinition, gamelists::DefinitionElements>;
// CScript's definitions (+0x8C; destructor 004CB310).
struct DefinitionArrayClass final
    : mfc::impl::CompiledCollection<DefinitionArrayClass, &gamearrays::Definitions::Destruct<DefinitionArrayClass>, &Definitions::Serialize> {};
using Runs = ArrayAbi<ScriptRun, EachElements<ScriptRun, nullptr, gamelists::DestroyRun>>;
using Decls = ArrayAbi<ScriptDecl, EachElements<ScriptDecl, nullptr, gamelists::DestroyDecl>>;


// ---- Destructors only the vtables point at (ticket S6c) ------------------------------------------

// CArray<SceneNode> (destructor 004514B0).
struct SceneNodeArrayClass final
    : mfc::impl::CompiledCollection<SceneNodeArrayClass, &SceneNodes::Destruct<SceneNodeArrayClass>, &SceneNodes::Serialize> {};
// The nested calls' base array (destructors 004C5180 / 004CCF70).
struct SubCallBaseClass
    : mfc::impl::CompiledCollection<SubCallBaseClass, &SubCalls::Destruct<SubCallBaseClass>, &SubCalls::Serialize> {};
// A command's nested calls (copy constructor 004CCD90): derived from SubCallBaseClass.
struct SubCallArrayClass final : SubCallBaseClass {
    static void* Vtable() { return mfc::impl::CompiledVtable<SubCallArrayClass>(); }
};
// CArray<DeltaField> (destructor 0050FBC0).
struct DeltaFieldArrayClass final
    : mfc::impl::CompiledCollection<DeltaFieldArrayClass, &DeltaFields::Destruct<DeltaFieldArrayClass>, &DeltaFields::Serialize> {};
using TerrainAlignedRecords = mfc::impl::AlignedBlockAt<0x10, 0>;
// The terrain's 16-byte records, an aligned block at +0 (006C4C40).
struct TerrainAlignedArrayClass final
    : mfc::impl::CompiledCollection<TerrainAlignedArrayClass, &TerrainAlignedRecords::Destruct<TerrainAlignedArrayClass>, &mfc::impl::AlignedArrays::Serialize> {};
// Each record's point lights returned to the pool (SectorVertex::ReleaseLights, inlined).
using SectorVertices = ArrayAbi<terrain::SectorVertex, terrain::RecordElements>;
// A terrain sector's vertex records (destructor 006C4690).
struct SectorVertexArrayClass final
    : mfc::impl::CompiledCollection<SectorVertexArrayClass, &SectorVertices::Destruct<SectorVertexArrayClass>, &SectorVertices::Serialize> {};

// The client world's water levels, CArray<CArray<float>> (vtable 0073F730, mfc::impl::
// ClientMapArrayClass; Serialize 00571240): each element an empty CArray<float> (00575690),
// destroyed by the base's destructor (005755C0: CArray<float>'s vtable, the storage freed).
using WaterLevelElement = mfc::impl::WithArray<0x14, 0, mfc::impl::FloatArrayClass>;
using WaterLevels =
    ArrayAbi<WaterLevelElement, EachElements<WaterLevelElement, mfc::impl::InitWithArray<0x14, 0, mfc::impl::FloatArrayClass>,
                                             mfc::impl::DestroyArrayAt<WaterLevelElement, 0, mfc::impl::FloatArrayClass>>>;
void __fastcall DestroyWaterLevel(WaterLevelElement* p);   // 005755C0
// The sector map's sectors (sector_map.h; Serialize 005993A0).
using Sectors = ArrayAbi<Sector, SectorElements>;

} // namespace recordarrays

// The tags lib/mfc_array.h only declares (their slots need the game's elements).
namespace mfc::impl {

// The client world's water levels (destructor 005711E0).
struct ClientMapArrayClass final
    : CompiledCollection<ClientMapArrayClass, &recordarrays::WaterLevels::Destruct<ClientMapArrayClass>, &recordarrays::WaterLevels::Serialize> {};
// The AI map's sectors (destructor 00599330, the sector map's DestructSectors).
struct SectorArrayClass final
    : CompiledCollection<SectorArrayClass, &CSectorMap::DestructSectors, &recordarrays::Sectors::Serialize> {};

} // namespace mfc::impl
