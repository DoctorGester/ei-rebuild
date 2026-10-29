// The sector spiral's tables (sector_map.h): the offsets of a 21 x 21 square of sectors ordered by distance,
// the number of offsets within each distinct distance and the tree that finds a distance's
// place among them. A static initialiser fills them through BuildSectorSpiral (sector_map.cpp). Here, not in
// sector_map.cpp, so the path vectors' replay (tests/vectors/replay_path.cpp) links the definitions.
#include "sector_map.h"


#include <vector>

std::vector<int> g_spiralLengths;
RadiusTree g_spiralRadii;
std::vector<SpiralOffset> g_spiralOffsets;
