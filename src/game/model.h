#pragma once

// Model and resource loading: the 3D library side of figures and the
// databases read at startup (prints.db, acks.db). See docs/notes/models.md.

#include "chunk_archive.h"
#include "maths.h"
#include "renderer_base.h"
#include "render/render_state.h"
#include "script.h"   // ScriptStream
#include "water_tweens.h"

#include "lib/bit_flags.h"
#include "lib/containers.h"
#include "lib/static_object.h"

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

// Bounding volume of a figure part, tested against the view frustum.
struct ClipBounds {
    std::uint32_t flags;   // BoundsFlag
    Vector3 center;
    float radius;          // half the side of the cube around the centre
    Vector3 boxMin;        // box corners, relative to the centre
    Vector3 boxMax;

    // Sets the box and its flag; returns the new flags.
    std::uint32_t SetBox(const float* min, const float* max);
};

// A box of an object's model that blocks the AI map: the part's bounds with the part's position when it was
// loaded. A figure mesh has one (CFigureMesh::box_), copied whole from the piece's header into the object's
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

struct VertexPacket;
struct TLVertex;
class CLight;

// The transform object of 3DfpFPU.dll (GetTLPipeline; CTransformPipeline::transform_), as far as
// the code calls it.
class ITransformPipeline {
public:
    virtual void Unused0();
    virtual void SetViewportSize(float width, float height);
    // The sun for the next LightMesh (its localDirection_).
    virtual void AddSun(CLight* sun);
    virtual void SetAmbient(float r, float g, float b);
    virtual void SetProjection(float* transposed);
    // The depth range (the options' fog start and far clip distances).
    virtual void SetDepthRange(float first, float second);
    virtual void Unused6();
    // Fits `bounds` around `count` vertex packets.
    virtual void FitBounds(ClipBounds* bounds, const VertexPacket* vertices, int count);
    // `count` points (16 bytes each: x, y, z and one more) projected with `matrix` into
    // `projected` (x, y, z, w), a flag byte per point set when it is off screen; 2 when every point
    // is, else 0 or 1 (the ribbon draw).
    virtual int ProjectPointsClipped(const Matrix4* matrix, const void* points, void* projected, std::uint8_t* flags,
                                     int count);
    // Projects `blocks` blocks of four positions with `matrix` into `projected`, writing their
    // clip outcodes; the result is the caller's (on screen / crossing / outside).
    virtual int ProjectClipped(const Matrix4* matrix, const void* positions, void* projected, void* outcodes,
                               int blocks);
    virtual void Unused10();
    virtual void Unused11();
    virtual void Unused12();
    // The same without clipping (the ribbon draw's mode 0).
    virtual void ProjectPoints(const Matrix4* matrix, const void* points, void* projected, int count);
    // The same without clipping (every block on screen); the result as the clipping one's.
    virtual int Project(const Matrix4* matrix, const void* positions, void* projected, int blocks);
    virtual void Unused15();
    virtual void Unused16();
    virtual void Unused17();
    // The same for the terrain's vertices.
    virtual int ProjectTerrainClipped(const Matrix4* matrix, const void* vertices, void* projected, void* outcodes,
                                      int count);
    virtual void ProjectTerrain(const Matrix4* matrix, const void* vertices, void* projected, int count);
    virtual void Unused20();
    virtual void Unused21();
    virtual void Unused22();
    virtual void Unused23();
    // `count` screen-facing sprites (`points`: centre x, y, z and size each) placed with the transposed view
    // matrix as four TL vertices each into `corners` (positions only), with an outcode byte per corner: 0 all
    // on screen, 1 some off screen, else none.
    virtual int ProjectSprites(const void* transposedView, const void* points, TLVertex* corners,
                               std::uint8_t* outcodes, int count);
    virtual void Unused25();
    // The `blocks` packets of four positions times the (transposed) matrix, as points (x, y, z, w) into `out`.
    virtual void TransformPoints(const Matrix4* transposed, const VertexPacket* vertices, Vector4* out, int blocks);
    virtual void Unused27();
    virtual void Unused28();
    // The `blocks` packets projected with `matrix` into `points` (x, y per vertex), `bounds` (min x, min y,
    // max x, max y) grown around them.
    virtual void ProjectFlat(const float* matrix, const VertexPacket* vertices, Vector2* points, int blocks,
                             float* bounds);
    virtual void Unused30();
    virtual void Unused31();
    // `count` projected vertices (x, y, z, w in the first 16 bytes) to the screen: 1 / w kept as rhw, x and y
    // scaled to the viewport, z divided.
    virtual void ToScreen(TLVertex* vertices, int count);
    virtual void Unused33();
    // The vertices ClipTriangles left, projected to the screen as ToScreen does.
    virtual void ToScreenClipped(TLVertex* vertices, int count);
    // The distance fog of `count` screen vertices: the specular's alpha lowered to the depth's fog factor.
    virtual void FogVertices(TLVertex* vertices, int count);
    virtual void Unused36();
    // Clips the triangles `indices` (`*indexCount` indices) of the `*vertexCount` vertices with
    // their `outcodes` against the screen: the vertices it adds are appended (the callers make room
    // for ten times the input), the triangles left written to `clippedIndices`, both counts updated.
    virtual int ClipTriangles(TLVertex* vertices, std::uint8_t* outcodes, const std::uint16_t* indices,
                              std::uint16_t* clippedIndices, int* vertexCount, int* indexCount);
    virtual void Unused38();
    virtual void Unused39();
    // Lights a figure mesh: its vertex packets and normals, the colours written (one per normal), its words,
    // the lights (their localPosition_, colours set for the material) and the material's light vector.
    virtual void LightMesh(const void* vertices, const void* normals, std::uint32_t* colours, const void* words,
                           int wordCount, CLight** lights, int lightCount, const std::uint32_t* vector);
};

// The transform pipeline of the 3D library: the matrix stacks and 3DfpFPU's transform object.
class CTransformPipeline final : public CRendererBase {
public:
    // Stack 2 holds the matrix ClipTest projects with; 14 the decal projector's platform objects
    // and parts (CWorldClient::ProjectDecal).
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
    // Projects `blocks` blocks of four positions as ClipTest classifies `bounds` (null: crossing): on screen
    // with stack 1's top (0), crossing with stack 2's (ProjectClipped's result), else nothing (2).
    int ProjectBlocks(const void* positions, void* projected, void* outcodes, int blocks, const ClipBounds* bounds);
    // The same for terrain vertices, both with stack 2.
    int ProjectTerrain(const void* vertices, void* projected, void* outcodes, int count, const ClipBounds* bounds);
    // Loads 3DfpFPU.dll from `folder` and takes its transform object (throws without either); also the
    // processor's CPUID function 1 into the globals.
    explicit CTransformPipeline(const char* folder);
    ~CTransformPipeline() override;
    // PushMatrix as the draw code inlines it: the new top by Matrix4::Multiply, whose elements sum in
    // another order.
    void PushMatrixProduct(const Matrix4& m, int index) {
        MatrixStack& stack = stacks_[index];
        if (stack.count == 0) {
            stack.count = 1;
            stack.matrices[0] = m;
            return;
        }
        const int depth = ++stack.count;
        stack.matrices[depth - 1].Multiply(stack.matrices[depth - 2].m, m.m);
    }
    // The top of stack `index` (matrices[count - 1], computed even when empty).
    const Matrix4* Top(int index) const { return stacks_[index].matrices.data() + (stacks_[index].count - 1); }
    void PopMatrix(int index) {
        if (stacks_[index].count != 0) --stacks_[index].count;
    }

    // Counted (CRendererBase): holders drop a reference, and the last one deletes it.

    ITransformPipeline* transform_;   // 3DfpFPU.dll's transform object (projects and clips vertices)
    void* library_;                   // 3DfpFPU.dll's module (FreeLibrary in the destructor)
    MatrixStack stacks_[16];    // (models.md: stacks 0-2, 5-10 and 14 are in use)
};

struct tagRECT;
class CTextureDevice;
namespace options {
class GameOptions;
}
class CTexture;
class CFigureMesh;
class CMaterial;
struct LineBatch;
struct LightningPoint;

// A render state preset the renderer's helper keeps (counted): the passes of a material's draw. The first pass
// sets the texture stages of the first `stages_` states and turns the next stage off; each further pass is one of
// the states from `stages_` on, with its blend factors.
class CRenderPreset final {
public:
    // One texture stage's operations and the blend factors of its pass.
    struct State {
        bool enabled;                     // the stage's operations are set
        render::StageCombiner combiner;
        bool blend;                       // the blend factors are set
        render::Blend sourceBlend;
        render::Blend destBlend;
    };

    CRenderPreset(const std::vector<State>& states, int stages) : states_(states), stages_(stages) {}

    CRenderPreset(const CRenderPreset&) = delete;
    CRenderPreset& operator=(const CRenderPreset&) = delete;

    int PassCount() const { return static_cast<int>(states_.size()) - stages_ + 1; }
    // Pass `pass`'s states set in the renderer's render state (render/render_state.h).
    void ApplyPass(int pass) const;

    void AddRef() { ++refs_; }
    // Drops a reference; the last one deletes the preset.
    void Release() {
        if (--refs_ == 0) delete this;
    }

    int refs_ = 0;
    std::vector<State> states_;
    int stages_;                         // texture stages the first pass sets
};

// The renderer's counted helper: the nine built-in render state presets of the materials.
class CRenderHelper final {
public:
    static constexpr unsigned kPresetCount = 9;

    // Records the nine presets (`flag`'s low byte: one texture stage each).
    explicit CRenderHelper(std::uint32_t flag);
    ~CRenderHelper();
    // Preset `index` of the nine, or null beyond them.
    CRenderPreset* Preset(unsigned index) const { return index < kPresetCount ? presets_[index] : nullptr; }

    CRenderHelper(const CRenderHelper&) = delete;
    CRenderHelper& operator=(const CRenderHelper&) = delete;

    void AddRef() { ++refs_; }
    // Drops a reference; the last one deletes the helper.
    void Release() {
        if (--refs_ == 0) delete this;
    }

    int refs_ = 0;

private:
    // Preset `index` made of `states`. `singleStage`: at most one texture stage.
    void Record(const std::vector<CRenderPreset::State>& states, unsigned index, bool singleStage);

    CRenderPreset* presets_[kPresetCount] = {};   // counted
};

// The ambient colour (r, g, b in 0..1) SetBaseColour last gave the transform object: 3DfpFPU's object
// is one for every renderer, and its LightMesh lit the figure parts with it; the parts lit on the GPU
// take it from here.
const float* TransformAmbientColour();

// The scene renderer (the global renderer object, &g_sceneRenderer.Get(); "CVisualizator" in the game's messages).
class CSceneRenderer final {
public:
    CSceneRenderer() = default;
    ~CSceneRenderer();
    CSceneRenderer(const CSceneRenderer&) = delete;
    CSceneRenderer& operator=(const CSceneRenderer&) = delete;

    // Sets the render state's fog colour (render/render_state.h) to `colour`.
    int SetFogColourState(std::uint32_t colour);
    void CreateRenderer(void* window, options::GameOptions* settings);
    // Drops the sun, the pipeline, the helper and the device object.
    void DestroyRenderer();
    // 0 unless flags_ bit 0 is set. Takes the frame time, clears the frame and begins the scene (a
    // scene still open is ended first; other failures are fatal). Returns 1.
    int BeginScene();
    // Ends the scene. Returns 1.
    int EndScene();
    std::uint32_t FrameTime() const { return frameTime_; }
    // Sets the colour the next draws are tinted with (colour_ and the device object's colour_) and the transform
    // object's ambient from it.
    void SetColour(std::uint32_t colour);
    // Sets SRCBLEND, then DESTBLEND.
    int SetBlend(render::Blend source, render::Blend dest);
    // Binds `texture` (a texture object, or null) to stage `stage`.
    int SetTexture(int stage, CTexture* texture);
    // Draws `figure` (a CFigureMesh) through its drawer's lit draw with `lights`, the drawing context
    // `material`, `alpha`, `screen` (or null) and two textures, unless it is hidden (flags bit 0x80).
    int DrawFigureLit(CFigureMesh* figure, const std::vector<CLight*>* lights, CMaterial* material, int alpha, tagRECT* screen, CTexture* textureA,
                      CTexture* textureB);
    // Draws `figure` through its drawer unless it is hidden (flags bit 0x80); `alpha` reaches the mesh
    // draw, `screen` (or null) gets the rectangle drawn, `material` (null, or an interface sprite's material)
    // is passed on.
    int DrawFigure(CFigureMesh* figure, CMaterial* material, int alpha, tagRECT* screen);
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
    int DrawSelectionMarker(const Vector3* point, float size, const Vector3* axisX, const Vector3* axisY);
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
    // The sun becomes `light` (counted; the old one released) and goes to the transform object (AddSun).
    void SetSun(CLight* light);
    // SetSun, then the colour from r, g, b in 0..1, packed as SetFogColour packs them.
    void SetSunAndAmbient(CLight* light, float r, float g, float b);

    std::uint32_t flags_ = 0;                  // bit 0: the software cursor option; bit 1 set by the creation
    Ref<CTransformPipeline> pipeline_;
    std::uint32_t fogColour_ = 0;              // also the colour the frame is cleared with
    std::uint32_t colour_ = 0;                 // the draw colour
    // The sun (counted; set together with the ambient colour, the client's lighting moves it every frame).
    Ref<CLight> sun_;
    Ref<CTextureDevice> device_;               // the device object
    Ref<CRenderHelper> helper_;
    // The display settings SetGamma last applied, as given.
    float brightness_ = 0.0f;
    float contrast_ = 0.0f;
    float gamma_ = 0.0f;
    // The frame time (milliseconds; BeginScene / EndScene).
    std::uint32_t frameTime_ = 0;              // the last frame's
    std::uint32_t frameStart_ = 0;             // timeGetTime at this frame's BeginScene
    std::uint32_t lastFrameStart_ = 0;         // the previous frame's (the creation: its time)

private:
    // The creation's first step: a new transform pipeline.
    void CreatePipeline(const options::GameOptions* settings);
    // Drops the sun (telling the transform object) and the pipeline.
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

class CMappedResource;

// An archive of CLinkFigureLoader's: its path and the open archive.
struct LinkArchive {
    std::string name;
    CMappedResource* resource = nullptr;   // a CMMIOResource (the loader's destructor deletes it)
};

// A figure's cached model: the mesh, bones and animation archives of "<name>.mod / .bon / .anm" (null when
// missing), and the animations read from the last by name. CFigureManager::models_ and the link figure
// loader's figure sets cache them by name.
struct FigureModel {
    // An empty animation map, then the three archives as new CMMIOResources.
    FigureModel();
    // Every cached animation deleted, the map cleared, each archive closed (CloseResource) and deleted.
    ~FigureModel();
    FigureModel(const FigureModel&) = delete;
    FigureModel& operator=(const FigureModel&) = delete;
    CMappedResource* FindAnimation(const std::string* name);

    CMappedResource* mesh;          // the .mod archive
    CMappedResource* bones;
    CMappedResource* animations;
    std::unordered_map<std::string, CMappedResource*> animationCache;    // each a CMMIOResource over the animation's data

    // The link figure loader's sets open the three archives from the images of the figure's .mod / .bon /
    // .anm files (OpenFromMemory); a null image leaves its archive alone.
    void Open(const void* mod, const void* bon, const void* anm);
};

// Loads .lnk link files and the figures they name (created at startup with figures.res and menus.res). A
// figure is a CLinkMesh of parts morphed by three coefficients (s, t, u); the loader keeps every mesh it made
// in a multimap by name (a counted reference each) and hands out a cached one whose coefficients are within
// 1e-5 (squared distance) unless asked for a fresh one. Parts come from the figure's own archive set when
// there is one, else from the open archives as "<figure><part>.fig".
class CLinkMesh;
class ITransformPipeline;

class CLinkFigureLoader final {
public:
    // The figure and menu archives of `folder` opened (a failure is only logged).
    CLinkFigureLoader(CTextureDevice* renderer, const std::string* folder);
    virtual ~CLinkFigureLoader();
    virtual int CacheSize();
    virtual int CacheBytes();
    // Figure `name` with the parts `parts`: unless `fresh`, the first cached mesh of that name with close
    // coefficients, given the parts it lacks; otherwise a new one (MakeFigure). No reference is added for the
    // caller.
    virtual CLinkMesh* LoadFigure(const std::string* name, const std::vector<std::string>* parts, float s, float t,
                                  float u, bool fresh);
    // The same for a .lnk figure: a cached one, or LoadLinkFile.
    virtual CLinkMesh* LoadLink(const std::string* name, float s, float t, float u, bool fresh);
    virtual void ClearCache();
    // `mesh` with new coefficients: a mesh others also hold (more than two references) is looked up again by
    // its name; otherwise its parts are morphed in place (SetMorph, then per part). Null stays null.
    virtual CLinkMesh* Remorph(CLinkMesh* mesh, float s, float t, float u);

    void AddRef() { ++refCount_; }
    // Drops a reference; the last one deletes the loader.
    void Release() {
        if (--refCount_ == 0) delete this;
    }

    CLinkFigureLoader(const CLinkFigureLoader&) = delete;
    CLinkFigureLoader& operator=(const CLinkFigureLoader&) = delete;

    // Opens `path` as a memory-mapped archive and appends it to the list, unless an archive of that exact
    // name is already open. Returns 1 on success or when already open.
    int AddArchive(const std::string* path);
    // Reads a FIG / FIG8 part record at *cursor (moved past it) into `mesh` as part `name`: its header (two
    // dwords, bounds fitted to the vertices morphed by s, t, u) and its elements (CLinkMesh::AddPart).
    void LoadFigPart(CLinkMesh* mesh, const std::string* name, const char** cursor, float s, float t, float u);
    // The same for the older record without a FIG header (6 morph corners).
    void LoadOldPart(CLinkMesh* mesh, const std::string* name, const char** cursor, float s, float t, float u);

    // A new mesh from .lnk file `name` (from the figure set, else the archives' "<name>.lnk"): a count, then
    // per part its name and a second, unused name (an empty name keeps the previous one). Cached (one
    // reference). No link file: FatalError's result (0) is returned and the new mesh leaked.
    CLinkMesh* LoadLinkFile(const std::string* name, float s, float t, float u);
    // A new mesh `name` with the parts `parts`, loaded as LoadLinkFile does (a part missing from the figure
    // set is skipped); cached.
    CLinkMesh* MakeFigure(const std::string* name, const std::vector<std::string>* parts, float s, float t, float u);
    // Part `name` of `mesh` (its own coefficients) unless it has it: from its figure set (false when the set
    // lacks it) or the archives (LoadArchivePart's result).
    bool LoadPartInto(CLinkMesh* mesh, const std::string* name);
    // Part `name` of `mesh` from the first open archive that has "<mesh name><name>.fig".
    int LoadArchivePart(CLinkMesh* mesh, const std::string* name, float s, float t, float u);
    // Figure `name`'s own archive set: the cached one, else (when an open archive has "<name>.mod") a new set
    // stored under the name and loaded from the first archives with "<name>.mod" / ".bon" / ".anm"; null
    // without a .mod.
    FigureModel* FindFigureSet(const std::string* name);
    // Part `name` of `mesh` morphed again by s, t, u from its record (the figure set's model, else
    // "<mesh name><name>.fig" in the archives): false without one.
    bool RemorphPart(CLinkMesh* mesh, FigureModel* set, const std::string* name, float s, float t, float u);
    // The record at *cursor (moved past the vertices) morphed into the part's vertex packets and its header's
    // bounds refitted. The FIG one reads every record as FIG8 (header, bounds and vertices).
    void RemorphFigPart(CLinkMesh* mesh, const std::string* name, const char** cursor, float s, float t, float u);
    void RemorphOldPart(CLinkMesh* mesh, const std::string* name, const char** cursor, float s, float t, float u);

    int refCount_ = 0;
    ITransformPipeline* pipeline_;    // the renderer's pipeline's transform object
    std::list<LinkArchive> archives_;
    std::unordered_map<std::string, FigureModel*> figureSets_;    // by figure name
    Ref<CTextureDevice> renderer_;    // the device object; meshes take their pipeline from it
    std::multimap<std::string, CLinkMesh*> cache_;    // each entry holds a reference
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
    // The static initialiser's work. The three tables empty.
    PrintsDatabase* Construct();
    // The exit function's work: the tables from the last.
    void Destruct();

    std::vector<PrintRecord> blood;
    std::unordered_map<std::string, PrintRecord*> bloodByName;
    std::vector<PrintRecord2> footprints;
    std::unordered_map<std::string, PrintRecord2*> footprintsByName;
    std::vector<PrintRecord> scorch;
    std::unordered_map<std::string, PrintRecord*> scorchByName;
};
extern lib::StaticObject<PrintsDatabase> g_printsDatabase;

// A length-prefixed name (a four-byte length, then the characters) at *cursor into `out` (emptied for length
// 0), the cursor moved past it.
void ReadPrefixedName(std::string* out, const std::uint8_t** cursor);
