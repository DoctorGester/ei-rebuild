#pragma once

// Model and resource loading: the 3D library side of figures and the
// databases read at startup (prints.db, acks.db). See docs/notes/models.md.

#include "chunk_archive.h"
#include "maths.h"
#include "render/render_state.h"
#include "renderer_base.h"
#include "script.h"   // ScriptStream
#include "water_tweens.h"

#include "lib/bit_flags.h"
#include "lib/containers.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <list>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

// ---- 3D library ---------------------------------------------------------------------------------

// ClipBounds::flags. A figure part's bounds (FootprintPart) also say how the part stands in the AI map: bits 3-5
// its OccluderKind (BoundsOccluderKind), bits 6-9 its transparency; the part's name sets them
// (object3d_figure_setup.cpp).
enum class BoundsFlag : std::uint32_t {
    Sphere = 0x1,          // the centre and radius are valid
    Box = 0x2,             // the box is valid
    Loaded = 0x4,          // a part's bounds are loaded
    Crown = 0x90,          // a "CROWN..." part: OccluderKind::Crown, transparency 2
    Base = 0x400,          // a "BASE..." part: its footprint is a bridge deck
    NoFootprint = 0x800,   // an "EMPTY..." part: no footprint
};
template <> inline constexpr bool lib::kBitFlags<BoundsFlag> = true;

enum class OccluderKind : std::int8_t;   // sector_map.h

// A part's occluder kind and transparency (index into the transparency table).
inline OccluderKind BoundsOccluderKind(std::uint32_t flags) { return static_cast<OccluderKind>((flags >> 3) & 7); }
inline std::int8_t BoundsTransparency(std::uint32_t flags) { return static_cast<std::int8_t>((flags >> 6) & 0xF); }

struct VertexPacket;

// Bounding volume of a figure part, tested against the view frustum.
struct ClipBounds {
    std::uint32_t flags;   // BoundsFlag
    Vector3 center;
    float radius;          // half the side of the cube around the centre
    Vector3 boxMin;        // box corners, relative to the centre
    Vector3 boxMax;

    // Sets the box and its flag; returns the new flags.
    std::uint32_t SetBox(const float* min, const float* max);
    // Fits the box to the points of `blocks` packets of four (the centre midway between their extremes, the box
    // around it) and the sphere around the box (its half diagonal), and sets both flags.
    void Fit(const VertexPacket* packets, int blocks);
};

// A box of an object's model that blocks the AI map: the part's bounds with the part's position when it was
// loaded. A figure mesh has one (FigureMesh::box), copied whole from the piece's header into the object's
// part boxes (CObject3D::partBoxes).
struct FootprintPart {

    ClipBounds bounds;      // flags: also the part's occluder kind and transparency; the centre is the part's offset
    Vector3 pivot;
};

// One of the transform pipeline's matrix stacks: the matrices and the depth.
constexpr int kMatrixStackDepth = 16;   // matrices a stack has room for
struct MatrixStack {
    std::array<Matrix4, kMatrixStackDepth> matrices;
    int count = 0;          // the top is matrices[count - 1]
};

struct TLVertex;
struct LightningPoint;
class CLight;

// A screen-facing sprite's centre and size (SceneRenderer::DrawSprites' points).
struct SpritePoint {
    float x, y, z, size;
};

// `m` times the point (x, y, z, 1), each row's sum adding the translation first
// (((x m[4i] + m[4i + 3]) + y m[4i + 1]) + z m[4i + 2]): the projections' clip space.
Vector4 ToClip(const Matrix4& m, float x, float y, float z);
// The points of `blocks` packets of four projected with `m` into `points` (x, y each; the translation added
// last), `bounds` (min x, min y, max x, max y) grown around them (the shadows' projection).
void ProjectFlat(const Matrix4& m, const VertexPacket* positions, Vector2* points, int blocks, float* bounds);

// The transform pipeline of the 3D library: the matrix stacks, and the projections of points to clip space and
// to the screen, with the viewport and the fog's depth range they map with (docs/notes/renderer.md, section 10).
class TransformPipeline final : public RendererBase {
public:
    // Stack 2 holds the matrix ClipTest projects with; 14 the decal projector's platform objects
    // and parts (WorldClient::ProjectDecal).
    static constexpr int kClipStack = 2;
    static constexpr int kDecalStack = 14;
    // Stack 0 is the picking's (the view matrix pushed onto it), 9 the view matrices.
    static constexpr int kPickStack = 0;
    static constexpr int kViewStack = 9;

    // Classifies `bounds` against the view volume of the current (top) matrix:
    // 2 = entirely outside one clip plane, 0 = entirely inside, 1 = crossing or not decided.
    // The cube (flag 0) is tried first, then the box (flag 1).
    int ClipTest(const ClipBounds* bounds) const;
    // Pushes `m` onto stack `index`: an empty stack gets `m` itself, otherwise the new top is the old top
    // times `m` (no capacity check).
    void PushMatrix(const float* m, int index);
    // The verdict on `blocks` packets of four positions as ClipTest classifies `bounds` (null: crossing): 0 on
    // screen, 2 outside, and when crossing ProjectClipped's with stack 2's top (writing `projected` and `outcodes`).
    int ProjectBlocks(const VertexPacket* positions, Vector4* projected, std::uint8_t* outcodes, int blocks,
                      const ClipBounds* bounds) const;
    // PushMatrix as the draw code inlines it: the new top by Matrix4::Multiply, whose elements sum in
    // another order.
    void PushMatrixProduct(const Matrix4& m, int index) {
        MatrixStack& stack = stacks[index];
        if (stack.count == 0) {
            stack.count = 1;
            stack.matrices[0] = m;
            return;
        }
        const int depth = ++stack.count;
        stack.matrices[depth - 1].Multiply(stack.matrices[depth - 2].m, m.m);
    }
    // The top of stack `index` (matrices[count - 1], computed even when empty).
    const Matrix4* Top(int index) const { return stacks[index].matrices.data() + (stacks[index].count - 1); }
    void PopMatrix(int index) {
        if (stacks[index].count != 0) --stacks[index].count;
    }

    // The viewport the projections map clip space to: the screen's size.
    void SetViewportSize(float width, float height);
    // The distance fog's depth range: where it starts and where it is full.
    void SetDepthRange(float start, float end);

    // The positions of `blocks` packets of four projected with `m` (a screen matrix: stack 1's top) into
    // `projected`: x, y and z divided by w, and 1 / w (each row's sum adding the translation last).
    static void Project(const Matrix4& m, const VertexPacket* positions, Vector4* projected, int blocks);
    // The positions of `blocks` packets of four to clip space with `m` (ToClip) into `projected`, an outcode
    // each; returns the verdict (ClipVerdict). On screen (0) they are taken to the screen (ToScreen).
    int ProjectClipped(const Matrix4& m, const VertexPacket* positions, Vector4* projected, std::uint8_t* outcodes,
                       int blocks) const;
    // The same for `count` ribbon points: Project's division without clipping (with ToClip's sums), and
    // ProjectClipped's.
    static void ProjectPoints(const Matrix4& m, const LightningPoint* points, Vector4* projected, int count);
    int ProjectPointsClipped(const Matrix4& m, const LightningPoint* points, Vector4* projected, std::uint8_t* outcodes,
                             int count) const;
    // `count` screen-facing sprites, their centres to the screen with `m` (the clip stack's top), as four
    // corners each (positions only; `size` the half side at w 1).
    void ProjectSprites(const Matrix4& m, const SpritePoint* points, TLVertex* corners, int count) const;
    // A clip-space point on the screen: x right and y down from the top left corner, z / w, and 1 / w.
    Vector4 ToScreen(const Vector4& p) const;
    // ToScreen on the first 16 bytes (x, y, z, w) of `count` vertices.
    void ToScreen(TLVertex* vertices, int count) const;
    // The distance fog of `count` screen vertices: the specular's alpha lowered to 255 less the fog of the
    // vertex's depth (none before the range's start, full at its end).
    void FogVertices(TLVertex* vertices, int count) const;

    // Counted (RendererBase): holders drop a reference, and the last one deletes it.

    MatrixStack stacks[16];     // (models.md: stacks 0-2, 5-10 and 14 are in use)
    float halfWidth = 0.0f;     // the viewport's half size (SetViewportSize)
    float halfHeight = 0.0f;
    float fogStart = 0.0f;   // the fog's depth range's start, and 255 over its length (SetDepthRange)
    float fogScale = 0.0f;
    Matrix4 projection{};   // the projection matrix (the fog's depth from a screen z)
};
struct tagRECT;
class TextureDevice;
namespace options {
class GameOptions;
}
class CTexture;
class FigureMesh;
class Material;
struct LineBatch;

// A render state preset the renderer's helper keeps (counted): the passes of a material's draw. The first pass
// sets the texture stages of the first `stages` states and turns the next stage off; each further pass is one of
// the states from `stages` on, with its blend factors.
class RenderPreset final {
public:
    // One texture stage's operations and the blend factors of its pass.
    struct State {
        bool enabled;                     // the stage's operations are set
        render::StageCombiner combiner;
        bool blend;                       // the blend factors are set
        render::Blend sourceBlend;
        render::Blend destBlend;
    };

    RenderPreset(const std::vector<State>& newStates, int newStages) : states(newStates), stages(newStages) {}

    RenderPreset(const RenderPreset&) = delete;
    RenderPreset& operator=(const RenderPreset&) = delete;

    int PassCount() const { return static_cast<int>(states.size()) - stages + 1; }
    // Pass `pass`'s states set in the renderer's render state (render/render_state.h).
    void ApplyPass(int pass) const;

    void AddRef() { ++refs; }
    // Drops a reference; the last one deletes the preset.
    void Release() {
        if (--refs == 0) delete this;
    }

    int refs = 0;
    std::vector<State> states;
    int stages;                          // texture stages the first pass sets
};

// The renderer's counted helper: the nine built-in render state presets of the materials.
class RenderHelper final {
public:
    static constexpr unsigned kPresetCount = 9;

    // Records the nine presets (`flag`'s low byte: one texture stage each).
    explicit RenderHelper(std::uint32_t flag);
    ~RenderHelper();
    // Preset `index` of the nine, or null beyond them.
    RenderPreset* Preset(unsigned index) const { return index < kPresetCount ? presets[index] : nullptr; }

    RenderHelper(const RenderHelper&) = delete;
    RenderHelper& operator=(const RenderHelper&) = delete;

    void AddRef() { ++refs; }
    // Drops a reference; the last one deletes the helper.
    void Release() {
        if (--refs == 0) delete this;
    }

    int refs = 0;

private:
    // Preset `index` made of `states`. `singleStage`: at most one texture stage.
    void Record(const std::vector<RenderPreset::State>& states, unsigned index, bool singleStage);

    RenderPreset* presets[kPresetCount] = {};    // counted
};

// The ambient colour (r, g, b in 0..1) SceneRenderer::SetColour set last (one for every renderer): the figure
// parts lit on the GPU take it from here.
const float* AmbientColour();

// The scene renderer (the global renderer object, &g_sceneRenderer; "CVisualizator" in the game's messages).
class SceneRenderer final {
public:
    SceneRenderer() = default;
    ~SceneRenderer();
    SceneRenderer(const SceneRenderer&) = delete;
    SceneRenderer& operator=(const SceneRenderer&) = delete;

    // Sets the render state's fog colour (render/render_state.h) to `colour`.
    int SetFogColourState(std::uint32_t colour);
    void CreateRenderer(void* window, options::GameOptions* settings);
    // Drops the sun, the pipeline, the helper and the device object.
    void DestroyRenderer();
    // 0 unless flags bit 0 is set. Takes the frame time, clears the frame and begins the scene (a
    // scene still open is ended first; other failures are fatal). Returns 1.
    int BeginScene();
    // Ends the scene. Returns 1.
    int EndScene();
    std::uint32_t FrameTime() const { return frameTime; }
    // Sets the colour the next draws are tinted with (colour and the device object's colour) and the transform
    // object's ambient from it.
    void SetColour(std::uint32_t colour);
    // Sets SRCBLEND, then DESTBLEND.
    int SetBlend(render::Blend source, render::Blend dest);
    // Binds `texture` (a texture object, or null) to stage `stage`.
    int SetTexture(int stage, CTexture* texture);
    // Draws `figure` (a FigureMesh) through its drawer's lit draw with `lights`, the drawing context
    // `material`, `alpha`, `screen` (or null) and two textures, unless it is hidden (flags bit 0x80).
    int DrawFigureLit(FigureMesh* figure, const std::vector<CLight*>* lights, Material* material, int alpha, tagRECT* screen, CTexture* textureA,
                      CTexture* textureB);
    // Draws `figure` through its drawer unless it is hidden (flags bit 0x80); `alpha` reaches the mesh
    // draw, `screen` (or null) gets the rectangle drawn, `material` (null, or an interface sprite's material)
    // is passed on.
    int DrawFigure(FigureMesh* figure, Material* material, int alpha, tagRECT* screen);
    void DrawLineList(LineBatch* lines);
    // Draws a strip along `points` (a lightning segment's aligned points) of `width` with the bound texture:
    // `visibility` the bounds' clip result, the texture's `length` and `offset` along it (0 and 0: untextured
    // lengthwise), `slant` 0.25 at every call known.
    int DrawStrip(const std::vector<LightningPoint>* points, float width, int visibility, float length, float offset,
                  float slant);
    // Draws sprites facing the view with the bound texture: `points` an aligned array of centres and sizes
    // (x, y, z, size), `sprites` an array of their colours and texture rectangles. The first argument is 0
    // at every call known.
    int DrawSprites(int unknown, void* points, void* sprites);
    // The selection marker under a selected unit (the draw pass): a flat green arrow of half-transparent
    // 0x8000FF00, 0.3 above `point`, `size` long, pointing along -`axisY` (`axisX` across), drawn by the
    // device's flat geometry draw as six triangles of two vertex blocks.
    int DrawSelectionMarker(const Vector3& point, float size, const Vector3& axisX, const Vector3& axisY);
    // A black rectangle (left, top, right, bottom in screen pixels) of `alpha` over the current blend state,
    // with `outline` a white frame. Returns 1.
    int DrawRectangle(int left, int top, int right, int bottom, int alpha, int outline);
    // The renderer's gamma ramp (applied as the frame goes to the window) from the options' brightness,
    // contrast and gamma (kept for GetGamma); false without a device.
    bool SetGamma(float brightness, float contrast, float gamma);
    void GetGamma(float* brightness, float* contrast, float* gamma) const;
    // After a switch back (WM_ACTIVATE): a full-screen device gets its cooperative level and display mode
    // back, then every surface and the texture manager are restored. False without a device object.
    bool RestoreSurfaces();

    // Makes sure `texture` is loaded and binds it to `stage`.
    int BindLoadedTexture(int stage, CTexture* texture);
    // The fog (and clear) colour from r, g, b in 0..1 (each rounded to the nearest integer), stored and set
    // through SetFogColourState.
    void SetFogColour(float r, float g, float b);
    // The sun becomes `light` (counted; the old one released).
    void SetSun(CLight* light);
    // SetSun, then the colour from r, g, b in 0..1, packed as SetFogColour packs them.
    void SetSunAndAmbient(CLight* light, float r, float g, float b);

    std::uint32_t flags = 0;                   // bit 0: the software cursor option; bit 1 set by the creation
    Ref<TransformPipeline> pipeline;
    std::uint32_t fogColour = 0;               // also the colour the frame is cleared with
    std::uint32_t colour = 0;                  // the draw colour
    // The sun (counted; set together with the ambient colour, the client's lighting moves it every frame).
    Ref<CLight> sun;
    Ref<TextureDevice> device;                // the device object
    Ref<RenderHelper> helper;
    // The display settings SetGamma last applied, as given.
    float brightness = 0.0f;
    float contrast = 0.0f;
    float gamma = 0.0f;
    // The frame time (milliseconds; BeginScene / EndScene).
    std::uint32_t frameTime = 0;               // the last frame's
    std::uint32_t frameStart = 0;              // timeGetTime at this frame's BeginScene
    std::uint32_t lastFrameStart = 0;          // the previous frame's (the creation: its time)

private:
    // The creation's first step: a new transform pipeline.
    void CreatePipeline();
    // Drops the sun and the pipeline.
    void ReleasePipeline();
};


// Viewport matrix for a screen rectangle {left, top, right, bottom}: scale and offset by the half extents,
// y flipped.
Matrix4* ViewportMatrix(Matrix4* out, const tagRECT* rect);
// Perspective projection; `fov` in radians (full angle).
Matrix4* ProjectionMatrix(Matrix4* out, float fov, float aspect, float zNear, float zFar);

// A triangle corner of a figure part: the vertex (position) index, a second index (the normal's; the interface
// sprites write 0) and the texture coordinate index.
struct MeshCorner {
    std::uint16_t position;
    std::uint16_t normal;
    std::uint16_t texCoord;
};
static_assert(sizeof(MeshCorner) == 6);

class MappedResource;

// An archive of LinkFigureLoader's: its path and the open archive.
struct LinkArchive {
    std::string name;
    MappedResource* resource = nullptr;   // a MMIOResource (the loader's destructor deletes it)
};

// A figure's cached model: the mesh, bones and animation archives of "<name>.mod / .bon / .anm" (null when
// missing), and the animations read from the last by name. CFigureManager::models and the link figure
// loader's figure sets cache them by name.
struct FigureModel {
    // An empty animation map, then the three archives as new CMMIOResources.
    FigureModel();
    // Every cached animation deleted, the map cleared, each archive closed (CloseResource) and deleted.
    ~FigureModel();
    FigureModel(const FigureModel&) = delete;
    FigureModel& operator=(const FigureModel&) = delete;
    MappedResource* FindAnimation(const std::string* name);

    MappedResource* mesh;          // the .mod archive
    MappedResource* bones;
    MappedResource* animations;
    std::unordered_map<std::string, MappedResource*> animationCache;    // each a MMIOResource over the animation's data

    // The link figure loader's sets open the three archives from the images of the figure's .mod / .bon /
    // .anm files (OpenFromMemory); a null image leaves its archive alone.
    void Open(const void* mod, const void* bon, const void* anm);
};

// Loads .lnk link files and the figures they name (created at startup with figures.res and menus.res). A
// figure is a LinkMesh of parts morphed by three coefficients (s, t, u); the loader keeps every mesh it made
// in a multimap by name (a counted reference each) and hands out a cached one whose coefficients are within
// 1e-5 (squared distance) unless asked for a fresh one. Parts come from the figure's own archive set when
// there is one, else from the open archives as "<figure><part>.fig".
class LinkMesh;

class LinkFigureLoader final {
public:
    // The figure and menu archives of `folder` opened (a failure is only logged).
    LinkFigureLoader(TextureDevice* renderer, const std::string* folder);
    virtual ~LinkFigureLoader();
    virtual int CacheSize();
    virtual int CacheBytes();
    // Figure `name` with the parts `parts`: unless `fresh`, the first cached mesh of that name with close
    // coefficients, given the parts it lacks; otherwise a new one (MakeFigure). No reference is added for the
    // caller.
    virtual LinkMesh* LoadFigure(const std::string* name, const std::vector<std::string>* parts, float s, float t,
                                  float u);
    // The same for a .lnk figure: a cached one, or LoadLinkFile.
    virtual LinkMesh* LoadLink(const std::string* name, float s, float t, float u);
    virtual void ClearCache();
    // `mesh` with new coefficients: a mesh others also hold (more than two references) is looked up again by
    // its name; otherwise its parts are morphed in place (SetMorph, then per part). Null stays null.
    virtual LinkMesh* Remorph(LinkMesh* mesh, float s, float t, float u);

    void AddRef() { ++refCount; }
    // Drops a reference; the last one deletes the loader.
    void Release() {
        if (--refCount == 0) delete this;
    }

    LinkFigureLoader(const LinkFigureLoader&) = delete;
    LinkFigureLoader& operator=(const LinkFigureLoader&) = delete;

    // Opens `path` as a memory-mapped archive and appends it to the list, unless an archive of that exact
    // name is already open. Returns 1 on success or when already open.
    int AddArchive(const std::string* path);
    // Reads a FIG / FIG8 part record at *cursor (moved past it) into `mesh` as part `name`: its header (two
    // dwords, bounds fitted to the vertices morphed by s, t, u) and its elements (LinkMesh::AddPart).
    void LoadFigPart(LinkMesh* mesh, const std::string* name, const char** cursor, float s, float t, float u);
    // The same for the older record without a FIG header (6 morph corners).
    void LoadOldPart(LinkMesh* mesh, const std::string* name, const char** cursor, float s, float t, float u);

    // A new mesh from .lnk file `name` (from the figure set, else the archives' "<name>.lnk"): a count, then
    // per part its name and a second, unused name (an empty name keeps the previous one). Cached (one
    // reference). No link file: FatalError's result (0) is returned and the new mesh leaked.
    LinkMesh* LoadLinkFile(const std::string* name, float s, float t, float u);
    // A new mesh `name` with the parts `parts`, loaded as LoadLinkFile does (a part missing from the figure
    // set is skipped); cached.
    LinkMesh* MakeFigure(const std::string* name, const std::vector<std::string>* parts, float s, float t, float u);
    // Part `name` of `mesh` (its own coefficients) unless it has it: from its figure set (false when the set
    // lacks it) or the archives (LoadArchivePart's result).
    bool LoadPartInto(LinkMesh* mesh, const std::string* name);
    // Part `name` of `mesh` from the first open archive that has "<mesh name><name>.fig".
    int LoadArchivePart(LinkMesh* mesh, const std::string* name, float s, float t, float u);
    // Figure `name`'s own archive set: the cached one, else (when an open archive has "<name>.mod") a new set
    // stored under the name and loaded from the first archives with "<name>.mod" / ".bon" / ".anm"; null
    // without a .mod.
    FigureModel* FindFigureSet(const std::string* name);
    // Part `name` of `mesh` morphed again by s, t, u from its record (the figure set's model, else
    // "<mesh name><name>.fig" in the archives): false without one.
    bool RemorphPart(LinkMesh* mesh, FigureModel* set, const std::string* name, float s, float t, float u);
    // The record at *cursor (moved past the vertices) morphed into the part's vertex packets and its header's
    // bounds refitted. The FIG one reads every record as FIG8 (header, bounds and vertices).
    void RemorphFigPart(LinkMesh* mesh, const std::string* name, const char** cursor, float s, float t, float u);
    void RemorphOldPart(LinkMesh* mesh, const std::string* name, const char** cursor, float s, float t, float u);

    int refCount = 0;
    std::list<LinkArchive> archives;
    std::unordered_map<std::string, FigureModel*> figureSets;     // by figure name
    Ref<TextureDevice> renderer;                                 // the device object
    std::multimap<std::string, LinkMesh*> cache;     // each entry holds a reference
};

// What chunks 2 and 3 of both prints.db record types hold: a print's alpha and its life and lifetime in ticks
// (chunk 3 while it rains or snows).
struct PrintLook {
    float alpha;
    int life;
    int lifetime;
};

// A record of prints.db (terrain footprints).
struct PrintRecord {
    PrintRecord();
    PrintRecord(const PrintRecord& from);
    PrintRecord& operator=(const PrintRecord&) = delete;

    virtual void Serialize(ChunkArchive* ar);

    std::string name;
    PrintLook dryLook;           // chunk 2
    PrintLook wetLook;           // chunk 3 (while it rains or snows)
};

// The second record type of prints.db.
struct PrintRecord2 {
    PrintRecord2();
    PrintRecord2(const PrintRecord2& from);
    PrintRecord2& operator=(const PrintRecord2&) = delete;

    virtual void Serialize(ChunkArchive* ar);

    std::string name;
    PrintLook dryLook;           // chunk 2
    PrintLook wetLook;           // chunk 3 (while it rains or snows)
    std::uint32_t unreadChunk[3];   // chunk 10
};

inline const PrintLook& LookOf(const PrintRecord& r, bool second) { return second ? r.wetLook : r.dryLook; }
inline const PrintLook& LookOf(const PrintRecord2& r, bool second) { return second ? r.wetLook : r.dryLook; }

// prints.db (loaded at startup), indexed by the ground's surface class (Terrain::SurfaceClass): chunk 1
// blood and bloody footprints, chunk 2 footprints, chunk 3 scorch marks; each table is followed by a
// hash_map by name (filled by the load, never read).
struct PrintsDatabase {
    // "prints.db" of `folder` + "database.res" (Rebirth-patched): the three tables, each read through an
    // archive of its own and filed under its records' names. 1; FatalError's result when the archive
    // (reported as "databases.res") or the file cannot be opened.
    int Load(const std::string& folder);

    std::vector<PrintRecord> blood;
    std::unordered_map<std::string, PrintRecord*> bloodByName;
    std::vector<PrintRecord2> footprints;
    std::unordered_map<std::string, PrintRecord2*> footprintsByName;
    std::vector<PrintRecord> scorch;
    std::unordered_map<std::string, PrintRecord*> scorchByName;
};
extern PrintsDatabase g_printsDatabase;   // database_globals.cpp

// A length-prefixed name (a four-byte length, then the characters) at *cursor into `out` (emptied for length
// 0), the cursor moved past it.
void ReadPrefixedName(std::string* out, const std::uint8_t** cursor);
