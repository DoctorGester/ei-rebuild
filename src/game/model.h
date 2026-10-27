#pragma once

// Model and resource loading: the 3D library side of figures and the
// databases read at startup (prints.db, acks.db). See docs/notes/models.md.
// Names are provisional.

#include "chunk_archive.h"
#include "maths.h"
#include "renderer_base.h"
#include "script.h"   // ScriptStream
#include "water_tweens.h"

#include "lib/containers.h"
#include "lib/stlp.h"
#include "lib/static_object.h"
#include "game_data.h"

#include <cstddef>
#include <cstdint>

// ---- 3D library ---------------------------------------------------------------------------------

// Matrix4 (row-major 4x4, the transform library's) is in src/lib/vecmath.h.

// Bounding volume of a figure part, tested against the view frustum. 0x2C bytes.
struct ClipBounds {
    std::uint32_t flags;   // +0x00 bit 0: centre/radius valid, bit 1: box valid
    Vector3 center;        // +0x04
    float radius;          // +0x10 half the side of the cube around the centre
    Vector3 boxMin;        // +0x14 box corners, relative to the centre
    Vector3 boxMax;        // +0x20

    // Sets the box and its flag; returns the new flags (left in EAX).
    std::uint32_t SetBox(const float* min, const float* max);
};
static_assert(sizeof(ClipBounds) == 0x2C);

// One of the transform pipeline's matrix stacks: aligned storage of matrices and the depth.
struct MatrixStack {
    // Room for 16 matrices, empty (the pipeline's constructor runs it for each
    // of the 16).
    MatrixStack();
    // The block freed.
    ~MatrixStack();
    MatrixStack(const MatrixStack&) = delete;
    MatrixStack& operator=(const MatrixStack&) = delete;

    Matrix4* matrices;      // +0x00
    int count;              // +0x04 the top is matrices[count - 1]
};
static_assert(sizeof(MatrixStack) == 8);

struct VertexPacket;   // figure_mesh.h
struct TLVertex;       // clip.h
class CLight;          // spell.h

// The transform object of 3DfpFPU.dll (GetTLPipeline; CTransformPipeline::transform_), as far as
// the code calls it.
class ITransformPipeline {
public:
    virtual void Unknown0();
    // The viewport's size in pixels.
    virtual void SetViewportSize(float width, float height);
    // The sun for the next LightMesh (its localDirection_).
    virtual void AddSun(CLight* sun);
    // The ambient colour, components in 0..1.
    virtual void SetAmbient(float r, float g, float b);
    // The projection matrix, transposed.
    virtual void SetProjection(float* transposed);
    // The depth range (the options' fog start and far clip distances).
    virtual void SetDepthRange(float first, float second);
    virtual void Unknown6();
    // 7: fits `bounds` around `count` vertex packets.
    virtual void FitBounds(ClipBounds* bounds, const VertexPacket* vertices, int count);
    // `count` points (16 bytes each: x, y, z and one more) projected with `matrix` into
    // `projected` (x, y, z, w), a flag byte per point set when it is off screen; 2 when every point
    // is, else 0 or 1 (the ribbon draw).
    virtual int ProjectPointsClipped(const Matrix4* matrix, const void* points, void* projected, std::uint8_t* flags,
                                     int count);
    // 9: projects `blocks` blocks of four positions with `matrix` into `projected`, writing their
    // clip outcodes; the result is the caller's (on screen / crossing / outside).
    virtual int ProjectClipped(const Matrix4* matrix, const void* positions, void* projected, void* outcodes,
                               int blocks);
    virtual void Unknown10();
    virtual void Unknown11();
    virtual void Unknown12();
    // 13: the same without clipping (the ribbon draw's mode 0).
    virtual void ProjectPoints(const Matrix4* matrix, const void* points, void* projected, int count);
    // The same without clipping (every block on screen); the result as the clipping one's (the device's
    // mesh draws test it, CTransformPipeline::ProjectBlocks does not).
    virtual int Project(const Matrix4* matrix, const void* positions, void* projected, int blocks);
    virtual void Unknown15();
    virtual void Unknown16();
    virtual void Unknown17();
    // 18 / 19: the same for the terrain's vertices.
    virtual int ProjectTerrainClipped(const Matrix4* matrix, const void* vertices, void* projected, void* outcodes,
                                      int count);
    virtual void ProjectTerrain(const Matrix4* matrix, const void* vertices, void* projected, int count);
    virtual void Unknown20();
    virtual void Unknown21();
    virtual void Unknown22();
    virtual void Unknown23();
    // `count` screen-facing sprites (`points`: centre x, y, z and size each) placed with the
    // transposed view matrix as four TL vertices each into `corners` (positions only), with an
    // outcode byte per corner: 0 all on screen, 1 some off screen, else none (the sprite draw).
    virtual int ProjectSprites(const void* transposedView, const void* points, TLVertex* corners,
                               std::uint8_t* outcodes, int count);
    virtual void Unknown25();
    // The `blocks` packets of four positions times the (transposed) matrix, as points (x, y, z, w)
    // into `out` (the decal projector).
    virtual void TransformPoints(const Matrix4* transposed, const VertexPacket* vertices, Vector4* out, int blocks);
    virtual void Unknown27();
    virtual void Unknown28();
    // The `blocks` packets projected with `matrix` into `points` (x, y per vertex), `bounds`
    // (min x, min y, max x, max y) grown around them (a shadow's points).
    virtual void ProjectFlat(const float* matrix, const VertexPacket* vertices, Vector2* points, int blocks,
                             float* bounds);
    virtual void Unknown30();
    virtual void Unknown31();
    // 32: `count` projected vertices (x, y, z, w in the first 16 bytes) to the screen: 1 / w kept as
    // rhw, x and y scaled to the viewport, z divided (the rain and snow draws).
    virtual void ToScreen(TLVertex* vertices, int count);
    virtual void Unknown33();
    // 34: the vertices the clipper (37) left, projected to the screen as ToScreen does (the mesh,
    // flat and strip draws after clipping).
    virtual void ToScreenClipped(TLVertex* vertices, int count);
    // 35: the distance fog of `count` screen vertices: the specular's alpha lowered to the fog
    // factor of the depth (the device object's draws when its distanceFog_ is set).
    virtual void FogVertices(TLVertex* vertices, int count);
    virtual void Unknown36();
    // 37: clips the triangles `indices` (`*indexCount` indices) of the `*vertexCount` vertices with
    // their `outcodes` against the screen: the vertices it adds are appended (the callers make room
    // for ten times the input), the triangles left written to `clippedIndices`, both counts updated.
    virtual int ClipTriangles(TLVertex* vertices, std::uint8_t* outcodes, const std::uint16_t* indices,
                              std::uint16_t* clippedIndices, int* vertexCount, int* indexCount);
    virtual void Unknown38();
    virtual void Unknown39();
    // 40: lights a figure mesh: its vertex packets and normals, the colours written (one
    // per normal), its words, the lights (their localPosition_, colours set for the material) and
    // the material's light vector.
    virtual void LightMesh(const void* vertices, const void* normals, std::uint32_t* colours, const void* words,
                           int wordCount, CLight** lights, int lightCount, const std::uint32_t* vector);
};


// The transform pipeline of the 3D library (the scene renderer's +8, the device object's +0xAC).
// Only the matrix stacks and the transform object are known.
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
    // Pushes `m` onto stack `index`: an empty stack gets `m`
    // itself, otherwise the new top is the old top times `m` (no capacity check).
    void PushMatrix(const float* m, int index);
    // Projects `blocks` blocks of four positions: `bounds` (null: assumed
    // crossing) classified by ClipTest; on screen (0): the transform object's Project with the top of
    // stack 1, result 0; crossing (1): its ProjectClipped with the top of stack 2, its result;
    // anything else: 2, nothing projected.
    int ProjectBlocks(const void* positions, void* projected, void* outcodes, int blocks, const ClipBounds* bounds);
    // The same for terrain vertices, both with stack 2.
    int ProjectTerrain(const void* vertices, void* projected, void* outcodes, int count, const ClipBounds* bounds);
    // Constructs the pipeline (the renderer base's creation news it with the game's
    // work folder): the 16 matrix stacks, the processor's CPUID function 1 into the globals, then
    // 3DfpFPU.dll loaded from `folder` and its GetTLPipeline()'s transform object taken. No library, or
    // no such function in it: throws.
    explicit CTransformPipeline(const char* folder);
    // The destructor: the transform object forgotten, 3DfpFPU.dll
    // unloaded, the stacks' blocks freed from the last to the first.
    ~CTransformPipeline() override;
    // The push the draw code inlines (the terrain, the world draw
    // and the decal projector keep their own copies): as PushMatrix, but
    // the new top is computed by Matrix4::Multiply, whose elements sum in another order.
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
    // The top of stack `index` (matrices[count - 1], computed
    // even when empty).
    const Matrix4* Top(int index) const { return stacks_[index].matrices + (stacks_[index].count - 1); }
    // Drops the top of stack `index` unless it is empty.
    void PopMatrix(int index) {
        if (stacks_[index].count != 0) --stacks_[index].count;
    }

    // Counted (CRendererBase): holders drop a reference, and the last one deletes it.

    std::uint8_t unused_08[4] = {};
    ITransformPipeline* transform_;   // +0x0C 3DfpFPU.dll's transform object (projects and clips vertices)
    void* library_;                   // +0x10 3DfpFPU.dll's module (FreeLibrary in the destructor)
    MatrixStack stacks_[16];    // +0x14 (models.md: stacks 0-2, 5-10 and 14 are in use)
};
static_assert(offsetof(CTransformPipeline, stacks_) == 0x14 && offsetof(CTransformPipeline, transform_) == 0x0C);
static_assert(sizeof(CTransformPipeline) == 0x94);

// The scene renderer's pipeline (world_singletons.cpp): stacks 1 and 2 take the scene matrices, 5 the world
// draw's, 9 the views.
CTransformPipeline* ScenePipeline();

struct tagRECT;
struct ScreenPoints;   // renderer_lines.h
class CTextureDevice;
namespace options {
class GameOptions;
}
class CLight;   // spell.h
class CTexture;   // texture.h
class CPixelConverter;   // texture.h

// A render state preset the renderer's helper keeps (counted; over an abstract base
// with one pure virtual and the plain counted root, neither with fields; render_helper.cpp).
class CRenderPreset final {
public:
    // The helper's recording: no references, a copy of `blocks`.
    CRenderPreset(const stlp::vector<std::uint32_t>& blocks, int stages, int index);
    // The blocks' storage freed.
    virtual ~CRenderPreset();
    virtual unsigned Index() { return static_cast<unsigned>(index_); }

    // On the game's heap.
    static void* operator new(std::size_t size) noexcept;
    static void operator delete(void* block);

    CRenderPreset(const CRenderPreset&) = delete;
    CRenderPreset& operator=(const CRenderPreset&) = delete;

    int refs_ = 0;                       // +0x04
    // +0x08 Direct3D state block handles: the texture stages' first, then one for each state from
    // `stages_` on (its blend factors). The device's draws apply them (device_draw.cpp).
    stlp::vector<std::uint32_t> blocks_;
    int stages_;                         // +0x14 texture stages the first block sets
    int index_;                          // +0x18 the index it was recorded with (below 9: built in)
};
static_assert(offsetof(CRenderPreset, blocks_) == 0x08 && sizeof(CRenderPreset) == 0x1C);

// What the helper keeps for a preset: the states it was recorded from (copied out by virtual 6) and
// the preset itself, counted.
struct RenderPresetRecord {
    // One texture stage's operations and the blend factors of its pass. Direct3D's values
    // (D3DTEXTUREOP, D3DTA_*, D3DBLEND).
    struct State {
        bool enabled;               // +0x00 the stage's operations are set
        std::uint32_t colourOp;     // +0x04
        std::uint32_t colourArg1;   // +0x08
        std::uint32_t colourArg2;   // +0x0C
        std::uint32_t alphaOp;      // +0x10
        std::uint32_t alphaArg1;    // +0x14
        std::uint32_t alphaArg2;    // +0x18
        bool blend;                 // +0x1C the blend factors are set
        std::uint32_t sourceBlend;  // +0x20
        std::uint32_t destBlend;    // +0x24
    };
    stlp::vector<State> states;   // +0x00
    CRenderPreset* preset;        // +0x0C counted
};
static_assert(sizeof(RenderPresetRecord) == 0x10 && sizeof(RenderPresetRecord::State) == 0x28);
static_assert(offsetof(RenderPresetRecord::State, blend) == 0x1C);

struct IDirect3DDevice7;

// The renderer's counted helper object (0x118 bytes; over an abstract base with
// seven pure virtuals and the plain counted root, neither with fields). render_helper.cpp has all of it.
class CRenderHelper final {
public:
    static constexpr unsigned kPresetCount = 9;

    // Takes the device and its capabilities, then records the nine built-in presets (`flag`'s low
    // byte: one texture stage each).
    CRenderHelper(IDirect3DDevice7* device, std::uint32_t flag);
    // Every record's preset through virtual 3, the tables freed, the device released.
    virtual ~CRenderHelper();
    // Records the state blocks of `states` and keeps the new preset (as
    // built-in preset `index` when that is below 9). `singleStage`: at most one texture stage.
    virtual CRenderPreset* Record(const stlp::vector<RenderPresetRecord::State>* states, int index, bool singleStage);
    // The record of `preset`, if there is one: virtual 3 with its preset.
    virtual void ReleasePreset(CRenderPreset* preset);
    // Deletes the preset's state blocks and drops its record.
    virtual void ReleaseBlock(CRenderPreset* preset);
    // The preset of `preset`'s record, or null without a record.
    virtual void* PresetBlock(CRenderPreset* preset);
    // Preset `index` of the nine at +0x10C, or null beyond them.
    virtual CRenderPreset* Preset(unsigned index);
    // The states of `preset`'s record copied into `out`; false without a record.
    virtual bool PresetStates(CRenderPreset* preset, stlp::vector<RenderPresetRecord::State>* out);
    // Virtual 6 for preset `index`; false beyond the nine or for a null one.
    virtual bool BuiltInStates(unsigned index, stlp::vector<RenderPresetRecord::State>* out);

    // On the game's heap.
    static void* operator new(std::size_t size) noexcept;
    static void operator delete(void* block);

    CRenderHelper(const CRenderHelper&) = delete;
    CRenderHelper& operator=(const CRenderHelper&) = delete;

    // The state list of built-in preset `index` into
    // `states` (emptied first); false beyond the nine. Presets 0 and 8 have no states.
    bool RenderHelperBuiltInStateList(stlp::vector<RenderPresetRecord::State>* states, unsigned index);

    int refs_ = 0;   // +0x04
    IDirect3DDevice7* device_;          // +0x08 counted (AddRef / Release)
    std::uint8_t caps_[0xEC];           // +0x0C the device's D3DDEVICEDESC7
    stlp::hash_map<CRenderPreset*, RenderPresetRecord, stlp::PointerHash> records_;   // +0xF8 by preset
    stlp::vector<CRenderPreset*> presets_;   // +0x10C the nine built-in presets (not counted)
};
static_assert(offsetof(CRenderHelper, refs_) == 0x04 && sizeof(CRenderHelper) == 0x118);
static_assert(offsetof(CRenderHelper, caps_) == 0x0C);
static_assert(offsetof(CRenderHelper, records_) == 0xF8 && offsetof(CRenderHelper, presets_) == 0x10C);

// The scene renderer's base class ("CVisualizator" in the game's messages, 25 virtuals):
// the destructor, CreateBase, DestroyBase and SetBaseColour are its own, the rest pure (the
// compiler CRT's `_purecall`). It owns +0x04 .. +0x1C. Nothing has it as its final class:
// only CSceneRenderer's construction and destruction pass through it.
class CVisualizator {
public:
    // Sets the device's fog colour render state (D3DRENDERSTATE_FOGCOLOR) to `colour`.
    virtual int SetFogColourState(std::uint32_t colour) = 0;
    // The scalar deleting destructor (DestroyBase, then operator delete when bit 0 of the flags is set;
    // returns this).
    virtual ~CVisualizator();
    // The creation (CreateBase).
    virtual void CreateRenderer(void* window, options::GameOptions* settings);
    // DestroyBase.
    virtual void DestroyRenderer();
    virtual int BeginScene() = 0;
    virtual int EndScene() = 0;
    virtual std::uint32_t FrameTime() = 0;
    // SetBaseColour (the renderer's version also tints the device object). Sets the draw colour.
    virtual void SetColour(std::uint32_t colour);
    virtual int ClearFrame() = 0;
    virtual std::uint32_t ZEnable() = 0;
    virtual int SetBlend(std::uint32_t source, std::uint32_t dest) = 0;
    virtual int SetTexture(int stage, void* texture) = 0;
    virtual int DrawTriangles(const void* vertices, int vertexCount, const std::uint16_t* indices,
                              unsigned indexCount) = 0;
    virtual int DrawFigureLit(void* figure, const void* lights, void* material, int alpha, tagRECT* screen,
                              CTexture* textureA, CTexture* textureB) = 0;
    virtual int DrawFigure(void* figure, const void* unknownA, const void* unknownB, void* material, int alpha,
                           tagRECT* screen) = 0;
    virtual void DrawLineList(void* lines) = 0;
    virtual int DrawStrip(void* points, float width, int visibility, float length, float offset,
                          float slant) = 0;
    virtual int DrawSprites(int unknown, void* points, void* sprites) = 0;
    virtual int DrawSelectionMarker(const Vector3* point, float size, const Vector3* axisX,
                                    const Vector3* axisY) = 0;
    virtual int DrawRectangle(int left, int top, int right, int bottom, int alpha, int outline) = 0;
    virtual int Polyline(const ScreenPoints* points, std::uint32_t colour) = 0;
    virtual int DrawTexture(CTexture* texture, float x, float y, float scale) = 0;
    virtual bool SetGamma(float brightness, float contrast, float gamma) = 0;
    virtual void GetGamma(float* brightness, float* contrast, float* gamma) const = 0;
    virtual bool RestoreSurfaces() = 0;

    // Freed into the game's heap (the scalar deleting destructor's operator delete).
    static void operator delete(void* memory);

    // The creation (called first by the renderer's creation). A new transform pipeline (with the options' work path) replaces the held
    // one; CPUID function 1 goes to the globals and a processor without MMX is fatal
    // ("MMX required in this version !!!."); then <work path>2DintMMX.dll is loaded and its
    // GetGraphicsProcessor() kept (fatal when either is missing). `window` is not read. Returns the
    // graphics processor.
    void* CreateBase(void* window, options::GameOptions* settings);
    // The base constructor's work: flags_, pipeline_ and sun_ cleared. Returns this.
    CVisualizator* SetUpBase();
    // DestroyBase. The sun dropped (set to null, then released) and the transform object
    // told (AddSun with null), the pipeline dropped, the graphics library (2DintMMX.dll) freed and
    // its processor forgotten.
    void DestroyBase();
    // SetBaseColour. colour_ = `colour`; the transform object's ambient
    // becomes its red, green and blue bytes times 1 / 255.
    void SetBaseColour(std::uint32_t colour);
    // The sun becomes `light` (counted; the old one released) and is handed to the
    // transform object (AddSun) when there is one.
    void SetSun(CLight* light);
    // SetSun, then the colour from r, g, b in 0..1, packed as
    // SetFogColour packs them.
    void SetSunAndAmbient(CLight* light, float r, float g, float b);

    std::uint32_t flags_;            // +0x04 bit 0: the software cursor option; bit 1 set by the creation
    CTransformPipeline* pipeline_;   // +0x08 counted
    CPixelConverter* graphicsProcessor_;   // +0x0C 2DintMMX.dll's GetGraphicsProcessor()
    void* graphicsLibrary_;          // +0x10 the HMODULE of 2DintMMX.dll
    std::uint32_t fogColour_;        // +0x14 also the colour the frame is cleared with
    std::uint32_t colour_;           // +0x18 the draw colour
    // +0x1C the sun (counted; set together with the ambient colour, the client's lighting
    // moves it every frame).
    CLight* sun_;

protected:
    CVisualizator();
};

// The ambient colour (r, g, b in 0..1) SetBaseColour last gave the transform object: 3DfpFPU's object
// is one for every renderer, and its LightMesh lit the figure parts with it; the parts lit on the GPU
// take it from here (figure_part_draw.cpp).
const float* TransformAmbientColour();

// The scene renderer (the global renderer object, 26 virtuals). A real virtual class over
// CVisualizator; the static initialiser's constructor
// and the exit's destructor construct and destroy it in place.
// A counted texture pointer as the helpers take it: by address, on the pointer
// (the particle sources', the rain's, the sky's and the lightning's textures). Reads see the texture;
// a plain store (`held = p`) counts nothing.
struct HeldTexture {
    // Drops the texture (set to null, then released) and
    // takes the texture manager's registered or cached texture of `kind` named `name`, or a new one of
    // `kind` (CreateObject(kind, -1)), named `name` (one more reference).
    void Load(int kind, const char* name);

    operator CTexture*() const { return texture; }
    CTexture* operator->() const { return texture; }
    HeldTexture& operator=(CTexture* plain) {
        texture = plain;
        return *this;
    }

    CTexture* texture;
};
static_assert(sizeof(HeldTexture) == 4);

// The device object's counted holder (CSceneRenderer::device_): the release leaves
// the pointer null, the assignment takes `device` (one more reference) and releases
// the old one. Reads see the device; a plain store (`held = p`) counts nothing.
struct HeldDevice {
    void Assign(CTextureDevice* value);

    operator CTextureDevice*() const { return device; }
    CTextureDevice* operator->() const { return device; }
    HeldDevice& operator=(CTextureDevice* plain) {
        device = plain;
        return *this;
    }

    CTextureDevice* device;
};
static_assert(sizeof(HeldDevice) == 4);

class CSceneRenderer : public CVisualizator {
public:
    int SetFogColourState(std::uint32_t colour) override;
    // The device object dropped (null, then released), the helper
    // released (left set), then the base's destructor.
    ~CSceneRenderer() override;
    void CreateRenderer(void* window, options::GameOptions* settings) override;
    // DestroyBase of the base part, flags_ bit 1 cleared, the
    // helper and the device object dropped (each set to null, then released).
    void DestroyRenderer() override;
    // 0 unless flags_ bit 0 is set. The frame statistics:
    // this frame's time since the last EndScene, the shortest (nonzero) and longest (under 1000 ms),
    // the total and count, and once a second's worth has passed the average over it; then ClearFrame
    // (the batch clear), the device's counters cleared and IDirect3DDevice7::BeginScene. When a scene is
    // still open (D3DERR_SCENE_IN_SCENE) the batches are flushed, the scene ended and begun again;
    // other failures are fatal ("Can't begin scene." / "Can't end scene."). Returns 1.
    int BeginScene() override;
    // Flushes the device's two batches and ends the scene
    // (fatal on failure other than D3DERR_SCENE_NOT_IN_SCENE), then sets the debug panel's "FPS"
    // (1000 / frame time, "%g"), "num verts" and "num trias" ("%d") and remembers the frame's start.
    // Returns 1.
    int EndScene() override;
    // The last frame's time in ms (frameTime_).
    std::uint32_t FrameTime() override { return frameTime_; }
    // Sets the colour the next draws are tinted with (kept at +0x18 and in the
    // device object's colour_), the transform object's ambient from it (SetBaseColour).
    void SetColour(std::uint32_t colour) override;
    // Clears the render target and the depth (and the stencil when the device
    // has one) to the fog colour and 1.
    int ClearFrame() override;
    // The device object's z-buffer setting (CTextureDevice::zEnable_).
    std::uint32_t ZEnable() override;
    // Sets SRCBLEND, then DESTBLEND.
    int SetBlend(std::uint32_t source, std::uint32_t dest) override;
    // Binds `texture` (a texture object, or null) to stage `stage`.
    int SetTexture(int stage, void* texture) override;
    // Draws pre-transformed vertices (D3DTLVERTEX,
    // FVF 0x1C4) as an indexed triangle list with the current state. Returns 1.
    int DrawTriangles(const void* vertices, int vertexCount, const std::uint16_t* indices, unsigned indexCount) override;
    // Draws `figure` (a CFigureMesh) through its drawer's lit draw
    // with `lights`, the drawing context `material`, `alpha`, `screen` (or null) and two
    // textures, unless it is hidden (flags +8 bit 0x80). Returns 1.
    int DrawFigureLit(void* figure, const void* lights, void* material, int alpha, tagRECT* screen, CTexture* textureA,
                      CTexture* textureB) override;
    // Draws `figure` through its drawer (+0x94) unless it is
    // hidden (flags +8 bit 0x80); `alpha` reaches the mesh draw, `screen` (or null) gets the
    // rectangle drawn, `material` (null, or an interface sprite's material) is passed on. The second
    // and third arguments are not read there.
    int DrawFigure(void* figure, const void* unknownA, const void* unknownB, void* material, int alpha,
                   tagRECT* screen) override;
    // Draws a line list with the pipeline and the device object.
    void DrawLineList(void* lines) override;
    // Draws a strip along `points` (a lightning segment's aligned points) of
    // `width` with the bound texture: `visibility` the bounds' clip result, the texture's `length`
    // and `offset` along it (0 and 0: untextured lengthwise), `slant` 0.25 at every call known.
    int DrawStrip(void* points, float width, int visibility, float length, float offset, float slant) override;
    // Draws sprites facing the view with the bound texture: `points` an aligned array
    // of centres and sizes (x, y, z, size), `sprites` a CArray of their colours and texture
    // rectangles. The first argument is 0 at every call known.
    int DrawSprites(int unknown, void* points, void* sprites) override;
    // The selection marker under a selected
    // unit (the draw pass): a flat green arrow of half-transparent 0x8000FF00, 0.3 above
    // `point`, `size` long, pointing along -`axisY` (`axisX` across), drawn by the device's flat
    // geometry draw as six triangles of two vertex blocks.
    int DrawSelectionMarker(const Vector3* point, float size, const Vector3* axisX, const Vector3* axisY) override;
    // A black rectangle (left, top, right, bottom in
    // screen pixels) of `alpha` over the current blend state, with `outline` a white frame. Returns 1.
    int DrawRectangle(int left, int top, int right, int bottom, int alpha, int outline) override;
    // A screen polyline in `colour`.
    int Polyline(const ScreenPoints* points, std::uint32_t colour) override;
    // Draws `texture` at (x, y) scaled by `scale` as a
    // pre-transformed strip of four vertices (z 0.9, white, specular 0xFF000000) through stage 0
    // 0 without a texture, else 1.
    int DrawTexture(CTexture* texture, float x, float y, float scale) override;
    // Sets the options' brightness,
    // contrast and gamma, at start-up, from the options screen and with zeros at exit. Without a
    // device or a primary-surface gamma ramp (DDCAPS2_PRIMARYGAMMA) false. Keeps the three values,
    // then sets the primary surface's gamma ramp (calibrated when the driver can): each value
    // clamped to -1..1 and halved; level i / 255 raised to 1 / (1 + 5 g) (g > 0) or 1 / (1 + g / 2)
    // (g < 0), times the contrast's scale (1 + 4 |c|, inverted for c < 0) and centred, clamped to
    // 0..1, plus the brightness, clamped again, times 65280 (truncated) for all three channels.
    // True when SetGammaRamp succeeds.
    bool SetGamma(float brightness, float contrast, float gamma) override;
    // The display settings SetGamma last kept.
    void GetGamma(float* brightness, float* contrast, float* gamma) const override;
    // False without
    // a device object (called by WM_ACTIVATE after a switch back). A full-screen device gets its cooperative level (with its screen
    // rectangle) and display mode (the render target's size and depth) back; then
    // IDirectDraw7::RestoreAllSurfaces under the device lock, the primary, render target and depth
    // surfaces restored, and the texture manager's restore, whose result is returned.
    bool RestoreSurfaces() override;
    // Nothing calls it. The pipeline's
    // matrix stacks emptied and filled again for `screen` and a field of view (0: the camera's), as
    // the creation fills them, then the transform object's projection and viewport size.
    virtual void SetView(const tagRECT* screen, float fieldOfView);

    // Makes sure the texture in `holder` is loaded
    // and binds it to `stage`.
    int BindHeldTexture(int stage, HeldTexture* holder);
    // The fog (and clear) colour from components in
    // 0..1: each times 255 as a float, rounded to the nearest integer (FISTP); A = 0xFF, packed as
    // ((r | 0xFFFFFF00) << 8 | g) << 8 | b; stored at +0x14 and set through the first virtual.
    void SetFogColour(float r, float g, float b);

    HeldDevice device_;              // +0x20 the device object, a counted holder
    CRenderHelper* helper_;          // +0x24 counted
    // +0x28 .. +0x30: the display settings SetGamma last applied, as given.
    float brightness_;
    float contrast_;
    float gamma_;
    // +0x34 .. +0x58: the frame statistics (milliseconds; BeginScene / EndScene), set by the
    // creation to 0, 0x80000000, zeros, the time, zeros.
    std::uint32_t frameTime_;        // +0x34 the last frame's
    std::uint32_t shortestFrame_;    // +0x38
    std::uint32_t longestFrame_;     // +0x3C (under 1000)
    std::uint32_t averageFrame_;     // +0x40 over the last second's worth of frames
    std::uint32_t totalTime_;        // +0x44
    std::uint32_t frameCount_;       // +0x48
    std::uint32_t frameStart_;       // +0x4C timeGetTime at this frame's BeginScene
    std::uint32_t lastFrameStart_;   // +0x50 the previous frame's (the creation: its time)
    std::uint32_t windowTime_;       // +0x54 the time and frames toward the average
    std::uint32_t windowFrames_;     // +0x58

    CSceneRenderer();
};
static_assert(offsetof(CSceneRenderer, pipeline_) == 0x08 && offsetof(CSceneRenderer, fogColour_) == 0x14 &&
              offsetof(CSceneRenderer, colour_) == 0x18 && offsetof(CSceneRenderer, device_) == 0x20);
static_assert(offsetof(CSceneRenderer, helper_) == 0x24 && offsetof(CSceneRenderer, frameTime_) == 0x34 &&
              offsetof(CSceneRenderer, lastFrameStart_) == 0x50 && offsetof(CSceneRenderer, windowFrames_) == 0x58);

// The renderer (the global renderer object).
CSceneRenderer* __cdecl SceneRenderer();

// Viewport matrix for a screen rectangle {left, top, right, bottom}: scale and offset by
// the half extents, y flipped. Returns out.
Matrix4* __stdcall ViewportMatrix(Matrix4* out, const int* rect);
// Perspective projection; `fov` in radians (full angle). Returns out.
Matrix4* __stdcall ProjectionMatrix(Matrix4* out, float fov, float aspect, float zNear, float zFar);

// Triangle index buffer of a figure part: 3 uint16 indices per triangle, an engine aligned array
// (lib::AlignedArray; its SetSize with growBy -1 keeps the step).
// A triangle corner: position index, a second index (normal?) and texture coordinate index.
struct MeshCorner {
    std::uint16_t index[3];
};
static_assert(sizeof(MeshCorner) == 6);
using TriangleIndexArray = lib::AlignedArray<MeshCorner>;

class CMappedResource;

// A node of CLinkFigureLoader's archive list (std::list<pair<string, CMMIOResource*>>).
struct LinkArchiveNode {
    LinkArchiveNode* next;
    LinkArchiveNode* prev;
    stlp::string name;           // +0x08
    CMappedResource* resource;   // +0x14 a CMMIOResource
};
static_assert(sizeof(LinkArchiveNode) == 0x18);

// A figure's cached model (0x20 bytes): the mesh, bones
// and animation archives of "<name>.mod / .bon / .anm" (0x6C-byte CMMIOResource objects; null when
// missing), then the animations read from the last by name. The figure manager
// (CFigureManager::models_) and the link figure loader (its figure sets) cache
// them by name.
struct FigureModel {
    CMappedResource* FindAnimation(const stlp::string* name);
    using AnimationMap = stlp::hash_map<stlp::string, CMappedResource*, stlp::StringHash>;

    CMappedResource* mesh;          // +0x00 the .mod archive
    CMappedResource* bones;         // +0x04
    CMappedResource* animations;    // +0x08
    AnimationMap animationCache;    // +0x0C each a CMMIOResource over the animation's data

    // The link figure loader's sets open
    // the three archives from the images of the figure's .mod / .bon / .anm files (OpenFromMemory);
    // a null image leaves its archive alone.
    void Open(const void* mod, const void* bon, const void* anm);
};
static_assert(offsetof(FigureModel, animationCache) == 0x0C && sizeof(FigureModel) == 0x20);
// A figure model's constructor
// (returns it) and destructor.
FigureModel* __fastcall ConstructFigureModel(FigureModel* model);
void __fastcall DestroyFigureModel(FigureModel* model);

// Loads .lnk link files and the figures they name (over an abstract base
// with six pure virtuals and the plain counted root, neither with fields; created at startup with
// figures.res and menus.res, a Rebirth-patched function). A figure is a CLinkMesh of
// parts morphed by three coefficients (s, t, u); the loader keeps every mesh it made in a
// multimap by name (a counted reference each) and hands out a cached one whose coefficients are
// within 1e-5 (squared distance) unless asked for a fresh one. Parts come
// from the figure's own archive set when there is one, else from the open archives
// as "<figure><part>.fig". link_loader.cpp.
class CLinkMesh;
class CTextureDevice;
class ITransformPipeline;
struct VertexPacket;

class CLinkFigureLoader final {
public:
    // The renderer held, the cache and the figure and menu archives of
    // `folder` opened (a failure is only logged).
    CLinkFigureLoader(CTextureDevice* renderer, const stlp::string* folder);
    // Each archive closed and deleted, the archive list emptied, each
    // figure set destroyed and freed, the device object released (the field cleared first), the cache
    // emptied (ClearCache's walk), then the containers' storage.
    virtual ~CLinkFigureLoader();
    virtual int CacheSize();
    virtual int CacheBytes();
    // Figure `name` with the parts `parts`: unless `fresh`, the first cached
    // mesh of that name with close coefficients, given the parts it lacks; otherwise a new one
    // (MakeFigure). No reference is added for the caller.
    virtual CLinkMesh* LoadFigure(const stlp::string* name, const stlp::vector<stlp::string>* parts, float s, float t,
                                  float u, bool fresh);
    // The same for a .lnk figure: a cached one, or LoadLinkFile.
    virtual CLinkMesh* LoadLink(const stlp::string* name, float s, float t, float u, bool fresh);
    virtual void ClearCache();
    // `mesh` with new coefficients: a mesh others also hold (more than
    // two references) is looked up again by its name; otherwise its parts are
    // morphed in place (SetMorph, then per part). Null stays null.
    virtual CLinkMesh* Remorph(CLinkMesh* mesh, float s, float t, float u);

    // On the game's heap (link_loader_create.cpp).
    static void* operator new(std::size_t size) noexcept;
    static void operator delete(void* block);

    CLinkFigureLoader(const CLinkFigureLoader&) = delete;
    CLinkFigureLoader& operator=(const CLinkFigureLoader&) = delete;

    // Opens `path` as a memory-mapped archive and appends it to the list, unless an
    // archive of that exact name is already open. Returns 1 on success or when already open.
    int AddArchive(const stlp::string* path);
    // link_figure.cpp. Reads a FIG / FIG8 part record at *cursor (moved past
    // it) into `mesh` as part `name`: its header (two dwords, bounds fitted to the vertices morphed
    // by s, t, u) and its elements (CLinkMesh::AddPart).
    void LoadFigPart(CLinkMesh* mesh, const stlp::string* name, const char** cursor, float s, float t, float u);
    // The same for the older record without a FIG header (6 morph corners).
    void LoadOldPart(CLinkMesh* mesh, const stlp::string* name, const char** cursor, float s, float t, float u);

    // link_loader.cpp:
    // A new mesh from .lnk file `name` (from the figure set, else the
    // archives' "<name>.lnk"): a count, then per part its name and a second, unused name
    // (ReadLinkString; an empty name keeps the previous one); each part loaded from the figure
    // set or the archives. Cached (one reference). No link file: FatalError("Can't find link
    // file ..."), whose result (0) is returned and the new mesh leaked.
    CLinkMesh* LoadLinkFile(const stlp::string* name, float s, float t, float u);
    // A new mesh `name` with the parts `parts`, loaded as LoadLinkFile does
    // (a part missing from the figure set is skipped); cached.
    CLinkMesh* MakeFigure(const stlp::string* name, const stlp::vector<stlp::string>* parts, float s, float t, float u);
    // Part `name` of `mesh` (its own coefficients) unless it has it: from its
    // figure set (false when the set lacks it) or the archives (LoadArchivePart's result).
    bool LoadPartInto(CLinkMesh* mesh, const stlp::string* name);
    // Part `name` of `mesh` from the first open archive that has
    // "<mesh name><name>.fig".
    int LoadArchivePart(CLinkMesh* mesh, const stlp::string* name, float s, float t, float u);
    // Figure `name`'s own archive set: the cached one, else (when an open
    // archive has "<name>.mod") a new set stored under the name and loaded from the first
    // archives with "<name>.mod" / ".bon" / ".anm"; null without a .mod.
    FigureModel* FindFigureSet(const stlp::string* name);
    // Part `name` of `mesh` morphed again by s, t, u from its record
    // (the figure set's model, else "<mesh name><name>.fig" in the archives): false without one.
    bool RemorphPart(CLinkMesh* mesh, FigureModel* set, const stlp::string* name, float s, float t, float u);
    // link_figure.cpp. The record at *cursor (moved past the
    // vertices) morphed into the part's vertex packets (CLinkMesh::SetPartVertices) and its header's
    // bounds refitted. The FIG one reads every record as FIG8 (header, bounds and vertices).
    void RemorphFigPart(CLinkMesh* mesh, const stlp::string* name, const char** cursor, float s, float t, float u);
    void RemorphOldPart(CLinkMesh* mesh, const stlp::string* name, const char** cursor, float s, float t, float u);

    int refCount_ = 0;                // +0x04
    ITransformPipeline* pipeline_;    // +0x08 the renderer's (+0x24) +0xAC -> +0xC
    LinkArchiveNode* archives_;       // +0x0C list sentinel
    stlp::hash_map<stlp::string, FigureModel*, stlp::StringHash> figureSets_;   // +0x10 by figure name
    CTextureDevice* renderer_;        // +0x24 the device object, counted; meshes take their pipeline from it
    stlp::map<stlp::string, CLinkMesh*> cache_;   // +0x28 a multimap of counted references
};

// ---- Databases ---------------------------------------------------------------------------------

// What chunks 2 and 3 of both prints.db record types hold: a print's alpha and its life and lifetime
// in ticks (the client world's +0x7A8 picks chunk 3).
struct PrintLook {
    float alpha;
    int life;
    int lifetime;
};
static_assert(sizeof(PrintLook) == 0xC);

// A record of prints.db (terrain footprints). 0x28 bytes. The records live
// in the prints' vectors (record_vectors.h): the copies copy the name and the looks; the record resize(n)
// appends copies of has an empty name with room for 8 bytes and zero looks; the destructor
// returns the name's storage.
struct PrintRecord {
    PrintRecord();
    PrintRecord(const PrintRecord& from);
    PrintRecord& operator=(const PrintRecord&) = delete;

    virtual void Serialize(ChunkArchive* ar);

    stlp::string name;           // +0x04
    PrintLook look2;             // +0x10 chunk 2
    PrintLook look3;             // +0x1C chunk 3
};
static_assert(offsetof(PrintRecord, name) == 0x04);
static_assert(sizeof(PrintRecord) == 0x28);

// The second record type of prints.db. 0x34 bytes. Its
// lifetime is PrintRecord's, the values +0x28 with the looks.
struct PrintRecord2 {
    PrintRecord2();
    PrintRecord2(const PrintRecord2& from);
    PrintRecord2& operator=(const PrintRecord2&) = delete;

    virtual void Serialize(ChunkArchive* ar);

    stlp::string name;           // +0x04
    PrintLook look2;             // +0x10 chunk 2
    PrintLook look3;             // +0x1C chunk 3
    std::uint32_t values10[3];   // +0x28 chunk 10
};
static_assert(offsetof(PrintRecord2, name) == 0x04);
static_assert(sizeof(PrintRecord2) == 0x34);

inline const PrintLook& LookOf(const PrintRecord& r, bool second) { return second ? r.look3 : r.look2; }
inline const PrintLook& LookOf(const PrintRecord2& r, bool second) { return second ? r.look3 : r.look2; }

// prints.db (loaded at startup), indexed by the ground's surface class
// (Terrain::SurfaceClass): chunk 1 blood and bloody footprints, chunk 2 footprints, chunk 3
// scorch marks; each table is followed by a hash_map by name (filled by the load, not read by the
// rewritten code).
struct PrintsDatabase {
    template <typename Record> using NameIndex = stlp::hash_map<stlp::string, Record*, stlp::StringHash>;

    // Rebirth-patched, hooked in the vanilla launch
    // only). "prints.db" of `folder` + "database.res": the three tables, each read through an
    // archive of its own and filed under its records' names. 1; FatalError's result when the
    // archive (reported as "databases.res") or the file cannot be opened.
    int Load(const char* folder);
    // The static initialiser's work. The three tables empty,
    // each index with 100 buckets (193). Returns this.
    PrintsDatabase* Construct();
    // The exit function's work. The tables from the
    // last: the index's nodes and buckets, then the records and their storage.
    void Destruct();

    stlp::vector<PrintRecord> blood;          // +0x00
    NameIndex<PrintRecord> bloodByName;       // +0x0C
    stlp::vector<PrintRecord2> footprints;    // +0x20
    NameIndex<PrintRecord2> footprintsByName; // +0x2C
    stlp::vector<PrintRecord> scorch;         // +0x40
    NameIndex<PrintRecord> scorchByName;      // +0x4C
};
static_assert(offsetof(PrintsDatabase, footprints) == 0x20 && offsetof(PrintsDatabase, scorch) == 0x40);
static_assert(offsetof(PrintsDatabase, footprintsByName) == 0x2C && sizeof(PrintsDatabase) == 0x60);
extern lib::StaticObject<PrintsDatabase> g_printsDatabase;
inline const PrintsDatabase& ThePrintsDatabase() { return *g_printsDatabase; }

// ---- Water level changes: water_tweens.h ----------------------------------------------

// A length-prefixed name (a
// four-byte length, then the characters) at *cursor into `out` (emptied for length 0), the
// cursor moved past it (the skeleton links' child / parent names, figure_links.cpp).
void __fastcall ReadPrefixedName(mfc::CString* out, const std::uint8_t** cursor);
