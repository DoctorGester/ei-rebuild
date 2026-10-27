#pragma once

#include "core_transform.h"
#include "resource.h"

#include "lib/containers.h"
#include "lib/vecmath.h"
#include "lib/mfc.h"
#include "lib/stlp.h"

#include <cstddef>
#include <cstdint>

// Names are provisional: they describe what the code does.

class CLight;
class CMaterial;
class CSceneRenderer;
class CTexture;
class DecalBuilder;

namespace terrain {
struct SectorData;
struct SectorPosition;
struct TerrainMaterial;

// Vertex neighbourhood: the tiles (of the sector's 16 x 16) that share a vertex (Terrain::vertexTiles,
// 33 x 33 entries; only the first `count` tiles are set).
struct VertexTiles {
    int count;
    int tiles[4];
};
static_assert(sizeof(VertexTiles) == 0x14);

// ConstructElements / DestructElements of the landscape's per-sector level arrays : zeroed, then each an
// empty CArray<float>; destroyed, each frees its storage.
struct LevelArrayElements {
    static void Construct(mfc::CArray<float>* arrays, int count);
    static void Destruct(mfc::CArray<float>* arrays, int count);
};

// The ambient sound classes' one-shot counts for the sound set `set` (the map loader;
// terrain_ambience.cpp): for every class with its own vote, how many of ambient\<set>\<class>\ day\1.wav,
// 2.wav, ... the wave sources have, counted until the first one missing. The table is the game's.
// AmbientVariants reads a class's count.
void CountAmbientVariants(const char* set);
int AmbientVariants(int soundClass);
}

// The area a point light reaches: the sectors it covers, and the first / last vertex
// column and row inside the first / last sector.
struct LightArea {
    int firstSectorX, firstSectorY, lastSectorX, lastSectorY;
    int firstX, firstY, lastX, lastY;
};

// The area of the bounds (minX, minY)..(maxX, maxY): the first vertex truncated, the last rounded (_ftol of
// value / cell + 0.5), then SetLightArea.
void __fastcall LightAreaFromBounds(int sectorsX, int sectorsY, float minX, float minY, float maxX, float maxY,
                                    LightArea* area);
// The area from vertex (x0, y0) (clamped to the map) to (x1, y1): sector and
// vertex within it (x / 32, x % 32); a first vertex on a sector edge counts as vertex 32 of the
// sector before.
void __fastcall SetLightArea(int x0, int y0, int x1, int y1, int sectorsX, int sectorsY, LightArea* area);

// A point light registered with the landscape: the light (held) and its area.
struct LandscapeLight {
    CLight* light;
    LightArea area;
};
static_assert(sizeof(LandscapeLight) == 0x24);

// Landscape: answers height queries for a world position. The vertex grid has unit spacing, but vertices
// can be displaced sideways, so a point may lie in a neighbouring cell. A counted engine object (0xAFC
// bytes; a real class) on a CTransformObject base; the client world's Setup makes it.
class Terrain final : public CTransformObject {
public:
    Terrain();
    ~Terrain() override;

    // Create: reset, the vertex tile table, the wave phases, the sine table sin(i * pi / 256); position
    // zero, rotation identity. Returns 1.
    int Create() override;
    // Frees the map, releases the registered lights and empties
    // their table, empties the sector cache list, then Reset.
    void Destroy() override;
    const char* GetClassName() override;    // "CTerrain" (world_class_slots.cpp)
    int GetClassId() override;              // class id 0xB
    // The constructor's body: no map, no textures (the arrays freed), texts "none",
    // frame 1, no sun, waves upwards at strength 0, phase 2 pi, no ambient loops.
    void Reset();
    // The water vertices' wave phases (33 x 33): 40 ripples at random centres (rand(),
    // radius 1..5) add (1 - d^2 / r^2) * pi / 2 inside them; each edge is then averaged with the
    // opposite one, so neighbouring sectors join.
    void BuildWavePhases();
    // Wave strength clamped to 0..1: glow, height and speed scale with it; the
    // direction is normalised, its x and y scaled by the step asin(1 - s) + pi * 0.50025.
    void SetWaveStrength(float strength);
    // The direction, then the strength as SetWaveStrength.
    void SetWaves(const Vector3* direction, float strength);
    // The texture pages: `count` pages (<map>NNN, "%s%3.3i" of textF8), then up to two extra pages <map>L00
    // / L01 while their .mmp exists (counted in extraPages); each page's texture (kind 0xB, held, loaded)
    // and a "Wood" material (kind 0x12, held). 0 when a texture does not load, else 1.
    int SetupPages(int count);
    // Rebirth redirects its ListFiles call, so this is the vanilla body, hooked in the vanilla launch only.
    // Loads the map archive `path`: ClearMap, the archive opened (0 when it does not open), textFC the path
    // without its extension, textF8 the name of the archive's first "*.mp" file; that file's header gives
    // the sector grid (emptied), the sizes, the materials (opacity to absorption 1 / (1 - opacity) and a
    // byte, the colour lit and capped at 1, level 0), the tile textures' classes and frame counts; then the
    // pages (SetupPages; 0 and ClearMap when a texture does not load), the eight uv tables (BuildTileUv per
    // tile and rotation; a margin of 8 pixels), mapLoaded, the ambient sound set ("Dungeon" when `dungeon`,
    // else "Ingos" / "Suslanger" by `zone`, else "Gipat") with its one-shot counts, the low 16 flags set,
    // and a zeroed water level per sector and material. 1; a missing or malformed map file is FatalError's
    // result after ClearMap ("Unable to open file", "Wrong file format").
    int LoadMap(const char* path, const char* zone, char dungeon);
    // The table of the tiles around each vertex of a sector (sized to 33 x 33, rows of vertices): two tiles
    // across a tile edge, four at a corner, one inside.
    void BuildVertexTiles();
    // Frees the map: the ambient loops stopped, the cached sectors deleted (most recently used first), the
    // page textures and materials released and their arrays emptied, the archive closed, the sector grid
    // freed (no map: sizes -1, mapLoaded null), the uv tables freed.
    void ClearMap();

    // Point lights (terrain_lights.cpp; the landscape is the light manager). Nothing happens before a map is loaded
    // (mapLoaded 0).
    // Holds the light, registers it by id with its area, adds it to the vertices
    // in range of the area's built sectors and relights the area.
    void AddLight(CLight* light);
    // Drops the registration, takes the light off the vertices of its area,
    // relights the area and releases the light.
    void RemoveLight(CLight* light);
    // Before the light moves by `delta`: its area grows by the move, the vertices
    // in range of the new position get it, the area is relit with the light moved, then the area
    // becomes the one at the new position (the light itself is left where it was).
    void MoveLight(CLight* light, const Vector3* delta);
    // When a sector is built: every registered light whose area covers the sector is added to its vertices
    // in range.
    void LightSector(terrain::SectorData* sector);
    // The area of the light's square (position +- range), widened by `delta`
    // on the side it points to.
    void LightAreaOf(CLight* light, const Vector3* delta, LightArea* area);
    // Relights the area's lit sectors (their point lights only).
    void RelightArea(const LightArea* area);

    // The world-matrix update (terrain_landscape.cpp): the base's world rotation, then worldMatrix from it
    // and the world position (CTransformObject::WorldMatrix inlined; UpdateWorld calls this before it moves
    // the position, so the matrix holds the position of the update before).
    void CombineRotation(const Quaternion& parent) override;
    // Called from the zone load. The frames between sun updates; negative: no countdown, otherwise the
    // countdown is raised to the interval.
    void SetLightInterval(int frames);
    // Called from TweenArray::Load. Sets water material `material`'s level to `level`: the change is added
    // to the material and to its entry of every sector's level table. Nothing for a material the map does
    // not have.
    void SetWaterLevel(int material, float level);
    // Called from the map loader (`this` is not used). The texture coordinates of the tile at (column, row)
    // of a page whose tiles are `size` wide, `margin` inside each edge: 9 u then 9 v (rows of three),
    // turned `rotation` quarter turns; with `wholePage` the v run covers the page's whole height (the last
    // pages hold one row).
    void BuildTileUv(float* uv, int rotation, int column, int row, float size, float margin, char wholePage);

    // Light tests (terrain_shadow.cpp).
    // How much light a point keeps near the map's edges (flag 4): 255, less 255 * depth / fade for a point
    // `depth` inside the fade band (MapEdgeFade, plus one cell) of the map's borders, corners by the
    // distance.
    std::uint8_t EdgeFade(const Vector3* point);
    // Whether the sun reaches the vertex lightingX / lightingY of lightingSector
    // from height `position`->z: the ray is walked over the vertex grid towards the sun (sunStepX /
    // sunStepY: a line of max(|x|, |y|) steps, the height rising by sunSlopeX / sunSlopeY per
    // column / row), loading the sectors it crosses; 0 when a vertex is above the ray, 1 at the
    // ray's end or the map's edge. A sun at or below the horizon: its direction's z sign bit.
    int SunReaches(const terrain::SectorPosition* position);

    // terrain_sector.cpp:
    // The 18 floats (9 u, then 9 v) of texture `index` of `page` under `rotation`.
    const float* TileUv(int rotation, int index, int page);
    // Texture class (sound / prints material) at (x, y): the water tile's when the
    // point is under water, 8 outside the map.
    int SurfaceClass(float x, float y);

    // The draw passes (terrain_passes.cpp), from the client's draw.
    // The ground: drawMatrix from the local rotation and position pushed on the
    // renderer's pipeline stacks 2 and 1, the pages 0 .. pages + 1 drawn, the stacks popped.
    void DrawGround(CSceneRenderer* renderer);
    // The same for the water pages pages + 2 .. 2 * pages + 3.
    void DrawWater(CSceneRenderer* renderer);
    // 0 without a map. For the visible sectors (lit, loaded when needed): the
    // level of detail and the clip test; for those not culled the seams against coarser neighbours
    // and the projection; then per page firstPage .. lastPage every such sector with tiles on it
    // is drawn (DrawSectorPage), the page's texture (page % (pages + 2)) bound to stage 0 before
    // the first. 0 when a draw fails, else 1.
    int DrawPages(CSceneRenderer* renderer, int firstPage, int lastPage);
    // The sector's level of detail from the squared distance of its centre
    // (mid height) to the camera: 0 within DetailNear, 1 within DetailFar, else 2; never below
    // minDetail.
    void ChooseDetail(terrain::SectorData* sector);

    // Height of `layer` (0 = ground, 1 = water surface) at (x, y).
    float HeightAt(float x, float y, int layer);

    // Vertex (x, y, z) at grid position (ix, iy) of `layer`, or null outside the map or
    // where the sector has no such layer.
    const Vector3* VertexAt(int ix, int iy, int layer);

    // Sector (sx, sy) when it has any of the `wanted` stages built (1 loaded, 2 geometry, 4 lit), else
    // null. Sectors live in an LRU cache: a hit moves to the front (stamped with the frame); with `create`
    // a missing sector is loaded (the least recently used one reused when older than 240 frames, older ones
    // freed) and the wanted stages are built.
    terrain::SectorData* SectorAt(int sx, int sy, std::uint32_t wanted, int create);

    // Ground height at (x, y), and water depth above it (0 when dry).
    void GroundAndWaterDepth(float x, float y, float* ground, float* depth);

    // Called from the draw pass. The sectors within `radius` of (x, y): the first row in visibleRowFirst,
    // per row the first and last column (the same for every row: a square). Each bound is FISTP(coordinate
    // -/+ radius - 0.5) / 32, clamped to the map.
    void SetVisibleSectors(float x, float y, float radius);

    // The per-frame update (terrain_animation.cpp): the light countdown (SetSun when it runs out), each
    // cached lit sector's relight countdown, the ambient sounds (flag 8), the wave phase and frame counter,
    // then Animate for each drawn sector of the visible rows. Returns 1.
    int RunTick() override;

    // SetSun: takes `sun` and the ambient colour: the ray steps of the shadow test from the direction
    // towards the sun (components below 0.01 in size pushed to 0.01), then with a light interval the lit
    // cached sectors get relight countdowns spread over it (sorted; 0 drops the lighting at once), without
    // one every sector's lighting is dropped.
    void SetSun(CLight* sun, const Vector4* ambient);
    // The ambient sounds around the listener (flag 8): the 3x3 sectors around it (loaded when needed) each
    // vote a sound class from their texture class counts and get a weight from their distance; up to three
    // classes with weight go on. Each may start a one-shot (ambient\<set>\<class>\<time of day>\<n>.wav) at
    // a random spot of a random sector of its class, and keeps a looping circle.wav at its volume and pan
    // (the loop already playing for that class is reused); the loops of classes no longer heard stop.
    void PlayAmbience();
    // Clears flag 8 and stops the three loops.
    void StopAmbience();
    // Called from the shadow projector (terrain_decal.cpp): slides the decal quad (`corners` as
    // DecalBuilder::Begin takes them) along the builder's slope until its lowest point meets the lowest
    // ground vertex of the cells under it (one cell of margin, clamped to the map), then hands those cells'
    // ground triangles to the builder (Reserve, then two AddTerrainTriangle per cell).
    void AddDecalTriangles(DecalBuilder* builder, float* const* corners);

    Matrix4 drawMatrix;             // the draw passes' matrix (rotation, position in the last column)
    // +0x094 1 water waves, 2 animated tiles, 4 fade the light at the map edges, 8 ambient sounds,
    // 0x10 sun shadows.
    std::uint32_t landscapeFlags;
    Matrix4 worldMatrix;            // CombineRotation (rows; position in the last row); zeroed by Reset
    lib::AlignedArray<CTexture*> pageTextures;     // per texture page (SetupPages; held)
    lib::AlignedArray<CMaterial*> pageMaterials;   // per texture page (held)
    mfc::CString textF8;
    mfc::CString textFC;
    int mapLoaded;                  // 1 once LoadMap has succeeded (nothing is drawn or lit while 0)
    int sectorsX;                   // map size in sectors
    int sectorsY;
    float width;                    // last valid vertex column
    float height;                   // last valid vertex row
    float heightRange;              // the highest terrain height (cursor picking starts at half of it)
    int pageSize;                   // a texture page's side in pixels; -1 after Reset
    int pages;                      // texture pages (sector loading)
    int tileSize;                   // a tile texture's side in pixels; -1 after Reset
    int tilesPerRow;                // pageSize / tileSize
    int perPage;                    // textures per page: tilesPerRow squared
    // Members the constructor builds (their constructors' addresses).
    lib::AlignedArray<terrain::TerrainMaterial> materials;   // the water materials
    // +0x13C, +0x14C, per tile texture: its class 0..15 (sounds, prints), with sixteen entries of class 15
    // after the map's own; its frame count when animated, else 0.
    lib::AlignedArray<int> textureClass;
    lib::AlignedArray<int> textureFrames;
    CMMIOResource archive;          // the map's sector archive
    float* uvTables[4];             // per rotation, 0x48 bytes per texture of a page
    float* uvTablesLast[4];         // the same for the last two pages
    lib::AlignedArray<terrain::VertexTiles> vertexTiles;   // BuildVertexTiles
    using SectorCache = mfc::CList<terrain::SectorData*>;
    SectorCache::Node*** sectorGrid;   // [sx][sy]: the sector's cache node, or null
    SectorCache sectorCache;        // (block size 10), most recently used first
    int frame;                      // counted by the per-frame update
    int lightInterval;              // frames between sun updates
    int lightCountdown;
    Vector4 ambient;                // the ambient light colour (r, g, b, a)
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
    LightTable lights;              // the registered point lights by id
    int maxLightId;                 // the highest id registered
    Vector3 waveDirection;          // unit vector; x, y: the waves' travel
    float waveHeight;
    float waveGlow;                 // scales the crests' specular colour
    float wavePhase;                // advanced by waveSpeed every frame
    float waveSpeed;
    lib::AlignedArray<float> wavePhases;   // per water vertex (33 x 33) phase offsets
    float sine[512];                // sin(i * pi / 256)
    int minDetail;                  // the finest level of detail ChooseDetail gives; 0 after Reset
    int visibleRowFirst;            // SetVisibleSectors
    lib::AlignedArray<int> visibleColumnFirst;   // per visible row
    lib::AlignedArray<int> visibleColumnLast;
    int ambientSounds[3];           // the looping circle sounds (handles, -1 none)
    int ambientClasses[3];          // their sound classes, -1 none
    mfc::CString textAE0;           // the ambient sound set (ambient\<set>\...)
    int extraPages;                 // texture pages loaded after the map's own
    // +0xAE8 per sector: a level per water material.
    mfc::CArray<mfc::CArray<float>, terrain::LevelArrayElements> waterLevels;
};
static_assert(offsetof(Terrain, mapLoaded) == 0x100 && offsetof(Terrain, tilesPerRow) == 0x124);
static_assert(offsetof(Terrain, textureClass) == 0x13C && offsetof(Terrain, textureFrames) == 0x14C);
static_assert(offsetof(Terrain, pageTextures) == 0xD8 && offsetof(Terrain, pageMaterials) == 0xE8);
static_assert(offsetof(Terrain, uvTables) == 0x1C8 && offsetof(Terrain, maxLightId) == 0x270);
static_assert(offsetof(Terrain, textF8) == 0xF8 && offsetof(Terrain, textFC) == 0xFC);
static_assert(offsetof(Terrain, width) == 0x10C);
static_assert(offsetof(Terrain, materials) == 0x12C && offsetof(Terrain, archive) == 0x15C);
static_assert(offsetof(Terrain, drawMatrix) == 0x54 && offsetof(Terrain, worldMatrix) == 0x98);
static_assert(offsetof(Terrain, lightingSector) == 0x24C && offsetof(Terrain, minDetail) == 0xAA0);
static_assert(offsetof(Terrain, waterLevels) == 0xAE8);
static_assert(offsetof(Terrain, vertexTiles) == 0x1E8);
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

// Height at (x, y) of the plane through triangle v0 v1 v2 ((x, y, z) each); the average
// of the three heights when the triangle is degenerate. A free __fastcall function.
float __fastcall PlaneHeight(const Vector3* v0, const Vector3* v1, float x, float y, const Vector3* v2);

static_assert(sizeof(void*) == 4, "game.exe is 32-bit");
