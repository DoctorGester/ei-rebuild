#pragma once

#include "core_transform.h"

#include "lib/containers.h"
#include "lib/vecmath.h"
#include "lib/mfc.h"
#include "lib/stlp.h"

#include <cstddef>
#include <cstdint>

// Names are provisional: they describe what the code does, not names recovered from the original.

class CLight;
class DecalBuilder;

namespace terrain {
struct SectorData;   // terrain_sector.h: the full sector (TerrainSector names a few of its fields)
}

// A 32x32-cell block of terrain. Each sector holds two layers of 33x33 vertices (x, y, z): the
// ground, and the water surface when the sector has water.
struct TerrainSector {
    std::uint8_t unknown_000[0x10];
    std::uint32_t hasWater;         // +0x10
    std::uint8_t unknown_014[0x180 - 0x14];
    float* vertices;                // +0x180 2 x 33 x 33 x (x, y, z)
};

// The area a point light reaches (006CAA70): the sectors it covers, and the first / last vertex
// column and row inside the first / last sector.
struct LightArea {
    int firstSectorX, firstSectorY, lastSectorX, lastSectorY;
    int firstX, firstY, lastX, lastY;
};

// A point light registered with the landscape (006CA5D0): the light (held) and its area.
struct LandscapeLight {
    CLight* light;
    LightArea area;
};
static_assert(sizeof(LandscapeLight) == 0x24);

// Landscape: answers height queries for a world position. The vertex grid has unit spacing, but
// vertices can be displaced sideways, so a point may lie in a neighbouring cell. A counted engine
// object (0xAFC bytes, vtable 0073F6A8) on a CTransformObject base (00453400); the client world's
// Setup inlines its constructor up to 006C5620, so the members it initialises are declared.
class Terrain : public CTransformObject {
public:
    // 006C5430 (slot 2; terrain_landscape.cpp). Reset, the vertex tile table (006C7A80), the wave
    // phases, the sine table sin(i * pi / 256); position zero, rotation identity. Returns 1.
    int Create() override;
    // 006C54D0 (slot 3). Frees the map (006C7750), releases the registered lights and empties
    // their table, empties the sector cache list, then Reset.
    void Destroy() override;
    // 006C5620. The constructor's body: no map, no textures (the arrays freed), texts "none",
    // frame 1, no sun, waves upwards at strength 0, phase 2 pi, no ambient loops.
    void Reset();
    // 006C57E0. The water vertices' wave phases (33 x 33): 40 ripples at random centres (rand(),
    // radius 1..5) add (1 - d^2 / r^2) * pi / 2 inside them; each edge is then averaged with the
    // opposite one, so neighbouring sectors join.
    void BuildWavePhases();
    // 006C7D70 (RET 4). Wave strength clamped to 0..1: glow, height and speed scale with it; the
    // direction is normalised, its x and y scaled by the step asin(1 - s) + pi * 0.50025.
    void SetWaveStrength(float strength);
    // 006C7EB0 (RET 8). The direction, then the strength as SetWaveStrength.
    void SetWaves(const Vector3* direction, float strength);
    // 006C7A80 (forwarder). The table of the cells around each vertex (+0x1E8).
    void BuildVertexTiles();
    // 006C7750 (forwarder). Frees the map: ambient loops, sectors, textures, materials, grid.
    void ClearMap();

    // 006C8680. Height of `layer` (0 = ground, 1 = water surface) at (x, y).
    float HeightAt(float x, float y, int layer);

    // 006C85A0. Vertex (x, y, z) at grid position (ix, iy) of `layer`, or null outside the map or
    // where the sector has no such layer.
    const float* VertexAt(int ix, int iy, int layer);

    // 006C8140 (RET 0x10; terrain_landscape.cpp). Sector (sx, sy) when it has any of the `wanted`
    // stages built (1 loaded, 2 geometry, 4 lit), else null. Sectors live in an LRU cache: a hit
    // moves to the front (stamped with the frame); with `create` a missing sector is loaded (the
    // least recently used one reused when older than 240 frames, older ones freed) and the wanted
    // stages are built.
    TerrainSector* SectorAt(int sx, int sy, std::uint32_t wanted, int create);

    // 006C8930. Ground height at (x, y), and water depth above it (0 when dry).
    void GroundAndWaterDepth(float x, float y, float* ground, float* depth);

    // 006CA260 (RET 0xC, from the draw pass 00569E40). The sectors within `radius` of (x, y): the
    // first row in visibleRowFirst, per row the first and last column (the same for every row:
    // a square). Each bound is FISTP(coordinate -/+ radius - 0.5) / 32, clamped to the map.
    void SetVisibleSectors(float x, float y, float radius);

    // 006CB1E0 (slot 6, terrain_animation.cpp). The per-frame update: the light countdown
    // (006C8ED0 when it runs out), each cached lit sector's relight countdown, the ambient sounds
    // (flag 8), the wave phase and frame counter, then Animate for each drawn sector of the visible
    // rows. Returns 1.
    int RunTick() override;

    // 006C8ED0 (RET 8; terrain_animation.cpp). Takes `sun` and the ambient colour: the ray steps
    // of the shadow test from the direction towards the sun (components below 0.01 in size pushed
    // to 0.01), then with a light interval the lit cached sectors get relight countdowns spread
    // over it (sorted; 0 drops the lighting at once), without one every sector's lighting is dropped.
    void SetSun(CLight* sun, const Vector4* ambient);
    // 006CB390 (terrain_ambience.cpp). The ambient sounds around the listener (flag 8): the 3x3
    // sectors around it (loaded when needed) each vote a sound class from their texture class
    // counts and get a weight from their distance; up to three classes with weight go on. Each may
    // start a one-shot (ambient\<set>\<class>\<time of day>\<n>.wav) at a random spot of a random
    // sector of its class, and keeps a looping circle.wav at its volume and pan (the loop already
    // playing for that class is reused); the loops of classes no longer heard stop.
    void PlayAmbience();
    // 006CBCD0 (from 00578940). Clears flag 8 and stops the three loops.
    void StopAmbience();
    // 006CCF20 (RET 8; terrain_decal.cpp, from the shadow projector 0056C750). Slides the decal
    // quad (`corners` as DecalBuilder::Begin takes them) along the builder's slope until its lowest
    // point meets the lowest ground vertex of the cells under it (one cell of margin, clamped to
    // the map), then hands those cells' ground triangles to the builder (Reserve, then two
    // AddTerrainTriangle per cell, through the original 004D8490).
    void AddDecalTriangles(DecalBuilder* builder, float* const* corners);

    std::uint8_t unknown_054[0x94 - 0x54];
    std::uint32_t landscapeFlags;   // +0x094 terrain::TerrainFields::flags
    std::uint8_t unknown_098[0x40]; // +0x098 zeroed by Reset
    lib::AlignedArray<CEngineObject*> pageTextures;    // +0x0D8 per texture page (006C5AA0)
    lib::AlignedArray<CEngineObject*> pageMaterials;   // +0x0E8 per texture page (constructor 00570E10)
    const char* textF8;             // +0x0F8 CString
    const char* textFC;             // +0x0FC CString
    void* unknown_100;              // +0x100 CWorldClient::DrawPass draws nothing while it is null
    int sectorsX;                   // +0x104 map size in sectors
    int sectorsY;                   // +0x108
    float width;                    // +0x10C last valid vertex column
    float height;                   // +0x110 last valid vertex row
    float heightRange;              // +0x114 the highest terrain height (cursor picking starts at half of it)
    int unknown_118;                // +0x118 -1 after Reset
    int pages;                      // +0x11C texture pages (sector loading)
    int unknown_120;                // +0x120 -1 after Reset
    int unknown_124;
    int perPage;                    // +0x128 textures per page
    // Members constructed in place by Setup (their constructors' addresses).
    std::uint8_t part12C[0x10];     // +0x12C 00570E40
    std::uint8_t part13C[0x10];     // +0x13C 0056FD30
    std::uint8_t part14C[0x10];     // +0x14C 0056FD30
    std::uint8_t part15C[0x6C];     // +0x15C 006721D0
    float* uvTables[4];             // +0x1C8 terrain::TerrainFields
    float* uvTablesLast[4];         // +0x1D8
    std::uint8_t part1E8[0x10];     // +0x1E8 00570E70
    using SectorCache = mfc::CList<terrain::SectorData*>;
    SectorCache::Node*** sectorGrid;   // +0x1F8 [sx][sy]: the sector's cache node, or null
    SectorCache sectorCache;        // +0x1FC 00570EA0 (block size 10), most recently used first
    int frame;                      // +0x218 counted by the per-frame update
    int lightInterval;              // +0x21C frames between sun updates (006C8ED0)
    int lightCountdown;             // +0x220
    Vector4 ambient;                // +0x224
    float sunSlopeX;                // +0x234 z / x of the unit direction towards the sun
    float sunRunX;                  // +0x238 x / z
    float sunSlopeY;                // +0x23C z / y
    float sunRunY;                  // +0x240 y / z
    int sunStepX;                   // +0x244 FISTP(95 * x)
    int sunStepY;                   // +0x248
    std::uint8_t unknown_24C[0x258 - 0x24C];   // the vertex being lit (terrain::TerrainFields)
    CLight* sun;                    // +0x258
    using LightTable = stlp::hash_map<int, LandscapeLight, stlp::IntHash>;
    LightTable lights;              // +0x25C 00571050: the registered point lights by id
    int maxLightId;                 // +0x270 the highest id registered
    Vector3 waveDirection;          // +0x274 the wave fields: terrain::TerrainFields
    float waveHeight;               // +0x280
    float waveGlow;                 // +0x284
    float wavePhase;                // +0x288
    float waveSpeed;                // +0x28C
    lib::AlignedArray<float> wavePhases;   // +0x290 00570DE0
    float sine[512];                // +0x2A0
    int unknown_AA0;                // +0xAA0 0 after Reset
    int visibleRowFirst;            // +0xAA4 SetVisibleSectors
    lib::AlignedArray<int> visibleColumnFirst;   // +0xAA8 0056FD30, per visible row
    lib::AlignedArray<int> visibleColumnLast;    // +0xAB8 0056FD30
    int ambientSounds[3];           // +0xAC8 the looping circle sounds (handles, -1 none)
    int ambientClasses[3];          // +0xAD4 their sound classes, -1 none
    const char* textAE0;            // +0xAE0 CString: the ambient sound set (ambient\<set>\...)
    int extraPages;                 // +0xAE4 texture pages loaded after the map's own (006C5AA0)
    std::uint8_t partAE8[0x14];     // +0xAE8 005711C0
};
static_assert(offsetof(Terrain, pageTextures) == 0xD8 && offsetof(Terrain, pageMaterials) == 0xE8);
static_assert(offsetof(Terrain, uvTables) == 0x1C8 && offsetof(Terrain, maxLightId) == 0x270);
static_assert(offsetof(Terrain, textF8) == 0xF8);
static_assert(offsetof(Terrain, width) == 0x10C);
static_assert(offsetof(Terrain, part12C) == 0x12C);
static_assert(offsetof(Terrain, sectorGrid) == 0x1F8);
static_assert(offsetof(Terrain, sectorCache) == 0x1FC);
static_assert(offsetof(Terrain, frame) == 0x218);
static_assert(offsetof(Terrain, landscapeFlags) == 0x94);
static_assert(offsetof(Terrain, ambient) == 0x224 && offsetof(Terrain, sun) == 0x258);
static_assert(offsetof(Terrain, lights) == 0x25C && offsetof(Terrain, waveDirection) == 0x274);
static_assert(offsetof(Terrain, wavePhases) == 0x290 && offsetof(Terrain, sine) == 0x2A0);
static_assert(offsetof(Terrain, visibleRowFirst) == 0xAA4);
static_assert(offsetof(Terrain, visibleColumnLast) == 0xAB8);
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
