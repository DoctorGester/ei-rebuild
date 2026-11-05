#pragma once

// The renderer's harness API: the switches the harness's steps turn (tests/verify/image/harness_render_image.cpp), the
// state its checks read back and the counts and times of the frames (renderstats). The same API in every build; the
// product never calls the switches, and its FrameStats has no times. See docs/notes/renderer.md.

#include "lib/enum_array.h"
#include "lib/vecmath.h"

#include <cstddef>
#include <cstdint>

class Object3D;

namespace render {

// ---- The render quality ---------------------------------------------------------------------------------------

// The render quality's state in the last presented frame: the levels asked for and what was drawn.
struct SmoothInfo {
    int msaa = 1, anisotropy = 1;
    int deviceSamples = 1, targetSamples = 1;
    int multisampledPasses = 0;
    int copiesInto = 0, depthCopiesBack = 0;
    int depthCopyPasses = 0;   // the multisampled passes that wrote the depth copy (none in a frame without a draw taking it)
    int anisotropicModels = 0, plainModels = 0;
};
SmoothInfo SmoothState();
// "smoothorder": the smooth passes in an order the game never draws. Before: an empty pass on the frame target first.
// Split: the world view's first run ends at its middle command and an empty pass on the frame target follows.
enum class SmoothOrder : std::uint8_t { Game, Before, Split };
void SetSmoothOrder(SmoothOrder order);

// ---- Weather, shadows and occlusion ---------------------------------------------------------------------------

// A unit's own weather (11b, Units) as the last frame left it: `known` when the unit (the object its meshes were
// handed over with, BeginCaster) was drawn lately; its wetness and snow (0 .. 1) and the share of its top covered by a
// roof (0 .. 1), and where it stood (the middle of its box's foot, the landscape's space).
struct UnitWeather {
    bool known = false;
    float wet = 0.0f, snow = 0.0f, shelter = 0.0f;
    Vector3 at{};
};
UnitWeather UnitWeatherState(const Object3D* object);
// "shadowcasters view|all|walked": View, only what the game draws casts; All, casters outside the view too (the
// default); Walked, every caster comes from the walk.
enum class OffscreenCasters { View, All, Walked };
void SetOffscreenCasters(OffscreenCasters mode);
// "shadowview on|off": the receivers show only how much sun the map and the clouds hide. "shadowshift <texels>": the
// map's origin moved by whole texels (half as many along), to show a filter that depends on the range cells.
void SetShadowView(bool on);
void SetShadowShift(int texels);
// "decals on|off": the ground prints (MeshDraw::groundDecal) drawn (the default) or left out.
void SetGroundDecals(bool on);
// "weatherparticles on|off": the rain and snow particles (RainSnow::DrawParticles) drawn (the default) or left out, to
// measure what share of a frame they cover (render.h's WeatherParticlesDrawn).
void SetWeatherParticles(bool on);
// "panelweather on|off": the interface figure of a world unit (BeginInterfaceUnit) with its unit's weather (the
// default) or dry, for a frozen capture of the HUD unit panel both ways.
void SetInterfaceUnitWeather(bool on);
// "roofs on|off": the ground and the world's meshes under a roof kept dry (the roof map, the default) or weathered as
// in the open, for a frozen capture both ways.
void SetRoofShelter(bool on);
// The roof map of the last prepared frame (render_weather.cpp): made (only while there is weather on the surfaces),
// its texels a side, the units it spans and the roof meshes drawn into it.
struct RoofInfo {
    bool made = false;
    int size = 0;
    float extent = 0.0f;
    int roofs = 0;
};
RoofInfo RoofState();
// "occlusion on|off|view": the ambient occlusion on (the default), off, or shown alone by its receivers (white none,
// black all). OcclusionState: the last presented frame's, with the ground's sectors whose occlusion is kept and how
// often one was worked out since the start.
enum class OcclusionMode { On, Off, View };
void SetOcclusion(OcclusionMode mode);
struct OcclusionInfo {
    bool made = false;
    int width = 0, height = 0;   // the half-size depth's
    int meshDraws = 0, terrainDraws = 0, receivingMeshes = 0, receivingTerrain = 0;
    int groundSectors = 0, groundMade = 0;
};
OcclusionInfo OcclusionState();
// The last presented frame's shadow map. Of the objects whose shadow the game draws: `offScreen` cast
// nothing and handed over no mesh, `lost` handed over meshes of which none went into the map (none should);
// `outside*`: casters through ShadowCasterMesh.
struct ShadowInfo {
    bool made = false;
    int size = 0;
    float extent = 0.0f;
    int casters = 0, casterDraws = 0, receivingMeshes = 0, receivingTerrain = 0;
    int offScreen = 0, lost = 0;
    int outsideCasters = 0, outsideDraws = 0, outsideCorners = 0;
    int terrainCasters = 0;   // the ground's sectors drawn into the map
    int groundDecals = 0, receivingDecals = 0;   // the ground prints handed over, and those sampling the map
    // The cloud shadows: how much of the sun a full cloud hid (0: none) and the share of the ground under clouds.
    float cloudLight = 0.0f, cloudCover = 0.0f;
    // The weather on the surfaces (0 .. 1): how wet the ground is, the puddles' share and the snow's cover.
    float wetness = 0.0f, puddles = 0.0f, snowCover = 0.0f;
};
ShadowInfo ShadowState();
// "groundreach on|off": the last presented frame's ground as the camera's clip space keeps it (render_shadows.cpp's
// terrain pieces' runs through the clip planes, measured only while on): the farthest point of it from the camera
// (world units), the runs in the view's sides and of them those the far side cuts or leaves out (a hard edge where
// the terrain ends; none since the view has no far plane).
struct GroundReachInfo {
    bool measured = false;
    float reach = 0.0f;
    int runs = 0, cut = 0;
};
void SetGroundReach(bool on);
GroundReachInfo GroundReachState();

// ---- What the frames cost ---------------------------------------------------------------------------------------

// What the renderer did for the presented frames (renderer.md section 13), summed over `frames`.
// TakeFrameStats returns the sums since its last call (the harness's "renderstats"). The CPU times and the
// reuse counts are the harness build's only.
struct FrameStats {
    enum class Count : int {
        // Hand-over: Meshes are BeginMesh with corners built; StateReads are the passes' reads of the render state
        // (ReadPassState).
        TerrainPages, TerrainBatches, Meshes, MeshPasses, TransformedDraws, Blits, DepthClears, StateReads, Matrices,
        // Of the meshes: the flat geometry (object shadows, ground prints, selection markers, light decals).
        FlatMeshes, FlatVertexBytes,
        // Meshes whose vertices equal another mesh's of the same frame (instancing candidates).
        SharedMeshes,
        TerrainVertexBytes, TerrainIndexBytes, MeshVertexBytes, MeshIndexBytes, UniformBytes,
        Passes, Draws, PipelineApplies, PipelineChanges, Bindings, TextureChanges,
        // Ground prints projected (ProjectDecal) and their vertices; the device's flat draws and their vertices;
        // RenderShadow's builds.
        DecalPrints, DecalVertices, FlatDraws, FlatDrawVertices, ShadowBuilds,
        // The shadow map's draws.
        ShadowDraws,
        // Meshes handed over with per-vertex offsets (an animation's morph) and their corners.
        MorphMeshes, MorphCorners,
        Kinds
    };
    // CPU times (ms). Each leaves out the times measured inside it: StatsMs (these counts' own hashing and checks),
    // TerrainMs (the terrain pages' hand-over and uploads), ShadowMs (the shadow map's work) and the rest.
    // DeviceMeshMs: the game device's mesh draws (device_mesh.cpp's DrawMesh); DecalPrintMs: ProjectDecal for ground
    // prints; FlatMs: the device's flat draws; ShadowBuildMs: RenderShadow.
    enum class Time : int {
        TerrainMs, HandOverMs, TextureMs, EndFrameMs, StatsMs, DeviceMeshMs, DecalPrintMs, FlatMs, ShadowBuildMs, ShadowMs,
        Kinds
    };
    // Images made (or updated in place) per kind.
    enum class Upload : int { Mmp, Movie, Small, Kinds };
    // Vertices against the last frame's of the same terrain page or mesh: all the same, only colours changed,
    // positions or texture coordinates changed, none last frame.
    enum class Reuse : int { Same, Relit, Moved, New, Kinds };

    std::uint32_t frames = 0;
    int width = 0, height = 0;   // the last frame's size
    lib::EnumArray<std::uint64_t, Count, lib::Index(Count::Kinds)> counts;
    lib::EnumArray<double, Time, lib::Index(Time::Kinds)> ms;
    lib::EnumArray<std::uint64_t, Upload, lib::Index(Upload::Kinds)> uploads, uploadBytes;
    lib::EnumArray<std::uint64_t, Reuse, lib::Index(Reuse::Kinds)> terrainReuse, meshReuse;
};
FrameStats TakeFrameStats();
// The game's own work timed into FrameStats (DeviceMeshMs, DecalPrintMs, FlatMs, ShadowBuildMs) with its counts.
void BeginDeviceMeshTime();
void EndDeviceMeshTime();
void BeginDecalTime();
void EndDecalTime(int vertices);
void BeginFlatDrawTime(int vertices);
void EndFlatDrawTime();
void BeginShadowBuildTime();
void EndShadowBuildTime();

// ---- Data kept on the GPU ---------------------------------------------------------------------------------------

// "terraincache": pages kept on the GPU (on, the default) or built every frame (off, for frame comparisons). Every
// 16th frame the kept pages are checked against a fresh build; `stale` counts a change TerrainChanged was not told of.
void SetTerrainCache(bool on);
struct TerrainCacheInfo {
    bool on = true;
    std::size_t pages = 0;
    std::uint64_t checks = 0, stale = 0;
};
TerrainCacheInfo TerrainCacheState();
// "meshcache": as SetTerrainCache, for figure meshes.
void SetMeshCache(bool on);
struct MeshCacheInfo {
    bool on = true;
    std::size_t meshes = 0;
    std::uint64_t checks = 0, stale = 0;
};
MeshCacheInfo MeshCacheState();
// "terrainlevels": what the ground pages draw. Levels: the texture level the page's sampler reads at each pixel (red =
// level * 32, mips blended as the sampler blends them; green 0x5A and blue 0xA5 mark the pixel). PointMips: the same
// through the old sampler (bilinear, point mips: the level steps along the tiles' edges that showed as seams). Off:
// the pages' colours.
enum class TerrainLevelMode { Off, Levels, PointMips };
void TerrainLevels(TerrainLevelMode mode);
// "mmplevels": how far the held .mmp textures' levels lie from their level 0, both box-filtered to 4 x 4: the mean
// absolute difference per channel (0..255), the largest over all levels. Levels decoded from the wrong bytes (bright
// green strips over the trees) lie far off.
struct MmpLevelCheck {
    int textures = 0, levels = 0;
    double worst = 0.0;
    int worstWidth = 0, worstHeight = 0, worstLevel = 0;   // that texture's first level's size, the level
    std::uint32_t worstFormat = 0;                         // its .mmp format id
};
MmpLevelCheck CheckMmpLevels();

// ---- The window --------------------------------------------------------------------------------------------------

// The presents by what they did with the cursor, and the shape shown (its size and hot spot in window pixels).
struct CursorInfo {
    std::uint32_t presentsShown = 0, presentsHeld = 0, presentsHidden = 0;
    std::uint32_t made = 0, failures = 0;   // shapes made, shapes the system could not make
    std::uint32_t shapeChanges = 0;         // another shape (or none) shown ...
    std::uint32_t frameChanges = 0;         // ... of them, another frame of the same picture
    bool shown = false;
    int width = 0, height = 0, hotX = 0, hotY = 0;
};
CursorInfo CursorState();
// Frames presented on the game window; the verify build's checks of the presented frame against the gamma ramp (after
// each SetGammaRamp, at a grid of points, when the frame shows one to one).
struct WindowPresentInfo {
    std::uint32_t presents = 0;
    int rampChecks = 0, rampPoints = 0, rampDiffering = 0;
    bool rampCheckPending = false;
    bool rampIdentity = true;
};
WindowPresentInfo WindowPresents();

} // namespace render
