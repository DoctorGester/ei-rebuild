#pragma once

#include "core_transform.h"
#include "resource.h"

#include "lib/containers.h"
#include "lib/vecmath.h"
#include "lib/mfc.h"
#include "lib/stlp.h"

#include <cstddef>
#include <cstdint>

// Names are provisional: they describe what the code does, not names recovered from the original.

class CLight;
class CMaterial;
class CSceneRenderer;
class CTexture;
class DecalBuilder;

namespace terrain {
struct SectorData;   // terrain_sector.h
struct SectorPosition;
struct TerrainMaterial;

// Vertex neighbourhood: the tiles (of the sector's 16 x 16) that share a vertex (Terrain::vertexTiles,
// 33 x 33 entries; only the first `count` tiles are set).
struct VertexTiles {
    int count;
    int tiles[4];
};
static_assert(sizeof(VertexTiles) == 0x14);

// ConstructElements / DestructElements of the landscape's per-sector level arrays (00575690; the
// elements' destructor slot 005755A0 with flags 0): zeroed, then each an empty CArray<float>
// (vtable 0073D3E8); destroyed, each frees its storage.
struct LevelArrayElements {
    static void Construct(mfc::CArray<float>* arrays, int count);
    static void Destruct(mfc::CArray<float>* arrays, int count);
};

// The ambient sound classes' one-shot counts for the sound set `set` (the map loader 006C68B0;
// terrain_ambience.cpp): for every class with its own vote, how many of ambient\<set>\<class>\
// day\1.wav, 2.wav, ... the wave sources have, counted until the first one missing. The table is
// the game's (007969D0). AmbientVariants reads a class's count.
void CountAmbientVariants(const char* set);
int AmbientVariants(int soundClass);
}

// The area a point light reaches (006CAA70): the sectors it covers, and the first / last vertex
// column and row inside the first / last sector.
struct LightArea {
    int firstSectorX, firstSectorY, lastSectorX, lastSectorY;
    int firstX, firstY, lastX, lastY;
};

// 006BF580 (RET 0x14; terrain_lights.cpp). The area of the bounds (minX, minY)..(maxX, maxY): the
// first vertex truncated, the last rounded (_ftol of value / cell + 0.5), then SetLightArea.
void __fastcall LightAreaFromBounds(int sectorsX, int sectorsY, float minX, float minY, float maxX, float maxY,
                                    LightArea* area);
// 006BF4A0 (RET 0x14). The area from vertex (x0, y0) (clamped to the map) to (x1, y1): sector and
// vertex within it (x / 32, x % 32); a first vertex on a sector edge counts as vertex 32 of the
// sector before.
void __fastcall SetLightArea(int x0, int y0, int x1, int y1, int sectorsX, int sectorsY, LightArea* area);

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
    int CreateImpl();   // body of the slot above (registered by member pointer)
    // 006C54D0 (slot 3). Frees the map (006C7750), releases the registered lights and empties
    // their table, empties the sector cache list, then Reset.
    void Destroy() override;
    void DestroyImpl();   // body of the slot above (registered by member pointer)
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
    // 006C5AA0 (RET 4, from the map loader 006C68B0). The texture pages: `count` pages
    // (<map>NNN, "%s%3.3i" of textF8), then up to two extra pages <map>L00 / L01 while their .mmp
    // exists (counted in extraPages); each page's texture (kind 0xB, held, loaded) and a "Wood"
    // material (kind 0x12, held). 0 when a texture does not load, else 1.
    int SetupPages(int count);
    // 006C68B0 (RET 0xC; A-H26: Rebirth redirects its ListFiles call, so this is the vanilla body,
    // hooked in the vanilla launch only; a caller of ours must go through the address). Loads the
    // map archive `path`: ClearMap, the archive opened (0 when it does not open), textFC the path
    // without its extension, textF8 the name of the archive's first "*.mp" file; that file's
    // header gives the sector grid (emptied), the sizes, the materials (opacity to absorption 1 /
    // (1 - opacity) and a byte, the colour lit and capped at 1, level 0), the tile textures'
    // classes and frame counts; then the pages (SetupPages; 0 and ClearMap when a texture does not
    // load), the eight uv tables (BuildTileUv per tile and rotation; a margin of 8 pixels),
    // mapLoaded, the ambient sound set ("Dungeon" when `dungeon`, else "Ingos" / "Suslanger" by
    // `zone`, else "Gipat") with its one-shot counts, the low 16 flags set, and a zeroed water
    // level per sector and material. 1; a missing or malformed map file is FatalError's result
    // after ClearMap ("Unable to open file", "Wrong file format").
    int LoadMap(const char* path, const char* zone, char dungeon);
    // 006C7A80 (terrain_landscape.cpp). The table of the tiles around each vertex of a sector (sized
    // to 33 x 33, rows of vertices): two tiles across a tile edge, four at a corner, one inside.
    void BuildVertexTiles();
    // 006C7750 (terrain_landscape.cpp). Frees the map: the ambient loops stopped, the cached
    // sectors deleted (most recently used first), the page textures and materials released and
    // their arrays emptied, the archive closed, the sector grid freed (no map: sizes -1, mapLoaded
    // null), the uv tables freed.
    void ClearMap();

    // Point lights (terrain_lights.cpp; the light manager *007AF738 is the landscape). Nothing
    // happens before a map is loaded (mapLoaded 0).
    // 006CA5D0 (RET 4). Holds the light, registers it by id with its area, adds it to the vertices
    // in range of the area's built sectors and relights the area.
    void AddLight(CLight* light);
    // 006CA720 (RET 4). Drops the registration, takes the light off the vertices of its area,
    // relights the area and releases the light.
    void RemoveLight(CLight* light);
    // 006CA830 (RET 8). Before the light moves by `delta`: its area grows by the move, the vertices
    // in range of the new position get it, the area is relit with the light moved, then the area
    // becomes the one at the new position (the light itself is left where it was).
    void MoveLight(CLight* light, const Vector3* delta);
    // 006CA9C0 (RET 4, when a sector is built). Every registered light whose area covers the
    // sector is added to its vertices in range.
    void LightSector(terrain::SectorData* sector);
    // 006CAA70 (RET 0xC). The area of the light's square (position +- range), widened by `delta`
    // on the side it points to.
    void LightAreaOf(CLight* light, const Vector3* delta, LightArea* area);
    // 006CAB50 (RET 4). Relights the area's lit sectors (their point lights only).
    void RelightArea(const LightArea* area);

    // 006C6070 (slot 14, RET 4; terrain_landscape.cpp). The base's world rotation, then worldMatrix
    // from it and the world position (CTransformObject::WorldMatrix inlined; UpdateWorld calls this
    // before it moves the position, so the matrix holds the position of the update before).
    void CombineRotation(const Quaternion& parent) override;
    void CombineRotationImpl(const Quaternion& parent);   // body of the slot above (registered by member pointer)
    // 006C9290 (RET 4, from the zone load 00567AB0). The frames between sun updates; negative: no
    // countdown, otherwise the countdown is raised to the interval.
    void SetLightInterval(int frames);
    // 006CC7F0 (RET 8, from TweenArray::Load 006D45D0). Sets water material `material`'s level to
    // `level`: the change is added to the material and to its entry of every sector's level table.
    // Nothing for a material the map does not have.
    void SetWaterLevel(int material, float level);
    // 006C7940 (RET 0x1C, from the map loader 006C68B0; `this` is not used). The texture
    // coordinates of the tile at (column, row) of a page whose tiles are `size` wide, `margin`
    // inside each edge: 9 u then 9 v (rows of three), turned `rotation` quarter turns; with
    // `wholePage` the v run covers the page's whole height (the last pages hold one row).
    void BuildTileUv(float* uv, int rotation, int column, int row, float size, float margin, char wholePage);

    // Light tests (terrain_shadow.cpp).
    // 006CB020 (RET 4, AL). How much light a point keeps near the map's edges (flag 4): 255, less
    // 255 * depth / fade for a point `depth` inside the fade band (MapEdgeFade, 0079BD34, plus one
    // cell) of the map's borders, corners by the distance.
    std::uint8_t EdgeFade(const Vector3* point);
    // 006C92C0 (RET 4). Whether the sun reaches the vertex lightingX / lightingY of lightingSector
    // from height `position`->z: the ray is walked over the vertex grid towards the sun (sunStepX /
    // sunStepY: a line of max(|x|, |y|) steps, the height rising by sunSlopeX / sunSlopeY per
    // column / row), loading the sectors it crosses; 0 when a vertex is above the ray, 1 at the
    // ray's end or the map's edge. A sun at or below the horizon: its direction's z sign bit.
    int SunReaches(const terrain::SectorPosition* position);

    // terrain_sector.cpp:
    // 006C53C0. The 18 floats (9 u, then 9 v) of texture `index` of `page` under `rotation`.
    const float* TileUv(int rotation, int index, int page);
    // 006C8D40. Texture class (sound / prints material) at (x, y): the water tile's when the
    // point is under water, 8 outside the map.
    int SurfaceClass(float x, float y);

    // The draw passes (terrain_passes.cpp), from the client's draw 00569E40.
    // 006C6230 (RET 4). The ground: drawMatrix from the local rotation and position pushed on the
    // renderer's pipeline stacks 2 and 1, the pages 0 .. pages + 1 drawn, the stacks popped.
    void DrawGround(CSceneRenderer* renderer);
    // 006C6330 (RET 4). The same for the water pages pages + 2 .. 2 * pages + 3.
    void DrawWater(CSceneRenderer* renderer);
    // 006C6460 (RET 0xC). 0 without a map. For the visible sectors (lit, loaded when needed): the
    // level of detail and the clip test; for those not culled the seams against coarser neighbours
    // and the projection; then per page firstPage .. lastPage every such sector with tiles on it
    // is drawn (DrawSectorPage), the page's texture (page % (pages + 2)) bound to stage 0 before
    // the first. 0 when a draw fails, else 1.
    int DrawPages(CSceneRenderer* renderer, int firstPage, int lastPage);
    // 006C66E0 (RET 4). The sector's level of detail from the squared distance of its centre
    // (mid height) to the camera: 0 within DetailNear, 1 within DetailFar, else 2; never below
    // minDetail.
    void ChooseDetail(terrain::SectorData* sector);

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
    terrain::SectorData* SectorAt(int sx, int sy, std::uint32_t wanted, int create);

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
    int RunTickImpl();   // body of the slot above (registered by member pointer)

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
    // AddTerrainTriangle per cell).
    void AddDecalTriangles(DecalBuilder* builder, float* const* corners);

    // 00562B80 (S13a, world_destructors.cpp). The destructor's body: the vtable, Destroy, the
    // members last to first (the water level arrays, the ambient set's text, the aligned arrays'
    // blocks, the light table, the sector cache, the archive, the texts), then CTransformObject's
    // (its vtable, CEngineObject's body).
    void Destruct();
    // 00562B60 (slot 0): Destruct, then operator delete when bit 0 of `flags` is set. Returns this.
    Terrain* DeletingDestruct(unsigned flags);

    Matrix4 drawMatrix;             // +0x054 the draw passes' matrix (rotation, position in the last column)
    // +0x094 1 water waves, 2 animated tiles, 4 fade the light at the map edges, 8 ambient sounds,
    // 0x10 sun shadows.
    std::uint32_t landscapeFlags;
    Matrix4 worldMatrix;            // +0x098 CombineRotation (rows; position in the last row); zeroed by Reset
    lib::AlignedArray<CTexture*> pageTextures;     // +0x0D8 per texture page (SetupPages; held)
    lib::AlignedArray<CMaterial*> pageMaterials;   // +0x0E8 per texture page (constructor 00570E10; held)
    mfc::CString textF8;            // +0x0F8
    mfc::CString textFC;            // +0x0FC
    int mapLoaded;                  // +0x100 1 once LoadMap has succeeded (nothing is drawn or lit while 0)
    int sectorsX;                   // +0x104 map size in sectors
    int sectorsY;                   // +0x108
    float width;                    // +0x10C last valid vertex column
    float height;                   // +0x110 last valid vertex row
    float heightRange;              // +0x114 the highest terrain height (cursor picking starts at half of it)
    int pageSize;                   // +0x118 a texture page's side in pixels; -1 after Reset
    int pages;                      // +0x11C texture pages (sector loading)
    int tileSize;                   // +0x120 a tile texture's side in pixels; -1 after Reset
    int tilesPerRow;                // +0x124 pageSize / tileSize
    int perPage;                    // +0x128 textures per page: tilesPerRow squared
    // Members constructed in place by Setup (their constructors' addresses).
    lib::AlignedArray<terrain::TerrainMaterial> materials;   // +0x12C 00570E40 the water materials
    // +0x13C, +0x14C (0056FD30 both), per tile texture: its class 0..15 (sounds, prints), with
    // sixteen entries of class 15 after the map's own; its frame count when animated, else 0.
    lib::AlignedArray<int> textureClass;
    lib::AlignedArray<int> textureFrames;
    CMMIOResource archive;          // +0x15C the map's sector archive (006721D0)
    float* uvTables[4];             // +0x1C8 per rotation, 0x48 bytes per texture of a page
    float* uvTablesLast[4];         // +0x1D8 the same for the last two pages
    lib::AlignedArray<terrain::VertexTiles> vertexTiles;   // +0x1E8 (00570E70) BuildVertexTiles
    using SectorCache = mfc::CList<terrain::SectorData*>;
    SectorCache::Node*** sectorGrid;   // +0x1F8 [sx][sy]: the sector's cache node, or null
    SectorCache sectorCache;        // +0x1FC 00570EA0 (block size 10), most recently used first
    int frame;                      // +0x218 counted by the per-frame update
    int lightInterval;              // +0x21C frames between sun updates (006C8ED0)
    int lightCountdown;             // +0x220
    Vector4 ambient;                // +0x224 the ambient light colour (r, g, b, a)
    float sunSlopeX;                // +0x234 z / x of the unit direction towards the sun
    float sunRunX;                  // +0x238 x / z
    float sunSlopeY;                // +0x23C z / y
    float sunRunY;                  // +0x240 y / z
    int sunStepX;                   // +0x244 FISTP(95 * x)
    int sunStepY;                   // +0x248
    terrain::SectorData* lightingSector;   // +0x24C the vertex being lit, for SunReaches
    int lightingX;                  // +0x250
    int lightingY;                  // +0x254
    CLight* sun;                    // +0x258
    using LightTable = stlp::hash_map<int, LandscapeLight, stlp::IntHash>;
    LightTable lights;              // +0x25C 00571050: the registered point lights by id
    int maxLightId;                 // +0x270 the highest id registered
    Vector3 waveDirection;          // +0x274 unit vector (006C7D70); x, y: the waves' travel
    float waveHeight;               // +0x280
    float waveGlow;                 // +0x284 scales the crests' specular colour
    float wavePhase;                // +0x288 advanced by waveSpeed every frame
    float waveSpeed;                // +0x28C
    lib::AlignedArray<float> wavePhases;   // +0x290 00570DE0: per water vertex (33 x 33) phase offsets (006C57E0)
    float sine[512];                // +0x2A0 sin(i * pi / 256)
    int minDetail;                  // +0xAA0 the finest level of detail ChooseDetail gives; 0 after Reset
    int visibleRowFirst;            // +0xAA4 SetVisibleSectors
    lib::AlignedArray<int> visibleColumnFirst;   // +0xAA8 0056FD30, per visible row
    lib::AlignedArray<int> visibleColumnLast;    // +0xAB8 0056FD30
    int ambientSounds[3];           // +0xAC8 the looping circle sounds (handles, -1 none)
    int ambientClasses[3];          // +0xAD4 their sound classes, -1 none
    mfc::CString textAE0;           // +0xAE0 the ambient sound set (ambient\<set>\...)
    int extraPages;                 // +0xAE4 texture pages loaded after the map's own (006C5AA0)
    // +0xAE8 (005711C0) per sector: a level per water material.
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

// 006BE3A0. Height at (x, y) of the plane through triangle v0 v1 v2 ((x, y, z) each); the average
// of the three heights when the triangle is degenerate. A free __fastcall function.
float __fastcall PlaneHeight(const float* v0, const float* v1, float x, float y, const float* v2);

static_assert(sizeof(void*) == 4, "game.exe is 32-bit");
