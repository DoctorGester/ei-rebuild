#pragma once

// Model and resource loading: the 3D library side of figures (004F0000-00500000) and the
// databases read at startup (prints.db, acks.db; 006D0000-006E0000). See docs/notes/models.md.
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

// ---- 3D library (004F0000-00500000) ---------------------------------------------------------------

// Matrix4 (row-major 4x4, the transform library's) is in src/lib/vecmath.h.

// Bounding volume of a figure part, tested against the view frustum. 0x2C bytes.
struct ClipBounds {
    std::uint32_t flags;   // +0x00 bit 0: centre/radius valid, bit 1: box valid
    Vector3 center;        // +0x04
    float radius;          // +0x10 half the side of the cube around the centre
    Vector3 boxMin;        // +0x14 box corners, relative to the centre
    Vector3 boxMax;        // +0x20

    // 004F93A0. Sets the box and its flag; returns the new flags (left in EAX).
    std::uint32_t SetBox(const float* min, const float* max);
};
static_assert(sizeof(ClipBounds) == 0x2C);

// One of the transform pipeline's matrix stacks: aligned storage of matrices and the depth.
struct MatrixStack {
    // 004F0DB0 (render_helper.cpp; the pipeline's constructor has the compiler's array constructor run it for each
    // of the 16): room for 16 matrices, empty.
    MatrixStack();
    // 004F0DE0: the block freed.
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
// rewritten code calls it.
class ITransformPipeline {
public:
    virtual void Unknown0();
    // 1: the viewport's size in pixels (the renderer's creation, 00500B70).
    virtual void SetViewportSize(float width, float height);
    // 2: the sun for the next LightMesh (its localDirection_; 005A7A70).
    virtual void AddSun(CLight* sun);
    // 3: the ambient colour, components in 0..1 (the renderer's colour, 00500740).
    virtual void SetAmbient(float r, float g, float b);
    // 4: the projection matrix, transposed (00500B70 passes a copy).
    virtual void SetProjection(float* transposed);
    // 5: the depth range (00500B70: the options' fog start and far clip distances).
    virtual void SetDepthRange(float first, float second);
    virtual void Unknown6();
    // 7: fits `bounds` around `count` vertex packets.
    virtual void FitBounds(ClipBounds* bounds, const VertexPacket* vertices, int count);
    // 8: `count` points (16 bytes each: x, y, z and one more) projected with `matrix` into
    // `projected` (x, y, z, w), a flag byte per point set when it is off screen; 2 when every point
    // is, else 0 or 1 (the ribbon draw 00501D90).
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
    // 14: the same without clipping (every block on screen); the result as slot 9's (the device's
    // mesh draws 004E9010 / 004E9A50 test it, CTransformPipeline::ProjectBlocks does not).
    virtual int Project(const Matrix4* matrix, const void* positions, void* projected, int blocks);
    virtual void Unknown15();
    virtual void Unknown16();
    virtual void Unknown17();
    // 18 / 19: the same for the terrain's vertices (006BFC80).
    virtual int ProjectTerrainClipped(const Matrix4* matrix, const void* vertices, void* projected, void* outcodes,
                                      int count);
    virtual void ProjectTerrain(const Matrix4* matrix, const void* vertices, void* projected, int count);
    virtual void Unknown20();
    virtual void Unknown21();
    virtual void Unknown22();
    virtual void Unknown23();
    // 24: `count` screen-facing sprites (`points`: centre x, y, z and size each) placed with the
    // transposed view matrix as four TL vertices each into `corners` (positions only), with an
    // outcode byte per corner: 0 all on screen, 1 some off screen, else none (the sprite draw
    // 00502CA0).
    virtual int ProjectSprites(const void* transposedView, const void* points, TLVertex* corners,
                               std::uint8_t* outcodes, int count);
    virtual void Unknown25();
    // 26: the `blocks` packets of four positions times the (transposed) matrix, as points (x, y, z, w)
    // into `out` (the decal projector 0056C750).
    virtual void TransformPoints(const Matrix4* transposed, const VertexPacket* vertices, Vector4* out, int blocks);
    virtual void Unknown27();
    virtual void Unknown28();
    // 29: the `blocks` packets projected with `matrix` into `points` (x, y per vertex), `bounds`
    // (min x, min y, max x, max y) grown around them (a shadow's points, 004F4680).
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
    // 40: lights a figure mesh (005A7A70): its vertex packets and normals, the colours written (one
    // per normal), its words, the lights (their localPosition_, colours set for the material) and
    // the material's light vector.
    virtual void LightMesh(const void* vertices, const void* normals, std::uint32_t* colours, const void* words,
                           int wordCount, CLight** lights, int lightCount, const std::uint32_t* vector);
};


// The transform pipeline of the 3D library (the scene renderer's +8, the device object's +0xAC; game.exe's vtable
// 0073D778). Only the matrix stacks and the transform object are known.
class CTransformPipeline final : public CRendererBase {
public:
    // Stack 2 holds the matrix ClipTest projects with; 14 the decal projector's platform objects
    // and parts (CWorldClient::ProjectDecal).
    static constexpr int kClipStack = 2;
    static constexpr int kDecalStack = 14;
    // Stack 0 is the picking's (the view matrix pushed onto it), 9 the view matrices.
    static constexpr int kPickStack = 0;
    static constexpr int kViewStack = 9;

    // 004F16A0. Classifies `bounds` against the view volume of the current (top) matrix:
    // 2 = entirely outside one clip plane, 0 = entirely inside, 1 = crossing or not decided.
    // The cube (flag 0) is tried first, then the box (flag 1).
    int ClipTest(const ClipBounds* bounds) const;
    // 004D9B80 (matrix_math.cpp, RET 8). Pushes `m` onto stack `index`: an empty stack gets `m`
    // itself, otherwise the new top is the old top times `m` (no capacity check).
    void PushMatrix(const float* m, int index);
    // 004F2650 (RET 0x14, A-S8). Projects `blocks` blocks of four positions: `bounds` (null: assumed
    // crossing) classified by ClipTest; on screen (0): the transform object's Project with the top of
    // stack 1, result 0; crossing (1): its ProjectClipped with the top of stack 2, its result;
    // anything else: 2, nothing projected.
    int ProjectBlocks(const void* positions, void* projected, void* outcodes, int blocks, const ClipBounds* bounds);
    // 004F29F0 (RET 0x14, A-S8). The same for terrain vertices (slots 18 / 19), both with stack 2.
    int ProjectTerrain(const void* vertices, void* projected, void* outcodes, int count, const ClipBounds* bounds);
    // 004F0B90 (RET 4; render_helper.cpp; the renderer base's creation 00500540 news it with the game's
    // work folder): the 16 matrix stacks, the processor's CPUID function 1 into the globals, then
    // 3DfpFPU.dll loaded from `folder` and its GetTLPipeline()'s transform object taken. No library, or
    // no such function in it: throws.
    explicit CTransformPipeline(const char* folder);
    // 004F0E10 (deleting: 004F0DF0; render_helper.cpp): the transform object forgotten, 3DfpFPU.dll
    // unloaded, the stacks' blocks freed from the last to the first.
    ~CTransformPipeline() override;
    // The push the draw code inlines (the terrain passes 006C6230 / 006C6330; also the world draw
    // 00569E40 and the decal projector 0056C750, which keep their own copies): as PushMatrix, but
    // the new top is computed by Matrix4::Multiply (004D9880), whose elements sum in another order.
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
    // 005019D0 (RET 4; also inlined). The top of stack `index` (matrices[count - 1], as the original
    // computes it even when empty).
    const Matrix4* Top(int index) const { return stacks_[index].matrices + (stacks_[index].count - 1); }
    // 0051C110 (RET 4; also inlined): drops the top of stack `index` unless it is empty.
    void PopMatrix(int index) {
        if (stacks_[index].count != 0) --stacks_[index].count;
    }

    // Counted (CRendererBase): holders drop a reference, and the last one deletes it.

    std::uint8_t unused_08[4] = {};
    ITransformPipeline* transform_;   // +0x0C 3DfpFPU.dll's transform object (projects and clips vertices)
    void* library_;                   // +0x10 3DfpFPU.dll's module (FreeLibrary in the destructor 004F0E10)
    MatrixStack stacks_[16];    // +0x14 (models.md: stacks 0-2, 5-10 and 14 are in use)
};
static_assert(offsetof(CTransformPipeline, stacks_) == 0x14 && offsetof(CTransformPipeline, transform_) == 0x0C);
static_assert(sizeof(CTransformPipeline) == 0x94);   // as 00500540 allocates it

// The scene renderer's pipeline (world_singletons.cpp): stacks 1 and 2 take the scene matrices (004D9B80), 5 the world
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

// A render state preset the renderer's helper keeps (counted; game.exe's vtable 0073D880 over an abstract base
// 0073D888 with the pure slot 1 and the plain counted root, neither with fields; render_helper.cpp).
class CRenderPreset final {
public:
    // The helper's recording (004FD2A0, inlined): no references, a copy of `blocks`.
    CRenderPreset(const stlp::vector<std::uint32_t>& blocks, int stages, int index);
    // 004FD8B0 (deleting: 004FD880): the blocks' storage freed.
    virtual ~CRenderPreset();
    virtual unsigned Index() { return static_cast<unsigned>(index_); }   // 1: 004FD870 (too short to hook)

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

// What the helper keeps for a preset: the states it was recorded from (copied out by slot 6) and
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

// The renderer's counted helper object (0x118 bytes; game.exe's vtable 0073D840 over an abstract base 0073D860 with
// the slots 1-7 pure and the plain counted root, neither with fields). render_helper.cpp has all of it (A-S11 the
// slots that only read the tables, A-S12 the rest).
class CRenderHelper final {
public:
    static constexpr unsigned kPresetCount = 9;

    // 004FCA60 (RET 8). Takes the device and its capabilities, then records the nine built-in presets (`flag`'s low
    // byte: one texture stage each).
    CRenderHelper(IDirect3DDevice7* device, std::uint32_t flag);
    // 004FCD50 (deleting: 004FCC20). Every record's preset through slot 3, the tables freed, the device released.
    virtual ~CRenderHelper();
    // 1: 004FD2A0 (RET 0xC). Records the state blocks of `states` and keeps the new preset (as
    // built-in preset `index` when that is below 9). `singleStage`: at most one texture stage.
    virtual CRenderPreset* Record(const stlp::vector<RenderPresetRecord::State>* states, int index, bool singleStage);
    // 2: 004FDB00 (RET 4). The record of `preset`, if there is one: slot 3 with its preset.
    virtual void ReleasePreset(CRenderPreset* preset);
    // 3: 004FD9D0 (RET 4). Deletes the preset's state blocks and drops its record.
    virtual void ReleaseBlock(CRenderPreset* preset);
    // 4: 004FDB50 (RET 4). The preset of `preset`'s record, or null without a record.
    virtual void* PresetBlock(CRenderPreset* preset);
    // 5: 004FDBA0 (RET 4). Preset `index` of the nine at +0x10C, or null beyond them.
    virtual CRenderPreset* Preset(unsigned index);
    // 6: 004FDBC0 (RET 8). The states of `preset`'s record copied into `out`; false without a record.
    virtual bool PresetStates(CRenderPreset* preset, stlp::vector<RenderPresetRecord::State>* out);
    // 7: 004FDD80 (RET 8). Slot 6 for preset `index`; false beyond the nine or for a null one.
    virtual bool BuiltInStates(unsigned index, stlp::vector<RenderPresetRecord::State>* out);

    // On the game's heap.
    static void* operator new(std::size_t size) noexcept;
    static void operator delete(void* block);

    CRenderHelper(const CRenderHelper&) = delete;
    CRenderHelper& operator=(const CRenderHelper&) = delete;

    // 004FCE90 (RET 8, AL; render_helper.cpp). The state list of built-in preset `index` into
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

// The scene renderer's base class (CVisualizator in the original's messages; C19: game.exe's 0073D8A0, 25 slots):
// slots 1 (the destructor), 2 (CreateBase), 3 (DestroyBase) and 7 (SetBaseColour) are its own, the rest pure (the
// compiler CRT's `_purecall`, as game.exe's 006E73EB). It owns +0x04 .. +0x1C. Nothing has it as its final class:
// only CSceneRenderer's construction and destruction pass through it.
class CVisualizator {
public:
    // 0: 00501AB0 in the renderer. Sets the device's fog colour render state (D3DRENDERSTATE_FOGCOLOR) to `colour`.
    virtual int SetFogColourState(std::uint32_t colour) = 0;
    // 1: the scalar deleting destructor (00500460: DestroyBase, then operator delete when bit 0 of the flags is set;
    // returns this). The renderer's is 00500AA0.
    virtual ~CVisualizator();
    // 2: the creation (00500540: CreateBase; the renderer's 00500B70, renderer_init.cpp).
    virtual void CreateRenderer(void* window, options::GameOptions* settings);
    // 3: 005006E0 (DestroyBase; the renderer's 005019F0).
    virtual void DestroyRenderer();
    virtual int BeginScene() = 0;            // 4: the renderer's 00503F40
    virtual int EndScene() = 0;              // 5: 005040A0
    virtual std::uint32_t FrameTime() = 0;   // 6: 00500A90
    // 7: 00500740 (SetBaseColour; the renderer's 00501CC0 also tints the device object). Sets the draw colour.
    virtual void SetColour(std::uint32_t colour);
    virtual int ClearFrame() = 0;                                     // 8: 00501A40
    virtual std::uint32_t ZEnable() = 0;                              // 9: 00501AA0
    virtual int SetBlend(std::uint32_t source, std::uint32_t dest) = 0;   // 10: 00501C60
    virtual int SetTexture(int stage, void* texture) = 0;             // 11: 00501AF0
    virtual int DrawTriangles(const void* vertices, int vertexCount, const std::uint16_t* indices,
                              unsigned indexCount) = 0;                // 12: 00501BB0
    virtual int DrawFigureLit(void* figure, const void* lights, void* material, int alpha, tagRECT* screen,
                              CTexture* textureA, CTexture* textureB) = 0;   // 13: 00501D40
    virtual int DrawFigure(void* figure, const void* unknownA, const void* unknownB, void* material, int alpha,
                           tagRECT* screen) = 0;                       // 14: 00501D00
    virtual void DrawLineList(void* lines) = 0;                        // 15: 00501CE0
    virtual int DrawStrip(void* points, float width, int visibility, float length, float offset,
                          float slant) = 0;                            // 16: 00501D90
    virtual int DrawSprites(int unknown, void* points, void* sprites) = 0;   // 17: 00502CA0
    virtual int DrawSelectionMarker(const Vector3* point, float size, const Vector3* axisX,
                                    const Vector3* axisY) = 0;         // 18: 00503A40
    virtual int DrawRectangle(int left, int top, int right, int bottom, int alpha, int outline) = 0;   // 19: 005041F0
    virtual int Polyline(const ScreenPoints* points, std::uint32_t colour) = 0;   // 20: 005045D0
    virtual int DrawTexture(CTexture* texture, float x, float y, float scale) = 0;   // 21: 00504860
    virtual bool SetGamma(float brightness, float contrast, float gamma) = 0;      // 22: 00504A60
    virtual void GetGamma(float* brightness, float* contrast, float* gamma) const = 0;   // 23: 00504A40
    virtual bool RestoreSurfaces() = 0;                                // 24: 00504780

    // Freed into the game's heap (the scalar deleting destructor's operator delete).
    static void operator delete(void* memory);

    // 00500540 (base slot 2, RET 8; renderer_base_create.cpp, A-U12; the renderer's creation calls
    // it first). A new transform pipeline (004F0B90, with the options' work path) replaces the held
    // one; CPUID function 1 goes to the globals at 007CAED0 and a processor without MMX is fatal
    // ("MMX required in this version !!!."); then <work path>2DintMMX.dll is loaded and its
    // GetGraphicsProcessor() kept (fatal when either is missing). `window` is not read. Returns the
    // graphics processor.
    void* CreateBase(void* window, options::GameOptions* settings);
    // 00500440 (the base constructor's work, renderer_lifetime.cpp): flags_, pipeline_ and sun_ cleared. Returns this.
    CVisualizator* SetUpBase();
    // 005006E0 (base slot 3). The sun dropped (set to null, then released) and the transform object
    // told (AddSun with null), the pipeline dropped, the graphics library (2DintMMX.dll) freed and
    // its processor forgotten.
    void DestroyBase();
    // 00500740 (base slot 7, RET 4). colour_ = `colour`; the transform object's ambient (slot 3)
    // becomes its red, green and blue bytes times 1 / 255.
    void SetBaseColour(std::uint32_t colour);
    // 005007C0 (RET 4). The sun becomes `light` (counted; the old one released) and is handed to the
    // transform object (AddSun) when there is one.
    void SetSun(CLight* light);
    // 00500800 (RET 0x10). SetSun, then the colour (slot 7) from r, g, b in 0..1, packed as
    // SetFogColour packs them.
    void SetSunAndAmbient(CLight* light, float r, float g, float b);

    std::uint32_t flags_;            // +0x04 bit 0: the software cursor option; bit 1 set by the creation (00500B70)
    CTransformPipeline* pipeline_;   // +0x08 counted
    CPixelConverter* graphicsProcessor_;   // +0x0C 2DintMMX.dll's GetGraphicsProcessor() (00500540)
    void* graphicsLibrary_;          // +0x10 the HMODULE of 2DintMMX.dll
    std::uint32_t fogColour_;        // +0x14 also the colour the frame is cleared with
    std::uint32_t colour_;           // +0x18 the draw colour (slot 7)
    // +0x1C the sun (counted; 00500800 sets it with the ambient colour, the client's lighting
    // 00566360 moves it every frame).
    CLight* sun_;

protected:
    CVisualizator();   // 00500440's work (SetUpBase)
};

// R16i: the ambient colour (r, g, b in 0..1) SetBaseColour last gave the transform object: 3DfpFPU's object
// is one for every renderer, and its LightMesh lit the figure parts with it; the parts lit on the GPU
// take it from here (figure_part_draw.cpp).
const float* TransformAmbientColour();

// The scene renderer (the global at 007AF060, 26 slots; 00500960 returns it). C19: a real virtual class over
// CVisualizator, the compiler's table in place of game.exe's 0073D93C; the static initialiser's constructor
// (00500A70) and the exit's destructor (00500AC0) construct and destroy it in place.
// A counted texture pointer as the original's helpers take it: by address, thiscall on the pointer
// (the particle sources', the rain's, the sky's and the lightning's textures). Reads see the texture;
// a plain store (`held = p`) counts nothing.
struct HeldTexture {
    // 00500A00 (RET 8; renderer_lifetime.cpp). Drops the texture (set to null, then released) and
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

// The device object's counted holder (CSceneRenderer::device_): 00504E00 releases it (the pointer
// left), 00504E20 (RET 4; renderer_lifetime.cpp) takes `device` (one more reference) and releases
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
    // 0: 00501AB0 (device_states.cpp).
    int SetFogColourState(std::uint32_t colour) override;
    // 1: 00500AA0 (RET 4; renderer_lifetime.cpp). The device object dropped (null, then released), the helper
    // released (left set), then the base's destructor.
    ~CSceneRenderer() override;
    // 2: 00500B70 (renderer_init.cpp).
    void CreateRenderer(void* window, options::GameOptions* settings) override;
    // 3: 005019F0 (renderer_lifetime.cpp). The base part's DestroyBase, flags_ bit 1 cleared, the
    // helper and the device object dropped (each set to null, then released).
    void DestroyRenderer() override;
    // renderer_scene.cpp (A-U10).
    // 4: 00503F40 (plain RET). 0 unless flags_ bit 0 is set. The frame statistics:
    // this frame's time since the last EndScene, the shortest (nonzero) and longest (under 1000 ms),
    // the total and count, and once a second's worth has passed the average over it; then ClearFrame
    // (slot 8), the device's counters cleared and IDirect3DDevice7::BeginScene. When a scene is
    // still open (D3DERR_SCENE_IN_SCENE) the batches are flushed, the scene ended and begun again;
    // other failures are fatal ("Can't begin scene." / "Can't end scene."). Returns 1.
    int BeginScene() override;
    // 5: 005040A0 (plain RET). Flushes the device's two batches and ends the scene
    // (fatal on failure other than D3DERR_SCENE_NOT_IN_SCENE), then sets the debug panel's "FPS"
    // (1000 / frame time, "%g"), "num verts" and "num trias" ("%d") and remembers the frame's start.
    // Returns 1.
    int EndScene() override;
    // 6: 00500A90 (too short to hook). The last frame's time in ms (frameTime_).
    std::uint32_t FrameTime() override { return frameTime_; }
    // 7: 00501CC0 (RET 4). Sets the colour the next draws are tinted with (kept at +0x18 and in the
    // device object's colour_), the transform object's ambient from it (SetBaseColour).
    void SetColour(std::uint32_t colour) override;
    // 8: 00501A40 (EAX 1). Clears the render target and the depth (and the stencil when the device
    // has one) to the fog colour and 1.
    int ClearFrame() override;
    // 9: 00501AA0. The device object's z-buffer setting (CTextureDevice::zEnable_).
    std::uint32_t ZEnable() override;
    // 10: 00501C60 (RET 8; device_states.cpp). Sets SRCBLEND, then DESTBLEND.
    int SetBlend(std::uint32_t source, std::uint32_t dest) override;
    // 11: 00501AF0 (RET 8, EAX 1). Binds `texture` (a texture object, or null) to stage `stage`.
    int SetTexture(int stage, void* texture) override;
    // 12: 00501BB0 (RET 0x10; renderer_triangles.cpp). Draws pre-transformed vertices (D3DTLVERTEX,
    // FVF 0x1C4) as an indexed triangle list with the current state. Returns 1.
    int DrawTriangles(const void* vertices, int vertexCount, const std::uint16_t* indices, unsigned indexCount) override;
    // 13: 00501D40 (RET 0x1C). Draws `figure` (a CFigureMesh) through its drawer's lit draw
    // (005A7A70) with `lights`, the drawing context `material`, `alpha`, `screen` (or null) and two
    // textures, unless it is hidden (flags +8 bit 0x80). Returns 1.
    int DrawFigureLit(void* figure, const void* lights, void* material, int alpha, tagRECT* screen, CTexture* textureA,
                      CTexture* textureB) override;
    // 14: 00501D00 (RET 0x18). Draws `figure` through its drawer (+0x94, 005A7640) unless it is
    // hidden (flags +8 bit 0x80); `alpha` reaches the mesh draw, `screen` (or null) gets the
    // rectangle drawn, `material` (null, or an interface sprite's material) is passed on. The second
    // and third arguments are not read there.
    int DrawFigure(void* figure, const void* unknownA, const void* unknownB, void* material, int alpha,
                   tagRECT* screen) override;
    // 15: 00501CE0 (RET 4). Draws a line list (0049B780 with the pipeline and the device object).
    void DrawLineList(void* lines) override;
    // 16: 00501D90 (RET 0x18; renderer_strip.cpp). Draws a strip along `points` (a lightning segment's aligned points) of
    // `width` with the bound texture: `visibility` the bounds' clip result, the texture's `length`
    // and `offset` along it (0 and 0: untextured lengthwise), `slant` 0.25 at every call known.
    int DrawStrip(void* points, float width, int visibility, float length, float offset, float slant) override;
    // 17: 00502CA0 (RET 0xC; renderer_sprites.cpp). Draws sprites facing the view with the bound texture: `points` an aligned array
    // of centres and sizes (x, y, z, size), `sprites` a CArray of their colours and texture
    // rectangles. The first argument is 0 at every call known.
    int DrawSprites(int unknown, void* points, void* sprites) override;
    // 18: 00503A40 (RET 0x10, EAX 1; renderer_marker.cpp, A-U9). The selection marker under a selected
    // unit (00569E40's draw pass): a flat green arrow of half-transparent 0x8000FF00, 0.3 above
    // `point`, `size` long, pointing along -`axisY` (`axisX` across), drawn by the device's flat
    // geometry draw (004EA4A0) as six triangles of two vertex blocks.
    int DrawSelectionMarker(const Vector3* point, float size, const Vector3* axisX, const Vector3* axisY) override;
    // 19: 005041F0 (RET 0x18; renderer_rect.cpp). A black rectangle (left, top, right, bottom in
    // screen pixels) of `alpha` over the current blend state, with `outline` a white frame. Returns 1.
    int DrawRectangle(int left, int top, int right, int bottom, int alpha, int outline) override;
    // 20: 005045D0 (RET 8; renderer_lines.cpp). A screen polyline in `colour`.
    int Polyline(const ScreenPoints* points, std::uint32_t colour) override;
    // 21: 00504860 (RET 0x10). Draws `texture` at (x, y) scaled by `scale` as a
    // pre-transformed strip of four vertices (z 0.9, white, specular 0xFF000000) through stage 0
    // (slot 11). 0 without a texture, else 1.
    int DrawTexture(CTexture* texture, float x, float y, float scale) override;
    // 22: 00504A60 (RET 0xC, AL; renderer_gamma.cpp, A-U9; the options' brightness,
    // contrast and gamma, at start-up, from the options screen and with zeros at exit). Without a
    // device or a primary-surface gamma ramp (DDCAPS2_PRIMARYGAMMA) false. Keeps the three values,
    // then sets the primary surface's gamma ramp (calibrated when the driver can): each value
    // clamped to -1..1 and halved; level i / 255 raised to 1 / (1 + 5 g) (g > 0) or 1 / (1 + g / 2)
    // (g < 0), times the contrast's scale (1 + 4 |c|, inverted for c < 0) and centred, clamped to
    // 0..1, plus the brightness, clamped again, times 65280 (truncated) for all three channels.
    // True when SetGammaRamp succeeds.
    bool SetGamma(float brightness, float contrast, float gamma) override;
    // 23: 00504A40 (RET 0xC). The display settings SetGamma last kept.
    void GetGamma(float* brightness, float* contrast, float* gamma) const override;
    // 24: 00504780 (plain RET, AL; WM_ACTIVATE after a switch back, app.cpp). False without
    // a device object. A full-screen device gets its cooperative level (004EECF0 with its screen
    // rectangle) and display mode (004EEBC0: the render target's size and depth) back; then
    // IDirectDraw7::RestoreAllSurfaces under the device lock, the primary, render target and depth
    // surfaces restored, and the texture manager's restore (00693F60), whose result is returned.
    bool RestoreSurfaces() override;
    // 25: 005015A0 (RET 8; renderer_init.cpp, A-U12; nothing calls it). The pipeline's
    // matrix stacks emptied and filled again for `screen` and a field of view (0: the camera's), as
    // the creation fills them, then the transform object's projection and viewport size.
    virtual void SetView(const tagRECT* screen, float fieldOfView);

    // 00497CE0 (RET 8; particle_render.cpp). Makes sure the texture in `holder` is loaded
    // (00500970) and binds it to `stage` through slot 11.
    int BindHeldTexture(int stage, HeldTexture* holder);
    // 004BA120 (RET 0xC; scene_renderer.cpp, A-S8). The fog (and clear) colour from components in
    // 0..1: each times 255 as a float, rounded to the nearest integer (FISTP); A = 0xFF, packed as
    // ((r | 0xFFFFFF00) << 8 | g) << 8 | b; stored at +0x14 and set through slot 0.
    void SetFogColour(float r, float g, float b);

    HeldDevice device_;              // +0x20 the device object (007AF080), a counted holder (00504E20 assigns)
    CRenderHelper* helper_;          // +0x24 counted
    // +0x28 .. +0x30: the display settings slot 22 (SetGamma) last applied, as given.
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

    CSceneRenderer();   // 00500A70's work: the base, device_ and helper_ cleared
};
static_assert(offsetof(CSceneRenderer, pipeline_) == 0x08 && offsetof(CSceneRenderer, fogColour_) == 0x14 &&
              offsetof(CSceneRenderer, colour_) == 0x18 && offsetof(CSceneRenderer, device_) == 0x20);
static_assert(offsetof(CSceneRenderer, helper_) == 0x24 && offsetof(CSceneRenderer, frameTime_) == 0x34 &&
              offsetof(CSceneRenderer, lastFrameStart_) == 0x50 && offsetof(CSceneRenderer, windowFrames_) == 0x58);

// renderer_lifetime.cpp (A-U11). 00500960 (cdecl): the renderer (the global at 007AF060).
CSceneRenderer* __cdecl SceneRenderer();

// 004F1370. Viewport matrix for a screen rectangle {left, top, right, bottom}: scale and offset by
// the half extents, y flipped. Returns out.
Matrix4* __stdcall ViewportMatrix(Matrix4* out, const int* rect);
// 004F0E80. Perspective projection; `fov` in radians (full angle). Returns out.
Matrix4* __stdcall ProjectionMatrix(Matrix4* out, float fov, float aspect, float zNear, float zFar);

// Triangle index buffer of a figure part: 3 uint16 indices per triangle, an engine aligned array
// (lib::AlignedArray). Its SetSize (004F57B0, growBy -1 keeps the step) is AlignedArray::SetSize.
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

// A figure's cached model (0x20 bytes, constructor 005A9A50, destructor 005A9C90): the mesh, bones
// and animation archives of "<name>.mod / .bon / .anm" (0x6C-byte CMMIOResource objects; null when
// missing), then the animations read from the last by name (005A9EF0). The figure manager
// (CFigureManager::models_, 005AA5F0) and the link figure loader (its figure sets, 004FAB00) cache
// them by name.
struct FigureModel {
    CMappedResource* FindAnimation(const stlp::string* name);
    using AnimationMap = stlp::hash_map<stlp::string, CMappedResource*, stlp::StringHash>;

    CMappedResource* mesh;          // +0x00 the .mod archive
    CMappedResource* bones;         // +0x04
    CMappedResource* animations;    // +0x08
    AnimationMap animationCache;    // +0x0C each a CMMIOResource over the animation's data

    // 005A9EB0 (RET 0xC; the link figure loader's sets, 004FAB00; figure_manager.cpp, A-W14). Opens
    // the three archives from the images of the figure's .mod / .bon / .anm files (OpenFromMemory);
    // a null image leaves its archive alone.
    void Open(const void* mod, const void* bon, const void* anm);
};
static_assert(offsetof(FigureModel, animationCache) == 0x0C && sizeof(FigureModel) == 0x20);
// 005A9A50 (figure_models.cpp) and 005A9C90 (figure_model_destroy.cpp): a figure model's constructor
// (returns it) and destructor.
FigureModel* __fastcall ConstructFigureModel(FigureModel* model);
void __fastcall DestroyFigureModel(FigureModel* model);

// Loads .lnk link files and the figures they name (game.exe's vtable 0073D808, 7 slots, over an abstract base
// 0073D824 with the slots 1-6 pure and the plain counted root, neither with fields; created at startup with
// figures.res and menus.res by 004F68C0, a Rebirth-patched function). A figure is a CLinkMesh of
// parts morphed by three coefficients (s, t, u); the loader keeps every mesh it made in a
// multimap by name (a counted reference each) and hands out a cached one whose coefficients are
// within 1e-5 (squared distance, the double at 0073B860) unless asked for a fresh one. Parts come
// from the figure's own archive set when there is one (004FAB00), else from the open archives
// as "<figure><part>.fig". link_loader.cpp (A-S7).
class CLinkMesh;
class CTextureDevice;
class ITransformPipeline;
struct VertexPacket;

class CLinkFigureLoader final {
public:
    // 004F68C0 (link_loader_create.cpp, RET 8): the renderer held, the cache and the figure and menu archives of
    // `folder` opened (a failure is only logged).
    CLinkFigureLoader(CTextureDevice* renderer, const stlp::string* folder);
    // 004F6BD0 (deleting: 004F6A30; A-S8). Each archive closed (slot 3) and deleted, the archive list emptied, each
    // figure set destroyed (005A9C90) and freed, the device object released (the field cleared first), the cache
    // emptied (ClearCache's walk), then the containers' storage.
    virtual ~CLinkFigureLoader();
    virtual int CacheSize();                    // 1: 004F6A20 (cache_'s count)
    virtual int CacheBytes();                   // 2: 004FAAA0 (each cached mesh's MemoryBytes summed; A-S8)
    // 3: 004F7150 (RET 0x18). Figure `name` with the parts `parts`: unless `fresh`, the first cached
    // mesh of that name with close coefficients, given the parts it lacks; otherwise a new one
    // (MakeFigure). No reference is added for the caller.
    virtual CLinkMesh* LoadFigure(const stlp::string* name, const stlp::vector<stlp::string>* parts, float s, float t,
                                  float u, bool fresh);
    // 4: 004F7080 (RET 0x14). The same for a .lnk figure: a cached one, or LoadLinkFile.
    virtual CLinkMesh* LoadLink(const stlp::string* name, float s, float t, float u, bool fresh);
    virtual void ClearCache();                  // 5: 004F7020
    // 6: 004F75E0 (RET 0x10). `mesh` with new coefficients: a mesh others also hold (more than
    // two references) is looked up again through slot 4 by its name; otherwise its parts are
    // morphed in place (SetMorph, then 004F9E50 per part). Null stays null.
    virtual CLinkMesh* Remorph(CLinkMesh* mesh, float s, float t, float u);

    // On the game's heap (link_loader_create.cpp).
    static void* operator new(std::size_t size) noexcept;
    static void operator delete(void* block);

    CLinkFigureLoader(const CLinkFigureLoader&) = delete;
    CLinkFigureLoader& operator=(const CLinkFigureLoader&) = delete;

    // 004F6D30. Opens `path` as a memory-mapped archive and appends it to the list, unless an
    // archive of that exact name is already open. Returns 1 on success or when already open.
    int AddArchive(const stlp::string* path);
    // 004F7F00 (RET 0x18; link_figure.cpp). Reads a FIG / FIG8 part record at *cursor (moved past
    // it) into `mesh` as part `name`: its header (two dwords, bounds fitted to the vertices morphed
    // by s, t, u) and its elements (CLinkMesh::AddPart).
    void LoadFigPart(CLinkMesh* mesh, const stlp::string* name, const char** cursor, float s, float t, float u);
    // 004F87D0 (RET 0x18). The same for the older record without a FIG header (6 morph corners).
    void LoadOldPart(CLinkMesh* mesh, const stlp::string* name, const char** cursor, float s, float t, float u);

    // link_loader.cpp (A-S7):
    // 004F77C0 (RET 0x10). A new mesh from .lnk file `name` (from the figure set, else the
    // archives' "<name>.lnk"): a count, then per part its name and a second, unused name
    // (ReadLinkString; an empty name keeps the previous one); each part loaded from the figure
    // set or the archives. Cached (one reference). No link file: FatalError("Can't find link
    // file ..."), whose result (0) is returned and the new mesh leaked.
    CLinkMesh* LoadLinkFile(const stlp::string* name, float s, float t, float u);
    // 004F7B10 (RET 0x14). A new mesh `name` with the parts `parts`, loaded as LoadLinkFile does
    // (a part missing from the figure set is skipped); cached.
    CLinkMesh* MakeFigure(const stlp::string* name, const stlp::vector<stlp::string>* parts, float s, float t, float u);
    // 004F7480 (RET 8, AL). Part `name` of `mesh` (its own coefficients) unless it has it: from its
    // figure set (false when the set lacks it) or the archives (LoadArchivePart's result).
    bool LoadPartInto(CLinkMesh* mesh, const stlp::string* name);
    // 004F7D20 (RET 0x14, EAX 1 / 0). Part `name` of `mesh` from the first open archive that has
    // "<mesh name><name>.fig".
    int LoadArchivePart(CLinkMesh* mesh, const stlp::string* name, float s, float t, float u);
    // 004FAB00 (RET 4; A-S8). Figure `name`'s own archive set: the cached one, else (when an open
    // archive has "<name>.mod") a new set (005A9A50) stored under the name and loaded from the first
    // archives with "<name>.mod" / ".bon" / ".anm" (005A9EB0); null without a .mod.
    FigureModel* FindFigureSet(const stlp::string* name);
    // 004F9E50 (RET 0x18, AL; A-S8). Part `name` of `mesh` morphed again by s, t, u from its record
    // (the figure set's model, else "<mesh name><name>.fig" in the archives): false without one.
    bool RemorphPart(CLinkMesh* mesh, FigureModel* set, const stlp::string* name, float s, float t, float u);
    // 004F9FD0 / 004FA670 (RET 0x18; link_figure.cpp). The record at *cursor (moved past the
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

// ---- Databases (006D0000-006E0000) -----------------------------------------------------------------

// What chunks 2 and 3 of both prints.db record types hold: a print's alpha and its life and lifetime
// in ticks (the client world's +0x7A8 picks chunk 3).
struct PrintLook {
    float alpha;
    int life;
    int lifetime;
};
static_assert(sizeof(PrintLook) == 0xC);

// A record of prints.db (terrain footprints; game.exe's vtable 00744264). 0x28 bytes. The records live
// in the prints' vectors (record_vectors.h): the copies copy the name and the looks; the record resize(n)
// appends copies of (006D26E0) has an empty name with room for 8 bytes and zero looks; the destructor
// (006D29A0) returns the name's storage.
struct PrintRecord {
    PrintRecord();
    PrintRecord(const PrintRecord& from);
    PrintRecord& operator=(const PrintRecord&) = delete;

    virtual void Serialize(ChunkArchive* ar);   // 0: 006D41D0

    stlp::string name;           // +0x04
    PrintLook look2;             // +0x10 chunk 2
    PrintLook look3;             // +0x1C chunk 3
};
static_assert(offsetof(PrintRecord, name) == 0x04);
static_assert(sizeof(PrintRecord) == 0x28);

// The second record type of prints.db (game.exe's vtable 00744268; destructor 006D29E0). 0x34 bytes. Its
// lifetime is PrintRecord's, the values +0x28 with the looks.
struct PrintRecord2 {
    PrintRecord2();
    PrintRecord2(const PrintRecord2& from);
    PrintRecord2& operator=(const PrintRecord2&) = delete;

    virtual void Serialize(ChunkArchive* ar);   // 0: 006D4270

    stlp::string name;           // +0x04
    PrintLook look2;             // +0x10 chunk 2
    PrintLook look3;             // +0x1C chunk 3
    std::uint32_t values10[3];   // +0x28 chunk 10
};
static_assert(offsetof(PrintRecord2, name) == 0x04);
static_assert(sizeof(PrintRecord2) == 0x34);

inline const PrintLook& LookOf(const PrintRecord& r, bool second) { return second ? r.look3 : r.look2; }
inline const PrintLook& LookOf(const PrintRecord2& r, bool second) { return second ? r.look3 : r.look2; }

// prints.db (007C89C8, loaded at startup by 006D0630), indexed by the ground's surface class
// (Terrain::SurfaceClass): chunk 1 blood and bloody footprints, chunk 2 footprints, chunk 3
// scorch marks; each table is followed by a hash_map by name (filled by the load, not read by the
// rewritten code).
struct PrintsDatabase {
    template <typename Record> using NameIndex = stlp::hash_map<stlp::string, Record*, stlp::StringHash>;

    // 006D0630 (RET 4; model_db_open.cpp, A-H26; Rebirth-patched, hooked in the vanilla launch
    // only). "prints.db" of `folder` + "database.res": the three tables, each read through an
    // archive of its own and filed under its records' names. 1; FatalError's result when the
    // archive (reported as "databases.res") or the file cannot be opened.
    int Load(const char* folder);
    // 006D0250 (database_construct.cpp; the static initialiser 006D00B0). The three tables empty,
    // each index with 100 buckets (193). Returns this.
    PrintsDatabase* Construct();
    // 006D00E0 (database_destruct.cpp, S3c; the exit function 006D00D0). The tables from the
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
extern lib::StaticObject<PrintsDatabase> g_printsDatabase;   // 007C89C8, database_globals.cpp
inline const PrintsDatabase& ThePrintsDatabase() { return *g_printsDatabase; }

// ---- Water level changes (006D4400): water_tweens.h ----------------------------------------------

// model_names.cpp (A-S4). 004DBCA0 (__fastcall: ECX out, EDX cursor): a length-prefixed name (a
// four-byte length, then the characters) at *cursor into `out` (emptied for length 0), the
// cursor moved past it (the skeleton links' child / parent names, figure_links.cpp).
void __fastcall ReadPrefixedName(mfc::CString* out, const std::uint8_t** cursor);
