#pragma once

// The CArray<T> instantiations of game records: the terrain sectors' tile lists and vertex records, the
// client world's water levels and the AI map's sectors. lib/mfc_array.h's templates over the game's element
// types, each element's constructor and destructor ours, called directly.

#include "lib/mfc_array.h"
#include "lib/mfc_slots.h"
#include "game/figure_mesh.h"
#include "game/map_spec.h"
#include "game/material.h"
#include "game/net_delta.h"
#include "game/sector_map.h"
#include "game/sound.h"
#include "game/terrain_sector.h"
#include "game/world.h"
#include "game_data.h"

namespace recordarrays {

using mfc::impl::ArrayAbi;
using mfc::impl::Bytes;
using mfc::impl::EachElements;


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
