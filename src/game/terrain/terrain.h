#pragma once

#include "engine/core/core_transform.h"
#include "engine/core/ref.h"
#include "engine/data/resource.h"

#include "lib/containers.h"
#include "lib/bit_flags.h"
#include "lib/hash_map.h"
#include "lib/vecmath.h"
#include "render/mesh_records.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

class Light;
class SceneRenderer;
class Texture;
class DecalBuilder;
using VertexPacket = render::VertexPacket;

namespace terrain {

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

// What of a sector is built (SectorData::built; the stages Terrain::SectorAt asks for).
enum class SectorStage : std::uint32_t {
    None = 0,
    Loaded = 1,  // the vertex positions
    Built = 2,   // the geometry and the edge fade
};

} // namespace terrain

template <>
inline constexpr bool lib::kBitFlags<terrain::LandscapeFlag> = true;
template <>
inline constexpr bool lib::kBitFlags<terrain::SectorStage> = true;

namespace terrain {

struct MapHeader;
struct SectorData;
struct TerrainMaterial;
struct TileUv;
struct VertexTiles;

// The two vertex layers of a sector: the ground and the water surface.
enum class Layer { Ground, Water };

// Landscape: answers height queries for a world position. The vertex grid has unit spacing, but vertices
// can be displaced sideways, so a point may lie in a neighbouring cell. The client world's Setup makes it.
class Terrain final : public TransformObject {
public:
    Terrain();
    ~Terrain() override;

    void Create() override;
    void Destroy() override;
    const char* ClassName() override;   // "CTerrain"
    ClassId GetClassId() override;
    // Clamped to 0..1: glow, height and speed scale with it, and the waves' step along the direction.
    void SetWaveStrength(float strength);
    void SetWaves(const Vector3& direction, float strength);
    // The ambient sound set is "Dungeon" when `dungeon`, else "Ingos" / "Suslanger" by `zone`, else "Gipat".
    // False when the archive does not open or a page texture does not load; a missing or malformed map file ends the
    // game.
    bool LoadMap(std::string_view path, std::string_view zone, bool dungeon);
    // No map: sizes -1, the ambient loops stopped.
    void ClearMap();

    // The shown point lights (`lights`, the GPU lighting's). Registers the light by id (held); nothing happens
    // before a map is loaded.
    void AddLight(Light* light);
    // Takes the light off the table, which drops the reference it held (none for a light not registered), with or
    // without a map.
    void RemoveLight(Light* light);

    // The change is added to the material and to every sector's level table. Nothing for a material the map
    // does not have.
    void SetWaterLevel(int material, float level);

    // With LandscapeFlag::EdgeFade: 255, less 255 * depth / fade for a point `depth` inside the fade band
    // (MapEdgeFade, plus one cell) of the map's borders, corners by the distance.
    std::uint8_t EdgeFade(const Vector3& point);

    // Texture class (sound / prints material): the water tile's when the point is under water, 8 outside the
    // map.
    int SurfaceClass(float x, float y);

    // The ground pages, then (after the objects) the water pages.
    void DrawGround(SceneRenderer* renderer);
    void DrawWater(SceneRenderer* renderer);

    // The water layer's height is the waves' (WaterVertex).
    float HeightAt(float x, float y, Layer layer);
    // The ground height at (x, y), raised to the water surface (with its waves) where it is under water.
    float SurfaceHeight(float x, float y);
    // The same with the water at rest, without its waves (the camera's height: its bobbing was not intended).
    float CalmSurfaceHeight(float x, float y);

    // The waves of water material `material` at the last RunTick: the phase it adds along the wave direction, the
    // lift (waveHeight times the material's priority), the crest's specular colour per unit of wave (0 .. 255), and
    // whether its vertices keep the ground's x, y (only their crest moves).
    struct MaterialWaves {
        float turn = 0.0f;
        float lift = 0.0f;
        Vector3 crest;
        bool keepsGroundXY = false;
    };
    MaterialWaves WavesOf(int material) const;
    // The materials that wave: the first kWaveMaterials of the map's (no map has more than 7; terrain_water.hlsl's table).
    static constexpr int kWaveMaterials = 32;
    // Water vertex `index` (0 .. 33 x 33) of a built sector as the waves move it at the last RunTick, from its place
    // at rest: computed where it is asked for, the GPU moves the drawn ones the same way (terrain_water.hlsl's `waved`).
    // With LandscapeFlag::Waves and a material below kWaveMaterials, a sine table entry at the phase along the wave
    // direction (from the sector's first water vertex, mod 2 pi) plus the material's turn and the vertex's offset
    // (wavePhases) lifts it by a quarter of the material's lift and circles it sideways by three times the lift with
    // the time (the entries at its column and row a quarter turn apart); a material that keeps the ground's x, y
    // does not move it. Else, or when the sector is not built, the place at rest.
    Vector3 WaterVertex(const SectorData& sector, int index) const;

    // Every sector of the map is visible (the passes clip-test each one). The game took a square of sectors around
    // the camera, 100 units each way; Rebirth's LandscapeDrawRadius (always on) widened it to the whole map.
    void ShowAllSectors();

    // The ambient sounds, the waves' phase and time, then the tile animation of each drawn visible sector. Returns
    // true.
    bool RunTick() override;

    // The sun and the ambient colour (the GPU lighting's).
    void SetSun(Light* sun, const Vector4& ambient);
    // Clears LandscapeFlag::Ambience and stops the loops.
    void StopAmbience();
    // Slides the decal quad along the builder's slope until its lowest point meets the lowest ground vertex
    // of the cells under it (one cell of margin), then hands those cells' ground triangles to the builder.
    void AddDecalTriangles(DecalBuilder* builder, const VertexPacket& quad);

    // The sectors' part (terrain_sector.h).
    // Sector (sx, sy) when it has any of the `wanted` stages (SectorStage) built, else
    // null. Sectors live in an LRU cache: a hit moves to the front (stamped with the frame); with `create`
    // a missing sector is loaded (the least recently used one reused when older than 240 frames, older ones
    // freed) and the wanted stages are built.
    SectorData* SectorAt(int sx, int sy, SectorStage wanted, bool create);
    // The cached sector (sx, sy) of the grid, without a lookup's side effects; null when not cached.
    SectorData* CachedSector(int sx, int sy) const { return sectorGrid[GridIndex(sx, sy)]; }
    // The texture coordinates of tile texture `index` of a page (the extra pages' table for those).
    const TileUv& TileUvAt(int rotation, int index, int page) const;
    // The texture pages of one tile layer.
    int LayerPages() const;
    // Sector (sx, sy)'s pending water level change per material.
    std::span<float> SectorWaterLevels(int sx, int sy);

    // The visible sectors (ShowAllSectors): columns firstX..lastX of rows firstY..lastY.
    struct SectorRect {
        int firstX = 0, firstY = 0, lastX = -1, lastY = -1;
    };
    // A looping ambient sound and its sound class (-1 none).
    struct AmbientLoop {
        int sound = -1;
        int soundClass = -1;
    };
    static constexpr int kAmbientLoops = 3;
    static constexpr int kSoundClasses = 16;

    LandscapeFlag landscapeFlags = LandscapeFlag::None;
    std::vector<HeldRef<Texture>> pageTextures;
    std::string mapName = "none";
    bool mapLoaded = false;         // nothing is drawn or lit while false
    int sectorsX = -1;              // map size in sectors; -1 without a map
    int sectorsY = -1;
    float width = 0.0f;             // the map's size in cells
    float height = 0.0f;
    float heightRange = 0.0f;       // the highest terrain height (cursor picking starts at half of it)
    int pages = 0;
    int tilesPerRow = 0;
    int perPage = -1;               // tilesPerRow squared
    std::vector<TerrainMaterial> materials;   // the water materials
    // Per tile texture: its class 0..15 (sounds, prints), with sixteen entries of class 15 after the map's
    // own; its frame count when animated, else 0.
    std::vector<int> textureClass;
    std::vector<int> textureFrames;
    MMIOResource archive;          // the map's sector archive
    std::vector<TileUv> uvTables[4];       // per rotation, per texture of a page
    std::vector<TileUv> uvTablesLast[4];   // the same for the extra pages
    // The animated tiles carry their coordinates within their page's first tile
    // (per rotation: uvTables' without the tile's column and row), `tileSide` the tile's side as a part of the page.
    std::vector<TileUv> uvLocal;           // per rotation
    float tileSide = 0.0f;
    int tileSteps = 0;   // the animated tiles' steps made (RunTick), the GPU's clock for them
    std::vector<VertexTiles> vertexTiles;   // 33 x 33
    std::vector<SectorData*> sectorGrid;      // [GridIndex(sx, sy)]: the cached sector, or null
    std::vector<std::unique_ptr<SectorData>> sectorCache;   // most recently used first
    int frame = 1;
    Vector4 ambient;
    Light* sun = nullptr;
    lib::HashMap<int, HeldRef<Light>> lights;   // by id
    Vector3 waveDirection = {0.0f, 0.0f, 1.0f};   // unit vector; x, y: the waves' travel
    float waveHeight = 0.0f;
    float waveGlow = 0.0f;          // scales the crests' specular colour
    float wavePhase = kPi + kPi;    // advanced by waveSpeed every frame
    float waveTime = 0.0f;          // the circling's time at the last RunTick: the client world's ticks * 0.05
    float waveSpeed = -0.0f;        // as SetWaveStrength(0) leaves it (a negative scale times 0)
    std::vector<float> wavePhases;   // per water vertex (33 x 33) phase offsets
    float sine[512] = {};           // sin(i * pi / 256)
    SectorRect visible;
    AmbientLoop ambientLoops[kAmbientLoops];
    std::string ambientSet = "none";   // the ambient sound set (ambient\<set>\...)
    // Per sound class, how many one-shots ambient\<set>\<class>\day\1.wav, 2.wav, ... the map's set has.
    int ambientVariants[kSoundClasses] = {};
    std::vector<float> waterLevels;   // per sector (sy * sectorsX + sx), per material

private:
    // The start values of what ClearMap leaves: no flags, texts "none", frame 1, no sun, waves upwards at
    // strength 0, phase 2 pi, nothing visible, no ambient loops.
    void Reset();
    // The index of sector (sx, sy) in sectorGrid.
    std::size_t GridIndex(int sx, int sy) const { return static_cast<std::size_t>(sx * sectorsY + sy); }
    // Per water vertex of a sector, the phase offsets of 40 random ripples; each edge matches the opposite one,
    // so neighbouring sectors join.
    void BuildWavePhases();
    // Per vertex of a sector (33 x 33): two tiles across a tile edge, four at a corner, one inside.
    void BuildVertexTiles();
    // The sizes, materials, tile texture classes and animated textures of a map file whose header is checked.
    void ReadMap(const MapHeader& header, const std::uint8_t* file);
    // `count` pages <map>NNN, then up to two extra pages <map>L00 / L01 while their .mmp exists. False when a
    // texture does not load.
    bool SetupPages(int count);
    // The tile texture coordinates of every rotation, for tiles `tileSize` pixels wide.
    void BuildUvTables(int tileSize);
    // For every sound class, the one-shots of the ambient set (ambientVariants), counted until the first one
    // missing.
    void CountAmbientVariants();
    // With LandscapeFlag::Ambience: the 3x3 sectors around the listener vote a sound class, weighted by
    // distance; up to three classes play a looping circle.wav and random one-shots
    // (ambient\<set>\<class>\<time of day>\<n>.wav).
    void PlayAmbience();
    void StopAmbientLoops();
    // A pass: the landscape's matrix (rotation, the position in the last column) on the clip and projection
    // stacks around DrawPages.
    void DrawPass(SceneRenderer* renderer, int firstPage, int lastPage);
    // The visible sectors clip tested, and the pass (pages `firstPage` .. `lastPage` with a texture: the page's
    // texture is page % LayerPages()) handed to the renderer. Nothing without a map.
    void DrawPages(SceneRenderer* renderer, int firstPage, int lastPage);
    // The sector holding map vertex (ix, iy) (loaded) and the vertex's index in a layer; null outside the map.
    SectorData* VertexSector(int ix, int iy, int* index);
    // Null outside the map or where the sector has no such layer; the water at rest.
    const Vector3* VertexAt(int ix, int iy, Layer layer);
};

} // namespace terrain
