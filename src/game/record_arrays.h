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



} // namespace recordarrays

