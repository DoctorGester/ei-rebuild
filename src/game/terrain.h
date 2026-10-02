#pragma once

// Names are provisional: they describe what the code does, not names recovered from the original.

// Landscape: answers height queries for a world position.
class Terrain {
public:
    // 006C8680. layer 0 = ground surface, 1 = water surface.
    float HeightAt(float x, float y, int layer);

    // 006C8930. Ground height at (x, y), and water depth above it (0 when dry).
    void GroundAndWaterDepth(float x, float y, float* ground, float* depth);
};

// Owner of the landscape (a world/map object). Only the fields used so far are declared.
class TerrainOwner {
public:
    // 00564070, vtable slot at 0073F48C. Height of whatever surface is on top: ground, or water
    // when the point is submerged. Used by the camera and object placement.
    float SurfaceHeight(float x, float y);

private:
    char unknown_000[0x180];
    Terrain* terrain_;        // +0x180
};
static_assert(sizeof(void*) == 4, "game.exe is 32-bit");
