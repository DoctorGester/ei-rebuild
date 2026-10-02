#pragma once

#include <cstdint>

// Names are provisional: they describe what the code does, not names recovered from the original.

// A 32x32-cell block of terrain. Each sector holds two layers of 33x33 vertices (x, y, z): the
// ground, and the water surface when the sector has water.
struct TerrainSector {
    std::uint8_t unknown_000[0x10];
    std::uint32_t hasWater;         // +0x10
    std::uint8_t unknown_014[0x180 - 0x14];
    float* vertices;                // +0x180 2 x 33 x 33 x (x, y, z)
};

// Landscape: answers height queries for a world position. The vertex grid has unit spacing, but
// vertices can be displaced sideways, so a point may lie in a neighbouring cell.
class Terrain {
public:
    // 006C8680. Height of `layer` (0 = ground, 1 = water surface) at (x, y).
    float HeightAt(float x, float y, int layer);

    // 006C85A0. Vertex (x, y, z) at grid position (ix, iy) of `layer`, or null outside the map or
    // where the sector has no such layer.
    const float* VertexAt(int ix, int iy, int layer);

    // 006C8140. Sector (sx, sy); not rewritten yet.
    TerrainSector* SectorAt(int sx, int sy, int unknown1, int unknown2);

    // 006C8930. Ground height at (x, y), and water depth above it (0 when dry).
    void GroundAndWaterDepth(float x, float y, float* ground, float* depth);

    std::uint8_t unknown_000[0x10C];
    float width;                    // +0x10C last valid vertex column
    float height;                   // +0x110 last valid vertex row
};

// 006BE3A0. Height at (x, y) of the plane through triangle v0 v1 v2 ((x, y, z) each); the average
// of the three heights when the triangle is degenerate. A free __fastcall function.
float __fastcall PlaneHeight(const float* v0, const float* v1, float x, float y, const float* v2);

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
