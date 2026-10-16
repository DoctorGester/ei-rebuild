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

namespace recordarrays {

using mfc::impl::ArrayAbi;
using mfc::impl::Bytes;
using mfc::impl::DeleteThroughFirst;
using mfc::impl::EachElements;

// ---- Scene records -------------------------------------------------------------------------------

// The figure's 0x220-byte parts (ConstructElements 00453240): a class 0x42 figure mesh as the figure
// factory builds one (figure_factory.cpp): CFigureMesh::ConstructBase, the mesh's arrays empty, its
// vtable (0073C1C4). Destroyed through slot 0 (00453210).
void InitFigurePart(CFigureMesh* p);
using FigureParts = ArrayAbi<CFigureMesh, EachElements<CFigureMesh, InitFigurePart, DeleteThroughFirst<CFigureMesh>>>;

// The scene's 0x9C-byte materials (00454E40): CMaterial's constructor (0068DBC0); destroyed through
// slot 0.
inline void InitMaterial(CMaterial* p) { p->Construct(); }
using Materials = ArrayAbi<CMaterial, EachElements<CMaterial, InitMaterial, DeleteThroughFirst<CMaterial>>>;

// The scene's meshes (004545C0) and nodes (00454F80): the constructor bodies the instantiations
// inline (scene_loader.cpp). Meshes are destroyed through slot 0 (00476F50), nodes by their
// destructor (004543E0; 00477100).
inline void InitSceneMesh(scene::SceneMesh* p) { p->Construct(); }
inline void InitSceneNode(scene::SceneNode* p) { p->Construct(); }
inline void DestroySceneNode(scene::SceneNode* p) { p->Destruct(); }
using SceneMeshes = ArrayAbi<scene::SceneMesh, EachElements<scene::SceneMesh, InitSceneMesh, DeleteThroughFirst<scene::SceneMesh>>>;
using SceneNodes = ArrayAbi<scene::SceneNode, EachElements<scene::SceneNode, InitSceneNode, DestroySceneNode>>;

// A mesh's face groups: built by FaceGroup::ConstructElements (00454DB0), each destroyed by
// FaceGroup::Destruct (00454C50; 00476F20 and 00474DB0 call it).
struct FaceGroupElements {
    static void Construct(scene::FaceGroup* p, int n) { scene::FaceGroup::ConstructElements(p, n); }
    static void Destruct(scene::FaceGroup* p, int n) {
        for (; n--; ++p) p->Destruct();
    }
};
using FaceGroups = ArrayAbi<scene::FaceGroup, FaceGroupElements>;
struct FaceGroupArrayClass {   // CArray<FaceGroup> (a mesh's +0x148; constructor 00454A50)
    static void* Vtable() { return reinterpret_cast<void*>(0x0073C2BC); }
};

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
struct ThemeArrayClass {   // CArray<MusicTheme> (CMusicSystem +0x1C)
    static void* Vtable() { return reinterpret_cast<void*>(CMusicSystem::kThemeArrayVtable); }
};

// The sound cache's wave records (0x1C bytes): the name an empty string with room for 8 bytes
// (006BE240), its buffer freed (006B90C0, the record's destructor; 006BE210 calls it).
void __fastcall DestroyWave(WaveRecord* p);
using Waves = ArrayAbi<WaveRecord, EachElements<WaveRecord, gamelists::InitWave, gamelists::DestroyWave>>;

// A unit's behaviour records (0x4C bytes): built by the constructor (0045D5B0; ConstructElements
// 00558E10), destroyed by the destructor body (0046C290; 00557890 inlines it).
inline void InitBehaviour(mapspec::Behaviour* p) { mapspec::BehaviourConstruct(p); }
inline void DestroyBehaviour(mapspec::Behaviour* p) { mapspec::BehaviourDestruct(p); }
using Behaviours = ArrayAbi<mapspec::Behaviour, EachElements<mapspec::Behaviour, InitBehaviour, DestroyBehaviour>>;
struct BehaviourArrayClass {   // CArray<Behaviour> (the server unit's brain)
    static void* Vtable() { return reinterpret_cast<void*>(0x0073F034); }
};

// GetAt by value (__thiscall (result, index), RET 8; returns result): the element copy-constructed
// into result. 0046EAB0 / its body 0046EFD0: a patrol (the position, then its waypoint array: storage
// for exactly the source's count, zero filled, the waypoints copied). 00584EA0 / 00587DD0: a zone
// exit (the fields, the target string copy-constructed).
mapspec::Patrol* __fastcall PatrolGetAt(const mfc::CArray<mapspec::Patrol>* self, void*, mapspec::Patrol* result,
                                        int index);
ZoneExit* __fastcall ExitGetAt(const mfc::CArray<ZoneExit>* self, void*, ZoneExit* result, int index);
// CStringArray's GetAt by value (004698C0, and 004698A0 calling it; RET 8, S16): the element
// copy-constructed into `result`, which is returned.
mfc::CString* __fastcall StringGetAt(const mfc::CArray<mfc::CString>* self, void*, mfc::CString* result, int index);

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
struct BindingArrayClass {   // a command's binding arrays (copy constructor 004CCCB0)
    static void* Vtable() { return reinterpret_cast<void*>(0x0073D49C); }
};
struct SubCallArrayClass {   // a command's nested calls (copy constructor 004CCD90)
    static void* Vtable() { return reinterpret_cast<void*>(0x0073D4A8); }
};
// Definitions (built by 004CEB80, destroyed by 004BE9D0), runs (004BCAE0) and command blocks
// (004BF7E0).
using Definitions = ArrayAbi<ScriptDefinition, gamelists::DefinitionElements>;
using Runs = ArrayAbi<ScriptRun, EachElements<ScriptRun, nullptr, gamelists::DestroyRun>>;
using Decls = ArrayAbi<ScriptDecl, EachElements<ScriptDecl, nullptr, gamelists::DestroyDecl>>;

// 004CC0C0 (RET 4): SetSize to the source's size, then each command assigned from a GetAt copy
// (004CD450), the copy destroyed.
void __fastcall CmdArrayCopy(Cmds::Array* self, void*, const Cmds::Array* source);
// 004C6D10 (RET 4): SetSize to the source's size, then each nested call's type, argument and
// command assigned from a GetAt copy (004C7510: the command deep-copied), the copy's command
// destroyed and deleted.
void __fastcall SubCallArrayCopy(SubCalls::Array* self, void*, const SubCalls::Array* source);
// 004CCCB0 / 004CCD90 (RET 4; return this): the array's vtable, storage for exactly the source's
// size (zero filled / nested calls built), then each element copied (the nested calls as the
// assignment does).
Bindings::Array* __fastcall BindingsCopyConstruct(Bindings::Array* self, void*, const Bindings::Array* source);
SubCalls::Array* __fastcall SubCallsCopyConstruct(SubCalls::Array* self, void*, const SubCalls::Array* source);

// ---- Destructors only the vtables point at (ticket S6c) ------------------------------------------

struct SceneNodeArrayClass {   // CArray<SceneNode> (destructor 004514B0)
    static void* Vtable() { return reinterpret_cast<void*>(0x0073C15C); }
};
struct SubCallBaseClass {   // the nested calls' base array (destructors 004C5180 / 004CCF70)
    static void* Vtable() { return reinterpret_cast<void*>(0x0073D418); }
};
struct DeltaFieldArrayClass {   // CArray<DeltaField> (destructor 0050FBC0)
    static void* Vtable() { return reinterpret_cast<void*>(0x0073DB94); }
};
struct TerrainAlignedArrayClass {   // the terrain's 16-byte records, an aligned block at +0 (006C4C40)
    static void* Vtable() { return reinterpret_cast<void*>(0x007441B4); }
};
using TerrainAlignedRecords = mfc::impl::AlignedBlockAt<0x10, 0>;
struct WaveListClass {   // the sound cache's CList<WaveRecord> (destructor 006BC870)
    static void* Vtable() { return reinterpret_cast<void*>(0x00744124); }
};
struct SectorVertexArrayClass {   // a terrain sector's vertex records (destructor 006C4690)
    static void* Vtable() { return reinterpret_cast<void*>(0x00744184); }
};
// Each record's point lights returned to the pool (SectorVertex::ReleaseLights, inlined).
using SectorVertices = ArrayAbi<terrain::SectorVertex, terrain::RecordElements>;

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
