// The sector spiral's tables (sector_map.h): the offsets of a 21 x 21 square of sectors ordered by distance
// (007C2FF8), the number of offsets within each distinct distance (007C2FC0) and the tree that finds a distance's
// place among them (007C2FD8). Each is built empty by a static initialiser in the list's order (the offsets first)
// and destroyed by the exit function the initialiser registers with the game's atexit; both are ported on the names
// and registered at their addresses (docs/notes/standalone.md, "The static initialisers' objects"). The fourth
// initialiser (005BF8E0) fills them through BuildSectorSpiral (sector_map.cpp). Here, not in sector_map.cpp, so the
// path vectors' replay (tests/vectors/replay_path.cpp) links the definitions.
#include "sector_map.h"

#include "game_arrays.h"

#include "ei/hook.h"
#include "originals_library.h"

static_assert(sizeof(mfc::CArray<int>) == 0x14 && sizeof(RadiusTree) == 0x1C && sizeof(mfc::CArray<SpiralOffset>) == 0x14);

constinit lib::StaticObject<mfc::CArray<int>> g_spiralLengths = /*007C2FC0*/ {};
constinit lib::StaticObject<RadiusTree> g_spiralRadii = /*007C2FD8*/ {};
constinit lib::StaticObject<mfc::CArray<SpiralOffset>> g_spiralOffsets = /*007C2FF8*/ {};

namespace {

using AtExitFunction = void(__cdecl*)();
void RegisterExit(std::uintptr_t exitFunction) { GameAtExit(ei::Code<AtExitFunction>(exitFunction)); }

void __cdecl ConstructSpiralOffsets() {   // 005BF810 (the constructor 005C18C0)
    mfc::impl::ConstructArrayOf<gamearrays::SpiralOffsetArrayClass>(*g_spiralOffsets);
    RegisterExit(0x005BF830);
}
void __cdecl DestroySpiralOffsets() {   // 005BF830 (the destructor 005C18E0)
    mfc::impl::DestructArrayOf<gamearrays::SpiralOffsetArrayClass>(*g_spiralOffsets);
}

void __cdecl ConstructSpiralRadii() {   // 005BF840 (the keys' constructor 005BF890; depth and mask stay zero)
    mfc::impl::ConstructArrayOf<mfc::impl::FloatArrayClass>(g_spiralRadii->keys);
    RegisterExit(0x005BF860);
}
void __cdecl DestroySpiralRadii() {   // 005BF860 (the keys' destructor 005BF870)
    mfc::impl::DestructArrayOf<mfc::impl::FloatArrayClass>(g_spiralRadii->keys);
}

void __cdecl ConstructSpiralLengths() {   // 005BF8B0 (the constructor 005C1760)
    mfc::impl::ConstructArrayOf<mfc::impl::IntArrayClass>(*g_spiralLengths);
    RegisterExit(0x005BF8D0);
}
void __cdecl DestroySpiralLengths() {   // 005BF8D0 (the destructor 004600D0)
    mfc::impl::DestructArrayOf<mfc::impl::IntArrayClass>(*g_spiralLengths);
}

} // namespace

EI_REPLACE(0x005BF810, ConstructSpiralOffsets);
EI_REPLACE(0x005BF830, DestroySpiralOffsets);
EI_REPLACE(0x005BF840, ConstructSpiralRadii);
EI_REPLACE(0x005BF860, DestroySpiralRadii);
EI_REPLACE(0x005BF8B0, ConstructSpiralLengths);
EI_REPLACE(0x005BF8D0, DestroySpiralLengths);
