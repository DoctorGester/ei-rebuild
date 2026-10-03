// Checks for the terrain page draw (terrain_draw.cpp).
#include "verify.h"

#include "../../src/game/terrain_sector.h"

#include <cstdint>

int __fastcall DrawSectorPage(terrain::SectorData* s, void*, void* context, void* unused, int page, int unused2);

// 006BFF40 submits to Direct3D: ours only. Checked: the result is 0 or 1, and the shared vertex
// buffer (007C8890) holds four times the vertex count it was last sized for (its capacity is never
// below its size).
static void* s_drawSectorPage;
static int __fastcall DrawSectorPageVerify(terrain::SectorData* s, void*, void* context, void* unused, int page,
                                           int unused2) {
    int result = DrawSectorPage(s, nullptr, context, unused, page, unused2);
    const int* vertices = reinterpret_cast<const int*>(0x007C8890);   // data, size, capacity, growBy
    bool ok = (result == 0 || result == 1) && vertices[1] <= vertices[2] && vertices[1] % 4 == 0;
    ei::verify::Check("DrawSectorPage", true, ok, "page %d result %d size %d capacity %d", page, result, vertices[1],
                      vertices[2]);
    return result;
}
EI_VERIFY(0x006BFF40, DrawSectorPageVerify, 0, s_drawSectorPage);
