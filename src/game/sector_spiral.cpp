// The sector spiral's tables (sector_map.h): the offsets of a 21 x 21 square of sectors ordered by distance,
// the number of offsets within each distinct distance and the tree that finds a distance's
// place among them. Each is built empty by a static initialiser in the list's order (the offsets first)
// and destroyed by the exit function the initialiser registers with atexit
// (docs/notes/standalone.md, "The static initialisers' objects"). The fourth
// initialiser fills them through BuildSectorSpiral (sector_map.cpp). Here, not in sector_map.cpp, so the
// path vectors' replay (tests/vectors/replay_path.cpp) links the definitions.
#include "sector_map.h"

#include "game_arrays.h"

static_assert(sizeof(mfc::CArray<int>) == 0x14 && sizeof(RadiusTree) == 0x1C && sizeof(mfc::CArray<SpiralOffset>) == 0x14);

constinit lib::StaticObject<mfc::CArray<int>> g_spiralLengths = /*007C2FC0*/ {};
constinit lib::StaticObject<RadiusTree> g_spiralRadii = /*007C2FD8*/ {};
constinit lib::StaticObject<mfc::CArray<SpiralOffset>> g_spiralOffsets = /*007C2FF8*/ {};

namespace {

using AtExitFunction = void(__cdecl*)();
void RegisterExit(AtExitFunction exitFunction) { libcrt::AtExit(exitFunction); }

void __cdecl DestroySpiralOffsets() {
    mfc::impl::DestructArrayOf<gamearrays::SpiralOffsetArrayClass>(*g_spiralOffsets);
}
void __cdecl DestroySpiralRadii() {
    mfc::impl::DestructArrayOf<mfc::impl::FloatArrayClass>(g_spiralRadii->keys);
}
void __cdecl DestroySpiralLengths() {
    mfc::impl::DestructArrayOf<mfc::impl::IntArrayClass>(*g_spiralLengths);
}
} // namespace

void __cdecl ConstructSpiralOffsets() {
    mfc::impl::ConstructArrayOf<gamearrays::SpiralOffsetArrayClass>(*g_spiralOffsets);
    RegisterExit(DestroySpiralOffsets);
}

void __cdecl ConstructSpiralRadii() {
    mfc::impl::ConstructArrayOf<mfc::impl::FloatArrayClass>(g_spiralRadii->keys);
    RegisterExit(DestroySpiralRadii);
}

void __cdecl ConstructSpiralLengths() {
    mfc::impl::ConstructArrayOf<mfc::impl::IntArrayClass>(*g_spiralLengths);
    RegisterExit(DestroySpiralLengths);
}
