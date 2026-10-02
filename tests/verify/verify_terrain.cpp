// Differential check for TerrainOwner::SurfaceHeight: every call runs the original and the
// rewritten function with the same arguments and logs any result that differs bit for bit.
#include "verify.h"

#include "../../src/game/terrain.h"

// 00564070 starts with MOV ECX,[ECX+0x180] (6 bytes, no relative operands).
static float(__fastcall* s_original)(TerrainOwner*, void*, float, float);

static float __fastcall SurfaceHeightVerify(TerrainOwner* self, void*, float x, float y) {
    float expected = s_original(self, nullptr, x, y);
    float actual = self->SurfaceHeight(x, y);
    ei::verify::Check("TerrainOwner::SurfaceHeight", expected, actual, "x=%g y=%g", x, y);
    return expected;
}
EI_VERIFY(0x00564070, SurfaceHeightVerify, 6, s_original);

// Terrain sampling: pure functions, compared directly. Each original starts with a 6-byte prologue
// of plain pushes, moves and SUB ESP.
static float(__fastcall* s_heightAt)(Terrain*, void*, float, float, int);
static const float*(__fastcall* s_vertexAt)(Terrain*, void*, int, int, int);
static float(__fastcall* s_planeHeight)(const float*, const float*, float, float, const float*);

static float __fastcall HeightAtVerify(Terrain* self, void*, float x, float y, int layer) {
    float expected = s_heightAt(self, nullptr, x, y, layer);
    ei::verify::Check("Terrain::HeightAt", expected, self->HeightAt(x, y, layer), "x=%g y=%g layer %d", x, y, layer);
    return expected;
}
EI_VERIFY(0x006C8680, HeightAtVerify, 6, s_heightAt);

static const float* __fastcall VertexAtVerify(Terrain* self, void*, int ix, int iy, int layer) {
    const float* expected = s_vertexAt(self, nullptr, ix, iy, layer);
    ei::verify::Check("Terrain::VertexAt", expected, self->VertexAt(ix, iy, layer), "%d,%d layer %d", ix, iy, layer);
    return expected;
}
EI_VERIFY(0x006C85A0, VertexAtVerify, 6, s_vertexAt);

static float __fastcall PlaneHeightVerify(const float* v0, const float* v1, float x, float y, const float* v2) {
    float expected = s_planeHeight(v0, v1, x, y, v2);
    ei::verify::Check("PlaneHeight", expected, PlaneHeight(v0, v1, x, y, v2), "x=%g y=%g", x, y);
    return expected;
}
EI_VERIFY(0x006BE3A0, PlaneHeightVerify, 6, s_planeHeight);
