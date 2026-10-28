#pragma once

// The CArray<T> instantiations of game records: the network delta fields, the music themes, the
// sound cache's wave records, unit behaviours, patrols and zone exits by value, the global map's
// records and the script's command arrays. lib/mfc_array.h's templates over the game's element
// types, each element's constructor and destructor ours, called directly. Registered in

#include "lib/mfc_array.h"
#include "lib/mfc_slots.h"
#include "game/figure_mesh.h"
#include "game/game_lists.h"
#include "game/map_spec.h"
#include "game/material.h"
#include "game/net_delta.h"
#include "game/sector_map.h"
#include "game/script.h"
#include "game/sound.h"
#include "game/terrain_sector.h"
#include "game/world.h"
#include "game_data.h"

namespace recordarrays {

using mfc::impl::ArrayAbi;
using mfc::impl::Bytes;
using mfc::impl::EachElements;


// ---- The script's command arrays ------------------------------------------------------------------

// Commands: SSCCmd's constructor values, its destructor.
inline void InitCmd(SSCCmd* p) { p->Construct(); }
inline void DestroyCmd(SSCCmd* p) { ScriptCmdDestruct(p); }
using Cmds = ArrayAbi<SSCCmd, EachElements<SSCCmd, InitCmd, DestroyCmd>>;
// Nested calls (12 bytes): type and argument 0, a new constructed command (null when operator new
// fails); destroyed with their command deleted.
void InitSubCall(ScriptSubCall* p);
inline void DestroySubCall(ScriptSubCall* p) { p->DestroyElement(0); }
using SubCalls = ArrayAbi<ScriptSubCall, EachElements<ScriptSubCall, InitSubCall, DestroySubCall>>;
using Bindings = ArrayAbi<ScriptBinding>;
// A command's binding arrays: derived from BindingsBaseClass (mfc_slots.h).
struct BindingArrayClass final : mfc::impl::BindingsBaseClass {
    static void* Vtable() { return mfc::impl::CompiledVtable<BindingArrayClass>(); }
};
// Definitions, runs and command blocks
using Definitions = ArrayAbi<ScriptDefinition, gamelists::DefinitionElements>;
// CScript's definitions.
struct DefinitionArrayClass final
    : mfc::impl::CompiledCollection<DefinitionArrayClass, &gamearrays::Definitions::Destruct<DefinitionArrayClass>, &Definitions::Serialize> {};
using Runs = ArrayAbi<ScriptRun, EachElements<ScriptRun, nullptr, gamelists::DestroyRun>>;
using Decls = ArrayAbi<ScriptDecl, EachElements<ScriptDecl, nullptr, gamelists::DestroyDecl>>;


// ---- Destructors only the vtables point at -----------------------------------------------

// The nested calls' base array.
struct SubCallBaseClass
    : mfc::impl::CompiledCollection<SubCallBaseClass, &SubCalls::Destruct<SubCallBaseClass>, &SubCalls::Serialize> {};
// A command's nested calls: derived from SubCallBaseClass.
struct SubCallArrayClass final : SubCallBaseClass {
    static void* Vtable() { return mfc::impl::CompiledVtable<SubCallArrayClass>(); }
};
using TerrainTileLists = ArrayAbi<terrain::TileList, terrain::TileListElements>;
// The terrain sectors' tile lists (one vector per texture page).
struct TerrainTileListsClass final
    : mfc::impl::CompiledCollection<TerrainTileListsClass, &TerrainTileLists::Destruct<TerrainTileListsClass>, &TerrainTileLists::Serialize> {};
// Each record's point lights returned to the pool (SectorVertex::ReleaseLights, inlined).
using SectorVertices = ArrayAbi<terrain::SectorVertex, terrain::RecordElements>;
// A terrain sector's vertex records.
struct SectorVertexArrayClass final
    : mfc::impl::CompiledCollection<SectorVertexArrayClass, &SectorVertices::Destruct<SectorVertexArrayClass>, &SectorVertices::Serialize> {};

// The client world's water levels, CArray<CArray<float>> (mfc::impl:: ClientMapArrayClass): each element an
// empty CArray<float>, destroyed by the base's destructor (CArray<float>'s vtable, the storage freed).
using WaterLevelElement = mfc::impl::WithArray<0x14, 0, mfc::impl::FloatArrayClass>;
using WaterLevels =
    ArrayAbi<WaterLevelElement, EachElements<WaterLevelElement, mfc::impl::InitWithArray<0x14, 0, mfc::impl::FloatArrayClass>,
                                             mfc::impl::DestroyArrayAt<WaterLevelElement, 0, mfc::impl::FloatArrayClass>>>;
// The sector map's sectors (sector_map.h).
using Sectors = ArrayAbi<Sector, SectorElements>;

} // namespace recordarrays

// The tags lib/mfc_array.h only declares (their slots need the game's elements).
namespace mfc::impl {

// The client world's water levels (destructor).
struct ClientMapArrayClass final
    : CompiledCollection<ClientMapArrayClass, &recordarrays::WaterLevels::Destruct<ClientMapArrayClass>, &recordarrays::WaterLevels::Serialize> {};
// The AI map's sectors (the sector map's DestructSectors).
struct SectorArrayClass final
    : CompiledCollection<SectorArrayClass, &CSectorMap::DestructSectors, &recordarrays::Sectors::Serialize> {};

} // namespace mfc::impl
