#pragma once

#include "core_transform.h"
#include "resource.h"

#include "lib/containers.h"
#include "lib/vecmath.h"
#include "lib/mfc.h"
#include "lib/stlp.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class CLight;
class CMaterial;
class CSceneRenderer;
class CTexture;
class DecalBuilder;

namespace terrain {
struct SectorData;
struct SectorPosition;
struct TerrainMaterial;

// The tiles (of the sector's 16 x 16) that share a vertex; only the first `count` are set.
struct VertexTiles {
    int count;
    int tiles[4];
};
static_assert(sizeof(VertexTiles) == 0x14);

struct LevelArrayElements {
    static void Construct(mfc::CArray<float>* arrays, int count);
    static void Destruct(mfc::CArray<float>* arrays, int count);
};

// For every sound class with its own vote, how many of ambient\<set>\<class>\day\1.wav, 2.wav, ... the wave
// sources have, counted until the first one missing.
void CountAmbientVariants(const char* set);
}

// The area a point light reaches: the sectors it covers, and the first / last vertex column and row inside the
// first / last sector.
struct LightArea {
    int firstSectorX, firstSectorY, lastSectorX, lastSectorY;
    int firstX, firstY, lastX, lastY;
};

// The first vertex truncated, the last rounded.
void __fastcall LightAreaFromBounds(int sectorsX, int sectorsY, float minX, float minY, float maxX, float maxY,
                                    LightArea* area);
// (x0, y0) is clamped to the map; a first vertex on a sector edge counts as vertex 32 of the sector before.
void __fastcall SetLightArea(int x0, int y0, int x1, int y1, int sectorsX, int sectorsY, LightArea* area);

// A point light registered with the landscape (held).
struct LandscapeLight {
    CLight* light;
    LightArea area;
};
static_assert(sizeof(LandscapeLight) == 0x24);

// Landscape: answers height queries for a world position. The vertex grid has unit spacing, but vertices
// can be displaced sideways, so a point may lie in a neighbouring cell. The client world's Setup makes it.
class Terrain final : public CTransformObject {
public:
    Terrain();
    ~Terrain() override;

    // Returns 1.
    int Create() override;
    void Destroy() override;
    const char* GetClassName() override;    // "CTerrain"
    int GetClassId() override;
    // The constructor's body: no map, texts "none", frame 1, waves upwards at strength 0, phase 2 pi.
    void Reset();
    // 40 ripples at random centres (rand(), radius 1..5) add (1 - d^2 / r^2) * pi / 2 inside them; each edge
    // is then averaged with the opposite one, so neighbouring sectors join.
    void BuildWavePhases();
    // Clamped to 0..1: glow, height and speed scale with it; the direction's x and y are scaled by the step
    // asin(1 - s) + pi * 0.50025.
    void SetWaveStrength(float strength);
    void SetWaves(const Vector3* direction, float strength);
    // `count` pages <map>NNN, then up to two extra pages <map>L00 / L01 while their .mmp exists. 0 when a
    // texture does not load, else 1.
    int SetupPages(int count);
    // Rebirth redirects its ListFiles call; this is the vanilla body. The ambient sound set is "Dungeon" when
    // `dungeon`, else "Ingos" / "Suslanger" by `zone`, else "Gipat". 0 when the archive does not open or a
    // texture does not load; a missing or malformed map file is FatalError's result after ClearMap.
    int LoadMap(const char* path, const char* zone, char dungeon);
    // Per vertex of a sector (33 x 33): two tiles across a tile edge, four at a corner, one inside.
    void BuildVertexTiles();
    // No map: sizes -1, mapLoaded 0.
    void ClearMap();

    // Point lights: the landscape is the light manager. Nothing happens before a map is loaded.
    // Registers the light by id with its area (held) and relights the area.
    void AddLight(CLight* light);
    void RemoveLight(CLight* light);
    // Before the light moves by `delta`: the area grows by the move and is relit with the light moved, then
    // becomes the one at the new position (the light itself is left where it was).
    void MoveLight(CLight* light, const Vector3* delta);
    // Every registered light whose area covers the newly built sector.
    void LightSector(terrain::SectorData* sector);
    // The light's square (position +- range), widened by `delta` on the side it points to.
    void LightAreaOf(CLight* light, const Vector3* delta, LightArea* area);
    // The area's lit sectors (their point lights only).
    void RelightArea(const LightArea* area);

    // UpdateWorld calls this before it moves the position, so worldMatrix holds the position of the update
    // before.
    void CombineRotation(const Quaternion& parent) override;
    // Negative: no countdown, otherwise the countdown is raised to the interval.
    void SetLightInterval(int frames);
    // The change is added to the material and to every sector's level table. Nothing for a material the map
    // does not have.
    void SetWaterLevel(int material, float level);
    // 9 u then 9 v (rows of three), turned `rotation` quarter turns; with `wholePage` the v run covers the
    // page's whole height (the last pages hold one row). `this` is not used.
    void BuildTileUv(float* uv, int rotation, int column, int row, float size, float margin, char wholePage);

    // Flag 4: 255, less 255 * depth / fade for a point `depth` inside the fade band (MapEdgeFade, plus one
    // cell) of the map's borders, corners by the distance.
    std::uint8_t EdgeFade(const Vector3* point);
    // Whether the sun reaches vertex lightingX / lightingY of lightingSector from height `position`->z: the
    // ray is walked over the vertex grid, loading the sectors it crosses; 0 when a vertex is above the ray, 1
    // at the ray's end or the map's edge. A sun at or below the horizon: its direction's z sign bit.
    int SunReaches(const terrain::SectorPosition* position);

    // 9 u, then 9 v.
    const float* TileUv(int rotation, int index, int page);
    // Texture class (sound / prints material): the water tile's when the point is under water, 8 outside the
    // map.
    int SurfaceClass(float x, float y);

    // Pages 0 .. pages + 1.
    void DrawGround(CSceneRenderer* renderer);
    // Pages pages + 2 .. 2 * pages + 3.
    void DrawWater(CSceneRenderer* renderer);
    // The visible sectors not culled, per page; the page's texture is page % (pages + 2). 0 without a map or
    // when a draw fails, else 1.
    int DrawPages(CSceneRenderer* renderer, int firstPage, int lastPage);
    // From the squared distance of the centre (mid height) to the camera: 0 within DetailNear, 1 within
    // DetailFar, else 2; never below minDetail.
    void ChooseDetail(terrain::SectorData* sector);

    // Height of `layer` (0 = ground, 1 = water surface) at (x, y).
    float HeightAt(float x, float y, int layer);

    // Null outside the map or where the sector has no such layer.
    const Vector3* VertexAt(int ix, int iy, int layer);

    // Sector (sx, sy) when it has any of the `wanted` stages built (1 loaded, 2 geometry, 4 lit), else
    // null. Sectors live in an LRU cache: a hit moves to the front (stamped with the frame); with `create`
    // a missing sector is loaded (the least recently used one reused when older than 240 frames, older ones
    // freed) and the wanted stages are built.
    terrain::SectorData* SectorAt(int sx, int sy, std::uint32_t wanted, int create);

    // Ground height at (x, y), and water depth above it (0 when dry).
    void GroundAndWaterDepth(float x, float y, float* ground, float* depth);

    // A square of sectors: each bound is FISTP(coordinate -/+ radius - 0.5) / 32, clamped to the map.
    void SetVisibleSectors(float x, float y, float radius);

    // The light and relight countdowns, the ambient sounds, the waves, then Animate for each drawn sector of
    // the visible rows. Returns 1.
    int RunTick() override;

    // The shadow test's ray steps from the direction towards the sun (components below 0.01 pushed to 0.01);
    // with a light interval the lit sectors get relight countdowns spread over it (sorted; 0 drops the
    // lighting at once), without one every sector's lighting is dropped.
    void SetSun(CLight* sun, const Vector4* ambient);
    // Flag 8: the 3x3 sectors around the listener vote a sound class, weighted by distance; up to three
    // classes play a looping circle.wav and random one-shots (ambient\<set>\<class>\<time of day>\<n>.wav).
    void PlayAmbience();
    // Clears flag 8 and stops the three loops.
    void StopAmbience();
    // Slides the decal quad along the builder's slope until its lowest point meets the lowest ground vertex
    // of the cells under it (one cell of margin), then hands those cells' ground triangles to the builder.
    void AddDecalTriangles(DecalBuilder* builder, float* const* corners);

    Matrix4 drawMatrix;             // rotation, position in the last column
    // 1 water waves, 2 animated tiles, 4 fade the light at the map edges, 8 ambient sounds, 0x10 sun shadows.
    std::uint32_t landscapeFlags;
    Matrix4 worldMatrix;            // rows; position in the last row
    std::vector<CTexture*> pageTextures;     // held
    std::vector<CMaterial*> pageMaterials;   // held ("Wood")
    std::string textF8;
    std::string textFC;
    int mapLoaded;                  // nothing is drawn or lit while 0
    int sectorsX;                   // map size in sectors
    int sectorsY;
    float width;                    // last valid vertex column
    float height;                   // last valid vertex row
    float heightRange;              // the highest terrain height (cursor picking starts at half of it)
    int pageSize;                   // a texture page's side in pixels; -1 after Reset
    int pages;
    int tileSize;                   // a tile texture's side in pixels; -1 after Reset
    int tilesPerRow;
    int perPage;                    // tilesPerRow squared
    std::vector<terrain::TerrainMaterial> materials;   // the water materials
    // Per tile texture: its class 0..15 (sounds, prints), with sixteen entries of class 15 after the map's
    // own; its frame count when animated, else 0.
    std::vector<int> textureClass;
    std::vector<int> textureFrames;
    CMMIOResource archive;          // the map's sector archive
    float* uvTables[4];             // per rotation
    float* uvTablesLast[4];         // the same for the last two pages
    std::vector<terrain::VertexTiles> vertexTiles;   // 33 x 33
    using SectorCache = mfc::CList<terrain::SectorData*>;
    SectorCache::Node*** sectorGrid;   // [sx][sy]: the sector's cache node, or null
    SectorCache sectorCache;        // most recently used first
    int frame;
    int lightInterval;              // frames between sun updates
    int lightCountdown;
    Vector4 ambient;
    float sunSlopeX;                // z / x of the unit direction towards the sun
    float sunRunX;                  // x / z
    float sunSlopeY;                // z / y
    float sunRunY;                  // y / z
    int sunStepX;                   // FISTP(95 * x)
    int sunStepY;
    terrain::SectorData* lightingSector;   // the vertex being lit, for SunReaches
    int lightingX;
    int lightingY;
    CLight* sun;
    using LightTable = stlp::hash_map<int, LandscapeLight, stlp::IntHash>;
    LightTable lights;              // by id
    int maxLightId;
    Vector3 waveDirection;          // unit vector; x, y: the waves' travel
    float waveHeight;
    float waveGlow;                 // scales the crests' specular colour
    float wavePhase;                // advanced by waveSpeed every frame
    float waveSpeed;
    std::vector<float> wavePhases;   // per water vertex (33 x 33) phase offsets
    float sine[512];                // sin(i * pi / 256)
    int minDetail;                  // the finest level of detail ChooseDetail gives
    int visibleRowFirst;
    std::vector<int> visibleColumnFirst;   // per visible row
    std::vector<int> visibleColumnLast;
    int ambientSounds[3];           // the looping circle sounds (handles, -1 none)
    int ambientClasses[3];          // their sound classes, -1 none
    std::string textAE0;           // the ambient sound set (ambient\<set>\...)
    int extraPages;                 // texture pages loaded after the map's own
    mfc::CArray<mfc::CArray<float>, terrain::LevelArrayElements> waterLevels;   // per sector, per material
};
static_assert(offsetof(Terrain, drawMatrix) == 0x54 && offsetof(Terrain, worldMatrix) == 0x98);
static_assert(offsetof(Terrain, landscapeFlags) == 0x94);

// The average of the three heights when the triangle is degenerate.
float __fastcall PlaneHeight(const Vector3* v0, const Vector3* v1, float x, float y, const Vector3* v2);

static_assert(sizeof(void*) == 4, "game.exe is 32-bit");
