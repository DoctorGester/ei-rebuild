#pragma once

// The renderer: draws the game's frame on SDL3's GPU API (render/gpu.h: Direct3D 12 or Vulkan) from the data the game
// hands over and presents it on the game window. No fallback of any kind: when it cannot start, or cannot go on (no
// device, a pass or a submission refused, nothing to present into), the game ends with the reason in the game's
// error box (renderer.md, "When the renderer fails"). See docs/notes/renderer.md.
// EI_RENDER (or the harness step "render <spec>") is a comma-separated list of the harness build's words: "checkall"
// (the harness's own "removable" never reaches it). The harness's switches and read-backs: render_checks.h.

#include "lib/rect.h"
#include "lib/vecmath.h"
#include "render/kept_weather.h"
#include "render/mesh_records.h"
#include "render/render_state.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

class FigureMesh;
class LinkMesh;
class Object3D;
struct MmpFile;
struct Surface;
class SceneRenderer;
class TextureDevice;

namespace terrain {
struct SectorData;
class Terrain;
}

namespace render {

// Sets the options from `spec` (EI_RENDER's format) and EI_RENDER; without a call the environment is read
// at the first use.
void Configure(const char* spec);
// Starts making the device on its own thread at image start-up (before WinMain), and on that thread the pipelines the
// game always draws with (for the render quality's `msaa`, SetMsaa's argument); the first use takes both (saves
// 0.15 s of each start, and the pipelines' creation at their first draws).
void PrepareDevice(int msaa);

// The render quality (renderer.md section 11, "Render quality"), each from the next frame: the world view
// multisampled with `samples` (1: off; 2, 4 or 8, fewer when the device draws fewer) and its models sampled
// anisotropically at `level` (1: off, as their stage says; up to 16). Both off unless set: the product sets
// them from the settings file (device_mesh.cpp), a harness run by its steps "msaa" and "anisotropy".
void SetMsaa(int samples);
int Msaa();
void SetAnisotropy(int level);
int Anisotropy();
// False in the harness build: its pinned settings file lacks the values (a read of a missing value fails a run).
bool QualitySettingsSupported();
// The renderer stops: the performance overlay's frame timing thread ended. The device, its textures and its window
// stay until the process ends (nothing the next start needs is in them).
void Shutdown();
// The world view's draws (WorldClient::Render) come between BeginWorld and EndWorld.
void BeginWorld();
void EndWorld();

// The performance overlay (render_perf.h). Switched by the settings file; the harness build has none.
bool PerfOverlaySupported();
void SetPerfOverlay(bool on);
bool PerfOverlay();
// The sun for this frame's world view: the direction its light travels (z negative by day). Without it no
// shadow map is made.
void ShadowSun(const Vector3& direction);
// The cloud shadows and the weather on the surfaces (renderer.md section 13, items 11 and 11b): the client's weather
// for this frame's world view, handed over with its light. `day` how much the clouds can hide of the sun (1 by day, 0
// at night and in a cave), `rain` the rain's or snow's strength (0 .. 1, as its layer fades it), `snow` whether it is
// snow, `settled` the weather the layer is heading to (the last SetWeather's: 0 clear, 1 rain, 2 snow), `windX` /
// `windY` the wind (landscape units a tick), `time` the world's time in ticks (the tick and its fraction). Without it
// a frame has no cloud shadows and its surfaces are dry.
struct CloudWeather {
    float day = 0.0f, rain = 0.0f;
    bool snow = false;
    int settled = 0;
    float windX = 0.0f, windY = 0.0f;
    double time = 0.0;
};
void CloudShadows(const CloudWeather& weather);
// A zone's landscape was loaded (terrain::Terrain::LoadMap; a save's load too): the weather on the surfaces starts again
// from the settled weather of the next hand-overs (unless RestoreWeather follows), so the ground of a zone does not stay
// wet or snowy from the one before (the world's time goes on) and a zone entered in snow is covered at once.
void LandscapeLoaded();
// The weather a save's side file keeps (kept_weather.h): WeatherToKeep takes it; RestoreWeather, after the loaded
// zone's LandscapeLoaded, starts the surfaces and the clouds from it instead of from the settled weather.
KeptWeather WeatherToKeep();
void RestoreWeather(const KeptWeather& weather);
// A unit's own wetness and snow: none for a unit not drawn lately. RestoreUnitWeather, after the loaded zone's
// LandscapeLoaded, gives a unit the state it had (taken as it is at its next frame).
std::optional<UnitWeatherLevels> UnitWeatherToKeep(const Object3D* object);
void RestoreUnitWeather(const Object3D* object, const UnitWeatherLevels& levels);
// The GPU lighting (renderer.md section 13). The world's draw hands over the frame's light, kept
// until the next hand-over: the sun, the ambient colour, the point lights the landscape lights its ground
// with (the nearest kMostSceneLights), the camera and the client's time (the water's ripples move by it). The terrain
// shader lights each pixel and SectorData::Light writes only the edge fade. The daylight colours: the sun and ambient
// colour the ground prints' own colours are for (WorldClient::DaylightColours); a print is lit by the ground's light
// over the ground's light in them (mesh.hlsl's ground_shade).
struct ScenePointLight {
    Vector3 position;   // the landscape's space
    float inverseRangeSquared = 0.0f;
    float colour[3] = {};
    bool specular = false;  // light flag 0x40
    bool shadows = false;  // casts point shadows: not an object's own light (LightFlag::Off)
};
struct SceneLights {
    Vector3 sunDirection;
    float sunColour[3] = {};
    float ambientColour[3] = {};
    float daySunColour[3] = {};
    float dayAmbientColour[3] = {};
    Vector3 camera;
    const ScenePointLight* points = nullptr;
    int pointCount = 0;
    double clientTicks = 0.0;  // the client world's tick and the fraction of the next one it has had
};
constexpr int kMostSceneLights = 16;
void SceneLighting(const SceneLights& lights);
// Object shadows (renderer.md section 13): figures and map objects cast from the sun into a shadow map made
// every frame; the game's 64 x 64 shadow decals are neither made nor drawn.
// The ground casts too (landscape flag 0x10): ShadowTerrainArea is ShadowCasterArea for a caster no higher
// than `highestTop` (false: no map this frame); a loaded sector in it casts its ground layer's 33 x 33
// vertices when ShadowCasterReaches its box.
struct ShadowCasterRect;
bool ShadowTerrainArea(float highestTop, ShadowCasterRect* area);
void ShadowTerrainSector(const terrain::SectorData* sector);
// The meshes handed over between BeginCaster and EndCaster belong to `object`; CastsShadow (from its shadow
// draw) lets them cast this frame, at least 0.5 strong. A `figure`'s meshes take only the shadows others
// cast, on every face; other objects' meshes are shadowed on the faces turned to the sun.
void BeginCaster(const Object3D* object, bool figure);
void EndCaster();
// The meshes handed over between BeginInterfaceUnit and EndInterfaceUnit are an interface figure of the world unit
// `object` (the one BeginCaster was given for it; the HUD unit panel's): drawn in the interface, they take that
// unit's own weather (renderer.md 11b, Units), as the unit out in the rain. Figures not drawn so (the camp's and the
// hero screen's previews) stay dry.
void BeginInterfaceUnit(const Object3D* object);
void EndInterfaceUnit();
void CastsShadow(const Object3D* object, float strength);
// Casters the game does not draw this frame (or parts it left out) whose shadow can reach the visible ground
// cast through ShadowCasterMesh, read by the world's draw from the objects' parts. ShadowCasterArea: whether
// this frame gets a map, and the ground rectangle (landscape's space) a caster's centre must stand in: the
// visible ground's box swept back towards the sun as far as a caster no higher than `highestTop` reaches,
// widened by `widest` (the largest radius). ShadowCasterReaches is exact up to the bounds. CasterPartDrawn:
// `part` (MeshSource::owner) was drawn this frame as a caster's mesh. CasterStrength: -1 when CastsShadow was
// not called for `object` this frame (render_checks.h's SetOffscreenCasters: which casters cast).
struct ShadowCasterRect {
    float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
};
bool ShadowCasterArea(float highestTop, float widest, ShadowCasterRect* area);
struct ShadowCasterSphere {
    float x = 0.0f, y = 0.0f, z = 0.0f, radius = 0.0f;
};
bool ShadowCasterReaches(const ShadowCasterSphere& sphere);
bool CasterPartDrawn(const FigureMesh* part);
float CasterStrength(const Object3D* object);
struct ShadowMesh {
    const VertexPacket* positions = nullptr;  // as MeshDraw's
    int positionCount = 0;
    const TexCoord* uvs = nullptr;
    int uvCount = 0;
    const MeshCorner* corners = nullptr;
    int cornerCount = 0;
    const std::uint16_t* indices = nullptr;  // a triangle list over the corners
    int indexCount = 0;
    float uOffset = 0.0f, vOffset = 0.0f;  // added to the texture coordinates
    Matrix4 model;                     // model space to the landscape's, row-major: out = M * (x, y, z, 1)
    const Surface* texture = nullptr;  // stage 0's surface holder, or null
    // The part (MeshSource::owner) and its link figure (MeshSource::shared, else null), and whether the
    // positions are a copy with the per-vertex offsets added (MeshSource::moved): the renderer keeps the
    // corners on the GPU by part, read again when MeshChanged says so.
    const FigureMesh* owner = nullptr;
    const LinkMesh* shared = nullptr;
    bool moved = false;
    bool unit = false;  // a unit's part: no roof over the weather (render_weather.cpp's roof map)
};
void ShadowCasterMesh(const Object3D* object, const ShadowMesh& mesh);
// The walk's time counts as the shadow map's CPU time (FrameStats and the performance overlay).
void BeginShadowWalk();
void EndShadowWalk();
// The rain and snow particles (RainSnow::DrawParticles) drawn: true unless the harness left them out
// (render_checks.h's SetWeatherParticles).
bool WeatherParticlesDrawn();
// The holder is destroyed: what the renderer kept for it forgotten.
void HolderGone(const Surface* holder);
// A holder loaded from an 8888 .mmp picture (the software cursor's images): the renderer keeps the
// picture's pixels from the file data.
void PictureFromMmp(const Surface* holder, const MmpFile& file);
// RGBA8 pixels, rows from the top.
struct Picture {
    int width = 0, height = 0;
    std::vector<std::uint8_t> rgba;
};

// The device object is being built: the renderer starts (it presents through its own swap chain). When it cannot
// start the game ends here.
void StartPresenting();
// Default on; the harness turns it off under its virtual clock.
void SetWaitForVsync(bool on);
// The settings file's "VSync" (after StartPresenting, before the first present): off presents with sync
// interval 0 and, where supported, tearing. The harness build has no such switch.
bool VsyncSwitchSupported();
void SetVsync(bool on);
bool Vsync();

// An old-path mesh draw the game decided is visible, colours set: the mesh in model space and the matrix the
// old path projects it with. Every pass the old path draws between BeginMesh and EndMesh calls MeshPass,
// which takes the old device's state. A mesh the renderer refuses is lost (logged).
struct MeshDraw {
    const VertexPacket* positions = nullptr;  // vertex i is lane i % 4 of packet i / 4 (w unused)
    int positionCount = 0;             // vertices
    const std::uint32_t* colours = nullptr;  // D3DCOLOR, by the corners' normal index
    const std::uint32_t* speculars = nullptr;
    const TexCoord* uvs = nullptr;     // by the corners' texture index
    const MeshCorner* corners = nullptr;  // null (the flat geometry draw): vertex i is position i with uv i and
                                          // colours[0] / speculars[0]
    int cornerCount = 0;               // without corners: the vertex count
    const std::uint16_t* indices = nullptr;  // a triangle list over the corners
    int indexCount = 0;
    float uOffset = 0.0f, vOffset = 0.0f;  // added to the texture coordinates when `moveUv`
    bool moveUv = false;
    const Matrix4* matrix = nullptr;   // row-major, out = M * (x, y, z, 1)
    bool screenMatrix = false;         // `matrix` gives screen pixels after the divide, else clip space
    bool clipped = false;              // else only projected: the depth is not cut at the near plane
    MeshFog fog = MeshFog::None;
    float eyeHeight = 0.0f;            // MeshFog::Horizon's: the eye above the mesh's origin
    float zScale = 0.0f;               // 0 taken as 1
    bool coloursWritten = false;       // the draw set every colour to the device's (flag 4)
    bool groundDecal = false;          // flat geometry laid on the ground (a print: blood, footprints, scorch marks),
                                       // its positions in the landscape's space: shaded by the shadows as the ground
// A figure mesh lit on the GPU (MeshSource::lighting), else null: the normals (packets of four as the
// positions) and the words (a position index in the low half, a normal index in the high half, by the corners'
// colour index).
    const NormalPacket* normals = nullptr;
    int normalCount = 0;               // normals
    const std::uint32_t* words = nullptr;
    int wordCount = 0;
};

// A texture's .mmp file in memory: the levels from `mip` on that the game loads for `holder`, decoded and
// kept until the holder goes. False when not decoded (a format it does not read).
bool TextureFromMmp(const Surface* holder, const MmpFile& file, unsigned mip, unsigned levels);

// From now on mesh draws sampling `target` in their first stage take the rectangle `to` from `source`'s
// rectangle `from` (left, top, right, bottom in first-level texels); the target's texture is not changed.
// One per target; forgotten when either holder goes; nothing recorded for an empty or outside rectangle.
// The unit's face blink.
void TexturePatch(const Surface* target, const int to[4], const Surface* source, const int from[4]);

// How an image's pixels are stored (TextureImage).
enum class ImageLayout : std::uint8_t {
    Argb,     // 0xAARRGGBB with premultiplied alpha
    Pnt3,     // "PNT3": Argb in groups of four pixels, where a group's first pixel has alpha 0 and is not 0, that word
              // instead counts target bytes left as they are
    Rgba,     // 8 bits a channel: R, G, B, A (the save pictures)
};
// An image the game has drawn into a texture on the GPU (ComposeTexture, DrawIntoTexture): an .mmp file's pixels after
// its header and extra block, or another file's picture, rows from the top.
struct TextureImage {
    const void* pixels = nullptr;
    std::uint32_t bytes = 0;  // the pixels' bytes in the file
    int width = 0, height = 0;
    ImageLayout layout = {};
};
// The texture of `holder` composed on the GPU: `size` x `size` texels of transparent black, each image (`size`
// square) drawn over the last (premultiplied: what is there x (1 - the image's alpha) + the image), the levels
// below made from it; the texture shows the levels from `mip` on, `levels` of them (its size `size` >> `mip`).
// Kept until the holder goes. False when not made (nothing shown).
bool ComposeTexture(const Surface* holder, int size, unsigned mip, unsigned levels, const TextureImage* images, int count);
// What a texture the game draws into (BlankTexture) keeps of the alpha drawn into it, and what it starts as.
enum class BlankAlpha : std::uint8_t {
    None,     // no alpha (a 5:6:5 texture's): opaque black, everything drawn into it opaque
    OneBit,   // only the alpha's top bit (a 1:5:5:5 texture's): transparent black
    Full,     // transparent black
};
// A texture of `holder` the game draws into on the GPU (DrawIntoTexture, DrawClouds): `width` x `height` texels; with
// `mipmapped` the levels below the first are made from it after each draw, else it has one level.
bool BlankTexture(const Surface* holder, int width, int height, BlankAlpha alpha, bool mipmapped);
// The rectangle `width` x `height` at `image`'s top-left put into the texture at (x, y), replacing what is there
// (clipped to the texture); without an image that rectangle is filled with opaque white.
void DrawIntoTexture(const Surface* holder, const TextureImage* image, int x, int y, int width, int height);
// The global map's clouds for one frame (DrawClouds; GlobalMapClouds, global_map_clouds.cpp).
struct MapClouds {
    float time = 0.0f;          // the drift: the game's seconds times its speed, wrapped every kMapCloudsPeriod
    int start = 0, width = 0;   // the ramp: a texel's alpha is 1 - (width / 255)^max(0, 255 x density - start)
    std::uint32_t seed = 0;     // the pattern
};
// MapClouds::time's wrap: every octave's drift has moved by whole repeats of the texture.
constexpr double kMapCloudsPeriod = 32768.0;
// The texture of `holder` (BlankTexture) made the clouds (render_compose.cpp, shaders/map_clouds.hlsl): white to grey
// by the light, the alpha their cover. It takes a size by the frame's (a texel about a pixel where the map shows it).
void DrawClouds(const Surface* holder, const MapClouds& clouds);

void BeginMesh(const TextureDevice* device, const MeshDraw& mesh);
void MeshPass(const TextureDevice* device);
void EndMesh();

// Pre-transformed vertices (FVF 0xC4, rhw taken as 1): the rectangle fill and outline. A triangle list over
// `indices`, or a line strip when `indices` is null. A draw it refuses is lost (logged).
struct ScreenVertex {
    float x = 0.0f, y = 0.0f, z = 0.0f, rhw = 0.0f;
    std::uint32_t colour = 0, specular = 0;
};
void ScreenDraw(const TextureDevice* device, const ScreenVertex* vertices, int count, const std::uint16_t* indices, int indexCount);

// The game's two layouts of pre-transformed vertices (the vertex format codes the game's draws pass): x, y, z, rhw, colour,
// specular, and in the textured one u, v.
inline constexpr int kFvfTextured = 0x1C4;
inline constexpr int kFvfUntextured = 0xC4;
// What TlDraw's vertices make.
enum class TlPrimitive { LineStrip, TriangleList };

// A draw of pre-transformed vertices, fog already applied: `fvf` kFvfTextured or kFvfUntextured, indexed when
// `indices`. A draw it refuses is lost (logged).
void TlDraw(const TextureDevice* device, const void* vertices, int fvf, int vertexCount, const std::uint16_t* indices, int indexCount,
            TlPrimitive primitive);

// A pixel format (the screen's, a texture's): 16 or 32 bits a pixel with the channels at their
// masks (`alphaMask` 0: no alpha). All zero: no format (a compressed texture's).
struct PixelFormat {
    int bitsPerPixel = 0;
    std::uint32_t redMask = 0, greenMask = 0, blueMask = 0, alphaMask = 0;
};

// A movie's decoded frame (R, G, B, X bytes), kept for `key` (the video object) until the next one.
void MovieFrame(const void* key, const std::uint8_t* rgbx, int width, int height);
void MovieBlit(const void* key, const Rect& to);

// The movie player's fill before its first frame (`colour`'s alpha ignored).
void Fill(const Rect& to, std::uint32_t colour);

// The cursor the frame shows (docs/notes/renderer.md section 9). The frame's present shows it as the system's cursor at
// the window's scale and through the gamma ramp (a shape per picture frame, made at its first show); a frame presented
// without it hides it (movies, loading screens).
struct FrameCursor {
    const Surface* imageHolder = nullptr;   // the cursor picture (its frames side by side)
    const void* mask = nullptr;          // over the whole picture, the surface's pixel format: texels where it is not 0 left out
    Rect from{0, 0, 0, 0};               // the frame shown
    Point hot;                           // the hot spot, texels into `from`
    bool held = false;                   // the game keeps its cursor at `heldAt`, a game point, while the mouse
    Point heldAt;                        // turns the camera
};
void Cursor(const FrameCursor& cursor);

// The game clears the depth buffer mid-frame (before the interface's panels and 3D previews); `rects`
// null: all of it.
void ClearDepth(float z, const Rect* rects, int count);

// A terrain pass of the game: the landscape's ground (`firstPage` 0) or water pages up to `lastPage` (pages with a
// texture), drawn whole (the zone's mesh of that layer, one draw); `renderer`'s transform pipeline holds the clip
// matrix; `drawn`: the sectors the game's clip test kept, in its order (the shadow map's fit and the water's mirror
// take their quarters not culled).
void TerrainPass(const terrain::Terrain* terrain, const SceneRenderer* renderer, int firstPage, int lastPage,
                 std::span<const terrain::SectorData* const> drawn);
// What the zone's mesh holds of a sector changed: Rebuilt, its tile lists and records (Build); Moved, the animated
// tiles' texture coordinates; WaterMoved, the water layer's positions and crests (the waves, a water level); Dropped,
// the sector is cleared or deleted (its place in the mesh is built again from what the grid holds then).
enum class TerrainChange { Rebuilt, Moved, WaterMoved, Dropped };
void TerrainChanged(const terrain::SectorData* sector, TerrainChange change);

// A figure part lit on the GPU as 3DfpFPU's LightMesh lit its words (renderer.md section 13). Lights
// and sun are taken into the part's model space.
struct MeshLightSource {
    Vector3 position;              // the landscape's space
    float inverseRangeSquared = 0.0f;
    float colour[3] = {};
};
struct MeshLighting {
    Matrix4 model;                 // the part's model space to the landscape's (row-major); with `camera`, the
                                   // camera times that
    const Matrix4* camera = nullptr;  // the scene's camera (SceneCamera, world.h), else null
    bool sun = false;
    Vector3 sunDirection;          // the direction its light travels
    float sunColour[3] = {};
    float ambientColour[3] = {};   // SceneRenderer::SetColour's last
    float diffuseColour[4] = {};   // the material's
    float emissiveColour[3] = {};
    float coefficient = 0.0f;      // the options' objects lighting coefficient (Light::SetLitColors)
    const MeshLightSource* lights = nullptr;  // the lights within the object's reach (its draw's list)
    int lightCount = 0;
};

// The figure mesh the next mesh draws draw, set by the figure part draws around the device's draw. The
// renderer keeps its corners on the GPU under `owner` and rebuilds only what changed: the colours when the
// draw wrote them, the positions on MeshChanged. A mesh draw without one is built every frame.
struct MeshSource {
    const FigureMesh* owner = nullptr;  // the figure mesh drawn
    const LinkMesh* shared = nullptr;    // the link figure whose part it draws, else null
    bool moved = false;    // the positions handed over are a copy with the per-vertex offsets added
    bool coloursWritten = false;  // this draw wrote the mesh's colour arrays (lit, flat colours)
    const MeshLighting* lighting = nullptr;   // lit on the GPU (the draw's colours: only their alpha)
};
void SetMeshSource(const MeshSource* source);
// A figure mesh or link figure changed in place: Positions (per-vertex offsets, a part morphed again),
// Colours outside its draw (the sky's lighting; a link figure's part lit or filled), Rebuilt (loaded,
// cleared, mirrored, a part added or removed, destroyed).
enum class MeshChange { Positions, Colours, Rebuilt };
void MeshChanged(const void* object, MeshChange change);
// Mesh draws between SetInterfaceDraw(true) and (false) (UiMesh::Draw) are built every frame: the interface
// rewrites its sprites' corners in place in many places.
void SetInterfaceDraw(bool on);

// Draws what was handed over since the last present and presents it. Once per frame; captures call it
// before reading.
void EndFrame();

// EndFrame if not done yet, then a new frame begins.
void Present();

// A frame drawn but not shown (the game's frame step without a present: a frame drawn for a copy of the screen):
// what was handed over is drawn into the frame target, which keeps it for copies until the next frame's first pass,
// and a new frame begins, so the next frame's draws are a frame of their own and not added to this one.
void EndUnshownFrame();

// 24-bit BGR pixels, rows from the top.
struct FrameImage {
    int width = 0, height = 0;
    std::vector<std::uint8_t> bgr;
};
// The last frame EndFrame drew; none without a frame.
std::optional<FrameImage> ReadFrame();

// The display's gamma ramp (three 256-entry ramps, full scale 65280; null channels identity), applied as
// the frame goes to the window; captures read the frame before it.
void SetGammaRamp(const std::uint16_t* red, const std::uint16_t* green, const std::uint16_t* blue);
// The ramp the present applies: 256 RGBA8 entries (the harness checks the cursor's shapes against it).
const std::uint8_t* GammaRampTable();
// The frame's size and where it shows in the window's client area (aspect kept, centred).
struct FramePlacement {
    Size frame;
    Rect placed;
};
// Where the frame shows now; none when there is no frame or window.
std::optional<FramePlacement> FrameInWindow();
// Client-area point to frame pixel (outside the frame: beyond its edges) and back (the centre of the frame
// pixel's window pixels).
Point WindowToFrame(Point p);
Point FrameToWindow(Point p);

} // namespace render
