// The sector spiral's tables (sector_map.h): the offsets of a 21 x 21 square of sectors ordered by distance,
// the number of offsets within each distinct distance and the tree that finds a distance's
// place among them. Each is built empty by a static initialiser in the list's order (the offsets first)
// and destroyed by the exit function the initialiser registers with atexit
// (docs/notes/standalone.md, "The static initialisers' objects"). The fourth
// initialiser fills them through BuildSectorSpiral (sector_map.cpp). Here, not in sector_map.cpp, so the
// path vectors' replay (tests/vectors/replay_path.cpp) links the definitions.
#include "sector_map.h"


#include <cstdlib>
#include <vector>

constinit lib::StaticObject<std::vector<int>> g_spiralLengths = /*007C2FC0*/ {};
constinit lib::StaticObject<RadiusTree> g_spiralRadii = /*007C2FD8*/ {};
constinit lib::StaticObject<std::vector<SpiralOffset>> g_spiralOffsets = /*007C2FF8*/ {};

namespace {

void DestroySpiralOffsets() { g_spiralOffsets.Destroy(); }
void DestroySpiralRadii() { g_spiralRadii.Destroy(); }
void DestroySpiralLengths() { g_spiralLengths.Destroy(); }
} // namespace

void ConstructSpiralOffsets() {
    g_spiralOffsets.Construct();
    std::atexit(DestroySpiralOffsets);
}

void ConstructSpiralRadii() {
    g_spiralRadii.Construct();
    std::atexit(DestroySpiralRadii);
}

void ConstructSpiralLengths() {
    g_spiralLengths.Construct();
    std::atexit(DestroySpiralLengths);
}
