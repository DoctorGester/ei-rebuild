#pragma once

#include "core_transform.h"
#include "resource.h"

#include "lib/containers.h"
#include "lib/bit_flags.h"
#include "lib/hash_map.h"
#include "lib/vecmath.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <string>
#include <vector>

class CLight;
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

// A point light registered with the landscape (held).
struct LandscapeLight {
    CLight* light;
    LightArea area;
};

// Terrain::landscapeFlags.
enum class LandscapeFlag : std::uint32_t {
    None = 0,
    Waves = 0x01,
    AnimatedTiles = 0x02,
    EdgeFade = 0x04,  // the light fades out at the map's edges
    Ambience = 0x08,  // the ambient sounds play
    SunShadows = 0x10,
    All = 0xFFFF,     // a loaded map's
};
template <>
inline constexpr bool lib::kBitFlags<LandscapeFlag> = true;

// What of a sector is built (SectorData::built; the stages Terrain::SectorAt asks for).
enum class SectorStage : std::uint32_t {
    None = 0,
    Loaded = 1,  // the vertex positions
    Built = 2,   // the geometry
    Lit = 4,
};
template <>
inline constexpr bool lib::kBitFlags<SectorStage> = true;

// Landscape: answers height queries for a world position. The vertex grid has unit spacing, but vertices
// can be displaced sideways, so a point may lie in a neighbouring cell. The client world's Setup makes it.
class Terrain final : public CTransformObject {
public:
    Terrain();
    ~Terrain() override;

    void Create() override;
    void Destroy() override;
    const char* ClassName() override;   // "CTerrain"
    ClassId GetClassId() override;
    // No map, texts "none", frame 1, waves upwards at strength 0, phase 2 pi.
    void Reset();
    // Per water vertex of a sector, the phase offsets of 40 random ripples; each edge matches the opposite one,
    // so neighbouring sectors join.
    void BuildWavePhases();
    // Clamped to 0..1: glow, height and speed scale with it, and the waves' step along the direction.
    void SetWaveStrength(float strength);
    void SetWaves(const Vector3* direction, float strength);
    // `count` pages <map>NNN, then up to two extra pages <map>L00 / L01 while their .mmp exists. 0 when a
    // texture does not load, else 1.
    int SetupPages(int count);
    // The ambient sound set is "Dungeon" when `dungeon`, else "Ingos" / "Suslanger" by `zone`, else "Gipat".
    // 0 when the archive does not open or a texture does not load; a missing or malformed map file is
    // FatalError's result after ClearMap.
    int LoadMap(std::string_view path, std::string_view zone, bool dungeon);
    // Per vertex of a sector (33 x 33): two tiles across a tile edge, four at a corner, one inside.
    void BuildVertexTiles();
    // No map: sizes -1, the ambient loops stopped.
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
    LightArea LightAreaOf(CLight* light, const Vector3* delta) const;
    // The area's lit sectors (their point lights only).
    void RelightArea(const LightArea* area);

    // Negative: no countdown, otherwise the countdown is raised to the interval.
    void SetLightInterval(int frames);
    // The change is added to the material and to every sector's level table. Nothing for a material the map
    // does not have.
    void SetWaterLevel(int material, float level);

    // With LandscapeFlag::EdgeFade: 255, less 255 * depth / fade for a point `depth` inside the fade band
    // (MapEdgeFade, plus one cell) of the map's borders, corners by the distance.
    std::uint8_t EdgeFade(const Vector3* point);
    // Whether the sun reaches ground vertex (column, row) of `sector`: the ray is walked over the vertex grid,
    // loading the sectors it crosses; false when a vertex is above the ray, true at the ray's end or the map's
    // edge. A ray that does not rise: whether the sun points down.
    bool SunReaches(const terrain::SectorData& sector, int column, int row);

    // 9 u, then 9 v.
    const float* TileUv(int rotation, int index, int page);
    // Texture class (sound / prints material): the water tile's when the point is under water, 8 outside the
    // map.
    int SurfaceClass(float x, float y);

    // The ground pages, then (after the objects) the water pages.
    void DrawGround(CSceneRenderer* renderer);
    void DrawWater(CSceneRenderer* renderer);
    // The visible sectors not culled, per page; the page's texture is page % LayerPages(). Nothing without a
    // map.
    void DrawPages(CSceneRenderer* renderer, int firstPage, int lastPage);

    // Height of `layer` (0 = ground, 1 = water surface) at (x, y).
    float HeightAt(float x, float y, int layer);

    // Null outside the map or where the sector has no such layer.
    const Vector3* VertexAt(int ix, int iy, int layer);

    // Sector (sx, sy) when it has any of the `wanted` stages (SectorStage) built, else
    // null. Sectors live in an LRU cache: a hit moves to the front (stamped with the frame); with `create`
    // a missing sector is loaded (the least recently used one reused when older than 240 frames, older ones
    // freed) and the wanted stages are built.
    terrain::SectorData* SectorAt(int sx, int sy, SectorStage wanted, bool create);
    // The cached sector (sx, sy) of the grid, without a lookup's side effects; null when not cached.
    terrain::SectorData* CachedSector(int sx, int sy) const { return sectorGrid[sx * sectorsY + sy]; }

    // The ground height at (x, y), raised to the water surface where it is under water.
    float SurfaceHeight(float x, float y);

    // A square of sectors: each bound is (coordinate -/+ radius - 0.5) rounded to nearest / 32, clamped to the map.
    void SetVisibleSectors(float x, float y, float radius);

    // The light and relight countdowns, the ambient sounds, the waves, then the animation of each drawn
    // visible sector. Returns true.
    bool RunTick() override;

    // The shadow test's ray steps from the direction towards the sun (components below 0.01 pushed to 0.01);
    // with a light interval the lit sectors get relight countdowns spread over it (sorted; 0 drops the
    // lighting at once), without one every sector's lighting is dropped.
    void SetSun(CLight* sun, const Vector4* ambient);
    // With LandscapeFlag::Ambience: the 3x3 sectors around the listener vote a sound class, weighted by
    // distance; up to three classes play a looping circle.wav and random one-shots
    // (ambient\<set>\<class>\<time of day>\<n>.wav).
    void PlayAmbience();
    // Clears LandscapeFlag::Ambience and stops the loops.
    void StopAmbience();
    void StopAmbientLoops();
    // Slides the decal quad along the builder's slope until its lowest point meets the lowest ground vertex
    // of the cells under it (one cell of margin), then hands those cells' ground triangles to the builder.
    void AddDecalTriangles(DecalBuilder* builder, float* const* corners);

    // The texture pages of one tile layer.
    int LayerPages() const;

    // The visible sectors (SetVisibleSectors): columns firstX..lastX of rows firstY..lastY.
    struct SectorRect {
        int firstX = 0, firstY = 0, lastX = -1, lastY = -1;
    };
    // A looping ambient sound and its sound class (-1 none).
    struct AmbientLoop {
        int sound = -1;
        int soundClass = -1;
    };
    static constexpr int kAmbientLoops = 3;

    LandscapeFlag landscapeFlags;
    std::vector<CTexture*> pageTextures;     // held
    std::string mapName;
    bool mapLoaded;                 // nothing is drawn or lit while false
    int sectorsX;                   // map size in sectors
    int sectorsY;
    float width;                    // the map's size in cells
    float height;
    float heightRange;              // the highest terrain height (cursor picking starts at half of it)
    int pages;
    int tilesPerRow;
    int perPage;                    // tilesPerRow squared
    std::vector<terrain::TerrainMaterial> materials;   // the water materials
    // Per tile texture: its class 0..15 (sounds, prints), with sixteen entries of class 15 after the map's
    // own; its frame count when animated, else 0.
    std::vector<int> textureClass;
    std::vector<int> textureFrames;
    CMMIOResource archive;          // the map's sector archive
    std::vector<float> uvTables[4];       // per rotation, 18 floats per texture (TileUv)
    std::vector<float> uvTablesLast[4];   // the same for the extra pages
    std::vector<terrain::VertexTiles> vertexTiles;   // 33 x 33
    std::vector<terrain::SectorData*> sectorGrid;      // [sx * sectorsY + sy]: the cached sector, or null
    std::list<terrain::SectorData*> sectorCache;        // most recently used first
    int frame;
    int lightInterval;              // frames between sun updates
    int lightCountdown;
    Vector4 ambient;
    float sunSlopeX;                // z / x of the unit direction towards the sun
    float sunRunX;                  // x / z
    float sunSlopeY;                // z / y
    float sunRunY;                  // y / z
    int sunStepX;                   // 95 * x rounded to nearest
    int sunStepY;
    CLight* sun;
    // Walked in STLport's order (the lights are added up in it).
    using LightTable = lib::HashMap<int, LandscapeLight>;
    LightTable lights;              // by id
    Vector3 waveDirection;          // unit vector; x, y: the waves' travel
    float waveHeight;
    float waveGlow;                 // scales the crests' specular colour
    float wavePhase;                // advanced by waveSpeed every frame
    float waveSpeed;
    std::vector<float> wavePhases;   // per water vertex (33 x 33) phase offsets
    float sine[512];                // sin(i * pi / 256)
    SectorRect visible;
    AmbientLoop ambientLoops[kAmbientLoops];
    std::string ambientSet;           // the ambient sound set (ambient\<set>\...)
    std::vector<std::vector<float>> waterLevels;   // per sector, per material
};
