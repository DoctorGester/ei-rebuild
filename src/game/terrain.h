#pragma once

#include "engine_object.h"

#include <cstddef>
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
// vertices can be displaced sideways, so a point may lie in a neighbouring cell. A counted engine
// object (0xAFC bytes, vtable 0073F6A8) on a CTransformObject base (00453400); the client world's
// Setup inlines its constructor up to 006C5620, so the members it initialises are declared.
class Terrain : public CEngineObject {
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

    std::uint8_t unknown_01C[0xD8 - 0x1C];   // the rest of the CTransformObject base
    int unknownD8;                  // +0x0D8 \ cleared by the constructor
    int unknownDC;                  // +0x0DC |
    int unknownE0;                  // +0x0E0 |
    int unknownE4;                  // +0x0E4 /
    std::uint8_t partE8[0x10];      // +0x0E8 (constructor 00570E10)
    const char* textF8;             // +0x0F8 CString
    const char* textFC;             // +0x0FC CString
    void* unknown_100;              // +0x100 CWorldClient::DrawPass draws nothing while it is null
    std::uint8_t unknown_104[0x10C - 0x104];
    float width;                    // +0x10C last valid vertex column
    float height;                   // +0x110 last valid vertex row
    std::uint8_t unknown_114[0x12C - 0x114];
    // Members constructed in place by Setup (their constructors' addresses).
    std::uint8_t part12C[0x10];     // +0x12C 00570E40
    std::uint8_t part13C[0x10];     // +0x13C 0056FD30
    std::uint8_t part14C[0x10];     // +0x14C 0056FD30
    std::uint8_t part15C[0x8C];     // +0x15C 006721D0
    std::uint8_t part1E8[0x14];     // +0x1E8 00570E70
    std::uint8_t part1FC[0x60];     // +0x1FC 00570EA0 (10)
    std::uint8_t part25C[0x34];     // +0x25C 00571050
    std::uint8_t part290[0xAA8 - 0x290];   // +0x290 00570DE0
    std::uint8_t partAA8[0x10];     // +0xAA8 0056FD30
    std::uint8_t partAB8[0x28];     // +0xAB8 0056FD30
    const char* textAE0;            // +0xAE0 CString
    std::uint8_t unknown_AE4[4];
    std::uint8_t partAE8[0x14];     // +0xAE8 005711C0
};
static_assert(offsetof(Terrain, unknownD8) == 0xD8);
static_assert(offsetof(Terrain, textF8) == 0xF8);
static_assert(offsetof(Terrain, width) == 0x10C);
static_assert(offsetof(Terrain, part12C) == 0x12C);
static_assert(offsetof(Terrain, part1FC) == 0x1FC);
static_assert(offsetof(Terrain, part25C) == 0x25C);
static_assert(offsetof(Terrain, part290) == 0x290);
static_assert(offsetof(Terrain, partAB8) == 0xAB8);
static_assert(offsetof(Terrain, textAE0) == 0xAE0);
static_assert(sizeof(Terrain) == 0xAFC);

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
