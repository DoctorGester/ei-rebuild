#include "terrain.h"

#include "ei/hook.h"

// Not rewritten yet: forward to the original code.
__declspec(naked) float Terrain::HeightAt(float, float, int) EI_ORIGINAL(0x006C8680)
__declspec(naked) void Terrain::GroundAndWaterDepth(float, float, float*, float*) EI_ORIGINAL(0x006C8930)

float TerrainOwner::SurfaceHeight(float x, float y) {
    float ground, depth;
    terrain_->GroundAndWaterDepth(x, y, &ground, &depth);
    return ground + depth;
}
EI_REPLACE_METHOD(0x00564070, TerrainOwner::SurfaceHeight);
