#pragma once

// Model and resource loading: the 3D library side of figures (004F0000-00500000) and the
// databases read at startup (prints.db, acks.db; 006D0000-006E0000). See docs/notes/models.md.
// Names are provisional.

#include "chunk_archive.h"
#include "maths.h"
#include "script.h"   // ScriptStream

#include "lib/containers.h"
#include "lib/stlp.h"

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
    Matrix4* matrices;      // +0x00
    int count;              // +0x04 the top is matrices[count - 1]
};
static_assert(sizeof(MatrixStack) == 8);

// 004D9880 (matrix_math.cpp; ECX = out, RET 8). out = a * b (row-major: rows of `a` times columns
// of `b`). The elements are written one after the other, each from the inputs as they are then, so
// `out` may be one of the inputs as in the original.
void __fastcall MatrixMultiply(float* out, void*, const float* a, const float* b);

class ITransformPipeline;   // link_figure.h

// The transform pipeline of the 3D library (the scene renderer's +8, the device object's +0xAC).
// Only the matrix stacks and the transform object are known.
class CTransformPipeline {
public:
    // Stack 2 holds the matrix ClipTest projects with; 14 the decal projector's platform objects
    // and parts (CWorldClient::ProjectDecal).
    static constexpr int kClipStack = 2;
    static constexpr int kDecalStack = 14;

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
    // The push the draw code inlines (the terrain passes 006C6230 / 006C6330; also the world draw
    // 00569E40 and the decal projector 0056C750, which keep their own copies): as PushMatrix, but
    // the new top is computed by MatrixMultiply (004D9880), whose elements sum in another order.
    void PushMatrixProduct(const Matrix4& m, int index) {
        MatrixStack& stack = stacks_[index];
        if (stack.count == 0) {
            stack.count = 1;
            stack.matrices[0] = m;
            return;
        }
        const int depth = ++stack.count;
        MatrixMultiply(stack.matrices[depth - 1].m, nullptr, stack.matrices[depth - 2].m, m.m);
    }
    // 005019D0 (RET 4; also inlined). The top of stack `index` (matrices[count - 1], as the original
    // computes it even when empty).
    const Matrix4* Top(int index) const { return stacks_[index].matrices + (stacks_[index].count - 1); }
    // 0051C110 (RET 4; also inlined): drops the top of stack `index` unless it is empty.
    void PopMatrix(int index) {
        if (stacks_[index].count != 0) --stacks_[index].count;
    }

    // Counted: holders drop a reference and call the deleting destructor (vtable slot 0, with 1) at 0.
    void Release() {
        if (--refs_ != 0) return;
        using Destroy = void(__fastcall*)(CTransformPipeline*, void*, int);
        (*static_cast<Destroy*>(vtable_))(this, nullptr, 1);
    }

    void* vtable_;
    int refs_;                        // +0x04
    std::uint8_t unknown_08[4];
    ITransformPipeline* transform_;   // +0x0C 3DfpFPU.dll's transform object (projects and clips vertices)
    void* library_;                   // +0x10 3DfpFPU.dll's module (FreeLibrary in the destructor 004F0E10)
    MatrixStack stacks_[16];    // +0x14 (models.md: stacks 0-2, 5-10 and 14 are in use)
};
static_assert(offsetof(CTransformPipeline, stacks_) == 0x14 && offsetof(CTransformPipeline, transform_) == 0x0C);
static_assert(sizeof(CTransformPipeline) == 0x94);   // as 00500540 allocates it

// The scene's pipeline (the global pointer at 007AF068; world_client_view.cpp calls it the render
// device): stacks 1 and 2 take the scene matrices (004D9B80), 5 the world draw's, 9 the views.
inline CTransformPipeline* ScenePipeline() { return *reinterpret_cast<CTransformPipeline* const*>(0x007AF068); }

struct tagRECT;
class CTextureDevice;
namespace options {
class GameOptions;
}
class CLight;   // spell.h
class CTexture;   // texture.h

// The renderer's counted helper object (0x118 bytes, vtable 0073D840, constructor 004FCA60 with the
// Direct3D device and a flag). Slot 0 is the deleting destructor.
// A render state preset the helper keeps (counted; slot 0 is the deleting destructor).
// (vtable 0073D880; its base's, 0073D888, has the pure slot 1.)
class __declspec(novtable) CRenderPreset {
public:
    virtual void* DeleteThis(unsigned flags);   // 0: 004FD880 (the destructor body 004FD8B0)

    int refs_;                          // +0x04
    stlp::vector<std::uint32_t> items_; // +0x08 freed by the destructor (what it holds is not known)
};
static_assert(offsetof(CRenderPreset, items_) == 0x08);

// What the helper keeps for a preset: the render states it sets (40-byte records, copied out by
// slot 6) and a counted object slot 3 (004FD9D0) takes.
struct RenderPresetRecord {
    struct State {
        std::uint32_t words[10];
    };
    stlp::vector<State> states;   // +0x00
    void* block;                  // +0x0C counted (the count at +4, slot 0 deletes)
};
static_assert(sizeof(RenderPresetRecord) == 0x10 && sizeof(RenderPresetRecord::State) == 0x28);

// render_helper.cpp (A-S11) has the slots that only read the helper's tables (2, 4, 5, 6, 7); the
// others create, apply and delete Direct3D state blocks and are not rewritten.
class __declspec(novtable) CRenderHelper {
public:
    static constexpr unsigned kPresetCount = 9;

    virtual void* DeleteThis(unsigned flags);   // 0: 004FCC20 (the destructor body 004FCD50)
    virtual void Unknown1();                    // 1: 004FD2A0
    // 2: 004FDB00 (RET 4). The record of `preset`, if there is one: slot 3 with its block.
    virtual void ReleasePreset(CRenderPreset* preset);
    virtual void ReleaseBlock(void* block);     // 3: 004FD9D0 (RET 4)
    // 4: 004FDB50 (RET 4). The block of `preset`'s record, or null without a record.
    virtual void* PresetBlock(CRenderPreset* preset);
    // 5: 004FDBA0 (RET 4). Preset `index` of the nine at +0x10C, or null beyond them.
    virtual CRenderPreset* Preset(unsigned index);
    // 6: 004FDBC0 (RET 8). The states of `preset`'s record copied into `out`; false without a record.
    virtual bool PresetStates(CRenderPreset* preset, stlp::vector<RenderPresetRecord::State>* out);
    // 7: 004FDD80 (RET 8). Slot 6 for preset `index`; false beyond the nine or for a null one.
    virtual bool BuiltInStates(unsigned index, stlp::vector<RenderPresetRecord::State>* out);

    int refs_;   // +0x04
    std::uint8_t unknown_08[0xF8 - 0x08];
    stlp::hash_map<CRenderPreset*, RenderPresetRecord, stlp::PointerHash> records_;   // +0xF8 by preset
    CRenderPreset** presets_;           // +0x10C the nine built-in presets
    std::uint8_t unknown_110[0x118 - 0x110];
};
static_assert(offsetof(CRenderHelper, refs_) == 0x04 && sizeof(CRenderHelper) == 0x118);
static_assert(offsetof(CRenderHelper, records_) == 0xF8 && offsetof(CRenderHelper, presets_) == 0x10C);

// The scene renderer (the global at 007AF060, vtable 0073D93C, 26 slots; 00500960 returns it).
// Only the slots rewritten code calls are named. The game's code stores the vtable (novtable).
class __declspec(novtable) CSceneRenderer {
public:
    // 0: 00501AB0. Sets the device's fog colour render state (D3DRENDERSTATE_FOGCOLOR) to `colour`.
    virtual void SetFogColourState(std::uint32_t colour);
    // 1: 00500AA0 (RET 4; renderer_lifetime.cpp). Destructor body (00500AC0), then operator delete
    // when bit 0 of `flags` is set. Returns this.
    virtual void* DeleteRenderer(unsigned flags);
    virtual void Unknown2();   // 2: 00500B70 (the creation, renderer_init.cpp)
    // 3: 005019F0 (renderer_lifetime.cpp). The base part's DestroyBase, flags_ bit 1 cleared, the
    // helper and the device object dropped (each set to null, then released).
    virtual void DestroyRenderer();
    virtual void Unknown4();   // 4: 00503F40 (BeginScene below)
    virtual void Unknown5();   // 5: 005040A0 (EndScene below)
    // 6: 00500A90 (too short to hook). The last frame's time in ms (frameTime_).
    virtual std::uint32_t FrameTime();
    // 7: 00501CC0 (RET 4). Sets the colour the next draws are tinted with (kept at +0x18 and in the
    // device object's colour_), the transform object's ambient from it (SetBaseColour).
    virtual void SetColour(std::uint32_t colour);
    // 8: 00501A40 (EAX 1). Clears the render target and the depth (and the stencil when the device
    // has one) to the fog colour and 1.
    virtual int ClearFrame();
    // 9: 00501AA0. The device object's z-buffer setting (CTextureDevice::zEnable_).
    virtual std::uint32_t ZEnable();
    virtual void Unknown10();
    // 11: 00501AF0 (RET 8, EAX 1). Binds `texture` (a texture object, or null) to stage `stage`.
    virtual int SetTexture(int stage, void* texture);
    // 12: 00501BB0 (RET 0x10; renderer_triangles.cpp). Draws pre-transformed vertices (D3DTLVERTEX,
    // FVF 0x1C4) as an indexed triangle list with the current state. Returns 1.
    virtual int DrawTriangles(const void* vertices, int vertexCount, const std::uint16_t* indices, unsigned indexCount);
    // 13: 00501D40 (RET 0x1C). Draws `figure` (a CFigureMesh) through its drawer's lit draw
    // (005A7A70) with `lights`, the drawing context `material`, `alpha`, `screen` (or null) and two
    // textures, unless it is hidden (flags +8 bit 0x80). Returns 1.
    virtual int DrawFigureLit(void* figure, const void* lights, void* material, int alpha, tagRECT* screen,
                              CTexture* textureA, CTexture* textureB);
    // 14: 00501D00 (RET 0x18). Draws `figure` through its drawer (+0x94, 005A7640) unless it is
    // hidden (flags +8 bit 0x80); `alpha` reaches the mesh draw, `screen` (or null) gets the
    // rectangle drawn, `material` (null, or an interface sprite's material) is passed on. The second
    // and third arguments are not read there.
    virtual int DrawFigure(void* figure, const void* unknownA, const void* unknownB, void* material, int alpha,
                           tagRECT* screen);
    // 15: 00501CE0 (RET 4). Draws a line list (0049B780 with the pipeline and the device object).
    virtual void DrawLineList(void* lines);
    virtual void Unknown16();
    virtual void Unknown17();
    virtual void Unknown18();
    // 19: 005041F0 (RET 0x18; renderer_rect.cpp). A black rectangle (left, top, right, bottom in
    // screen pixels) of `alpha` over the current blend state, with `outline` a white frame. Returns 1.
    virtual int DrawRectangle(int left, int top, int right, int bottom, int alpha, int outline);

    // renderer_lifetime.cpp (A-U11). The base class (CVisualizator in the original's messages, vtable
    // 0073D8A0: slots 1 00500460, 2 00500540, 3 005006E0, 7 00500740, the rest pure) owns +0x04 ..
    // +0x1C; these are its methods, named apart from the renderer's.
    // 00500540 (base slot 2, RET 8; renderer_base_create.cpp, A-U12; the renderer's creation calls
    // it first). A new transform pipeline (004F0B90, with the options' work path) replaces the held
    // one; CPUID function 1 goes to the globals at 007CAED0 and a processor without MMX is fatal
    // ("MMX required in this version !!!."); then <work path>2DintMMX.dll is loaded and its
    // GetGraphicsProcessor() kept (fatal when either is missing). `window` is not read. Returns the
    // graphics processor.
    void* CreateBase(void* window, options::GameOptions* settings);
    // 005015A0 (virtual slot 25, RET 8; renderer_init.cpp, A-U12; nothing calls it). The pipeline's
    // matrix stacks emptied and filled again for `screen` and a field of view (0: the camera's), as
    // the creation fills them, then the transform object's projection and viewport size.
    void SetView(const tagRECT* screen, float fieldOfView);
    // 00500440. Base constructor: vtable 0073D8A0, flags_, pipeline_ and sun_ cleared. Returns this.
    CSceneRenderer* ConstructBase();
    // 00500480. Base destructor body: vtable 0073D8A0, then DestroyBase.
    void DestructBase();
    // 00500460 (base slot 1, RET 4). DestructBase, operator delete when bit 0 of `flags` is set.
    CSceneRenderer* DeleteBase(unsigned flags);
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
    // 00500A70. Constructor: ConstructBase, device_ and helper_ cleared, vtable 0073D93C. Returns this.
    CSceneRenderer* ConstructRenderer();
    // 00500AC0. Destructor body: vtable 0073D93C; the device object dropped (null, then released),
    // the helper released (left set), then DestructBase.
    void DestructRenderer();
    // 00504A40 (virtual slot 23, RET 0xC). The display settings SetGamma last kept.
    void GetGamma(float* brightness, float* contrast, float* gamma) const;
    // 00504780 (virtual slot 24, plain RET, AL; WM_ACTIVATE after a switch back, app.cpp). False without
    // a device object. A full-screen device gets its cooperative level (004EECF0 with its screen
    // rectangle) and display mode (004EEBC0: the render target's size and depth) back; then
    // IDirectDraw7::RestoreAllSurfaces under the device lock, the primary, render target and depth
    // surfaces restored, and the texture manager's restore (00693F60), whose result is returned.
    bool RestoreSurfaces();

    // 004BA120 (RET 0xC; scene_renderer.cpp, A-S8). The fog (and clear) colour from components in
    // 0..1: each times 255 as a float, rounded to the nearest integer (FISTP); A = 0xFF, packed as
    // ((r | 0xFFFFFF00) << 8 | g) << 8 | b; stored at +0x14 and set through slot 0.
    void SetFogColour(float r, float g, float b);
    // 00503A40 (RET 0x10, EAX 1; renderer_marker.cpp, A-U9). The selection marker under a selected
    // unit (00569E40's draw pass): a flat green arrow of half-transparent 0x8000FF00, 0.3 above
    // `point`, `size` long, pointing along -`axisY` (`axisX` across), drawn by the device's flat
    // geometry draw (004EA4A0) as six triangles of two vertex blocks.
    int DrawSelectionMarker(const Vector3* point, float size, const Vector3* axisX, const Vector3* axisY);
    // 00504A60 (virtual slot 22, RET 0xC, AL; renderer_gamma.cpp, A-U9; the options' brightness,
    // contrast and gamma, at start-up, from the options screen and with zeros at exit). Without a
    // device or a primary-surface gamma ramp (DDCAPS2_PRIMARYGAMMA) false. Keeps the three values,
    // then sets the primary surface's gamma ramp (calibrated when the driver can): each value
    // clamped to -1..1 and halved; level i / 255 raised to 1 / (1 + 5 g) (g > 0) or 1 / (1 + g / 2)
    // (g < 0), times the contrast's scale (1 + 4 |c|, inverted for c < 0) and centred, clamped to
    // 0..1, plus the brightness, clamped again, times 65280 (truncated) for all three channels.
    // True when SetGammaRamp succeeds.
    bool SetGamma(float brightness, float contrast, float gamma);
    // renderer_scene.cpp (A-U10).
    // 00503F40 (virtual slot 4, plain RET). 0 unless flags_ bit 0 is set. The frame statistics:
    // this frame's time since the last EndScene, the shortest (nonzero) and longest (under 1000 ms),
    // the total and count, and once a second's worth has passed the average over it; then ClearFrame
    // (slot 8), the device's counters cleared and IDirect3DDevice7::BeginScene. When a scene is
    // still open (D3DERR_SCENE_IN_SCENE) the batches are flushed, the scene ended and begun again;
    // other failures are fatal ("Can't begin scene." / "Can't end scene."). Returns 1.
    int BeginScene();
    // 005040A0 (virtual slot 5, plain RET). Flushes the device's two batches and ends the scene
    // (fatal on failure other than D3DERR_SCENE_NOT_IN_SCENE), then sets the debug panel's "FPS"
    // (1000 / frame time, "%g"), "num verts" and "num trias" ("%d") and remembers the frame's start.
    // Returns 1.
    int EndScene();
    // 00504860 (virtual slot 21, RET 0x10). Draws `texture` at (x, y) scaled by `scale` as a
    // pre-transformed strip of four vertices (z 0.9, white, specular 0xFF000000) through stage 0
    // (slot 11). 0 without a texture, else 1.
    int DrawTexture(CTexture* texture, float x, float y, float scale);

    std::uint32_t flags_;            // +0x04 bit 0: the software cursor option; bit 1 set by the creation (00500B70)
    CTransformPipeline* pipeline_;   // +0x08 counted
    void* graphicsProcessor_;        // +0x0C 2DintMMX.dll's GetGraphicsProcessor() (00500540)
    void* graphicsLibrary_;          // +0x10 the HMODULE of 2DintMMX.dll
    std::uint32_t fogColour_;        // +0x14 also the colour the frame is cleared with
    std::uint32_t colour_;           // +0x18 the draw colour (slot 7)
    // +0x1C the sun (counted; 00500800 sets it with the ambient colour, the client's lighting
    // 00566360 moves it every frame).
    CLight* sun_;
    CTextureDevice* device_;         // +0x20 the device object (007AF080), a counted holder (00504E20 assigns)
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
};
static_assert(offsetof(CSceneRenderer, pipeline_) == 0x08 && offsetof(CSceneRenderer, fogColour_) == 0x14 &&
              offsetof(CSceneRenderer, colour_) == 0x18 && offsetof(CSceneRenderer, device_) == 0x20);
static_assert(offsetof(CSceneRenderer, helper_) == 0x24 && offsetof(CSceneRenderer, frameTime_) == 0x34 &&
              offsetof(CSceneRenderer, lastFrameStart_) == 0x50 && offsetof(CSceneRenderer, windowFrames_) == 0x58);

// renderer_lifetime.cpp (A-U11). 00500960 (cdecl): the renderer (the global at 007AF060).
CSceneRenderer* __cdecl SceneRenderer();
// The device object's counted holder (CSceneRenderer::device_): 00504E00 releases it (the pointer
// left), 00504E20 (RET 4) takes `device` (one more reference) and releases the old one.
void __fastcall DeviceHolderRelease(CTextureDevice** holder);
void __fastcall DeviceHolderAssign(CTextureDevice** holder, void*, CTextureDevice* device);

// 004F1370. Viewport matrix for a screen rectangle {left, top, right, bottom}: scale and offset by
// the half extents, y flipped. Returns out.
Matrix4* __stdcall ViewportMatrix(Matrix4* out, const int* rect);
// 004F13F0. Shear along z that maps `dir` onto the z axis. Returns out.
Matrix4* __stdcall ShearMatrix(Matrix4* out, const Vector3* dir);
// 004F0E80. Perspective projection; `fov` in radians (full angle). Returns out.
Matrix4* __stdcall ProjectionMatrix(Matrix4* out, float fov, float aspect, float zNear, float zFar);

// Triangle index buffer of a figure part: 3 uint16 indices per triangle, an engine aligned array
// (lib::AlignedArray). Its SetSize (004F57B0, growBy -1 keeps the step) is AlignedArray::SetSize.
struct IndexTriangle {
    std::uint16_t index[3];
};
static_assert(sizeof(IndexTriangle) == 6);
using TriangleIndexArray = lib::AlignedArray<IndexTriangle>;

class CMappedResource;

// A node of CLinkFigureLoader's archive list (std::list<pair<string, CMMIOResource*>>).
struct LinkArchiveNode {
    LinkArchiveNode* next;
    LinkArchiveNode* prev;
    stlp::string name;           // +0x08
    CMappedResource* resource;   // +0x14 a CMMIOResource
};
static_assert(sizeof(LinkArchiveNode) == 0x18);

// A figure's own archive set (0x20 bytes, constructor 005A9A50): the .mod archive first, opened
// with the .bon / .anm ones by 005A9EB0. Found or made by 004FAB00 (FindFigureSet).
struct LinkFigureSet {
    CMappedResource* model;   // +0x00 the .mod archive
    std::uint8_t unknown_04[0x1C];
};
static_assert(sizeof(LinkFigureSet) == 0x20);

// Loads .lnk link files and the figures they name (vtable 0073D808, 7 slots; created at startup with
// figures.res and menus.res by 004F68C0, a Rebirth-patched function). A figure is a CLinkMesh of
// parts morphed by three coefficients (s, t, u); the loader keeps every mesh it made in a
// multimap by name (a counted reference each) and hands out a cached one whose coefficients are
// within 1e-5 (squared distance, the double at 0073B860) unless asked for a fresh one. Parts come
// from the figure's own archive set when there is one (004FAB00), else from the open archives
// as "<figure><part>.fig". link_loader.cpp (A-S7). The game's code stores the vtable (novtable).
class CLinkMesh;
class CTextureDevice;
class ITransformPipeline;
struct VertexPacket;

class __declspec(novtable) CLinkFigureLoader {
public:
    // 0: 004F6A30 (RET 4, returns this; A-S8). Destruct; freed when flags bit 0 is set.
    virtual void* DeleteThis(unsigned flags);
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
    LinkFigureSet* FindFigureSet(const stlp::string* name);
    // 004F9E50 (RET 0x18, AL; A-S8). Part `name` of `mesh` morphed again by s, t, u from its record
    // (the figure set's model, else "<mesh name><name>.fig" in the archives): false without one.
    bool RemorphPart(CLinkMesh* mesh, LinkFigureSet* set, const stlp::string* name, float s, float t, float u);
    // 004F9FD0 / 004FA670 (RET 0x18; link_figure.cpp). The record at *cursor (moved past the
    // vertices) morphed into the part's vertex packets (CLinkMesh::SetPartVertices) and its header's
    // bounds refitted. The FIG one reads every record as FIG8 (header, bounds and vertices).
    void RemorphFigPart(CLinkMesh* mesh, const stlp::string* name, const char** cursor, float s, float t, float u);
    void RemorphOldPart(CLinkMesh* mesh, const stlp::string* name, const char** cursor, float s, float t, float u);
    // 004F6BD0 (A-S8). Each archive closed (slot 3) and deleted, the archive list emptied, each
    // figure set destroyed (005A9C90) and freed, the device object released (the field cleared
    // first), the cache emptied (ClearCache's walk), then the containers' storage; the counted
    // root's vtable (0073C368) left.
    void Destruct();

    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x0073D808);

    int refCount_;                    // +0x04
    ITransformPipeline* pipeline_;    // +0x08 the renderer's (+0x24) +0xAC -> +0xC
    LinkArchiveNode* archives_;       // +0x0C list sentinel
    stlp::hash_map<stlp::string, LinkFigureSet*, stlp::StringHash> figureSets_;   // +0x10 by figure name
    CTextureDevice* renderer_;        // +0x24 the device object, counted; meshes take their pipeline from it
    stlp::map<stlp::string, CLinkMesh*> cache_;   // +0x28 a multimap of counted references
};
static_assert(offsetof(CLinkFigureLoader, pipeline_) == 0x08);
static_assert(offsetof(CLinkFigureLoader, archives_) == 0x0C);
static_assert(offsetof(CLinkFigureLoader, figureSets_) == 0x10);
static_assert(offsetof(CLinkFigureLoader, cache_) == 0x28);
static_assert(sizeof(CLinkFigureLoader) == 0x34);

// ---- Databases (006D0000-006E0000) -----------------------------------------------------------------

// A record of prints.db (terrain footprints). vtable 00744264. 0x28 bytes. The records are
// constructed and destroyed by the game's vector code.
struct PrintRecord {
    virtual void Serialize(ChunkArchive* ar);   // 0: 006D41D0

    stlp::string name;           // +0x04
    std::uint32_t values2[3];    // +0x10 chunk 2
    std::uint32_t values3[3];    // +0x1C chunk 3
};
static_assert(offsetof(PrintRecord, name) == 0x04);
static_assert(sizeof(PrintRecord) == 0x28);

// The second record type of prints.db. vtable 00744268. 0x34 bytes.
struct PrintRecord2 {
    virtual void Serialize(ChunkArchive* ar);   // 0: 006D4270

    stlp::string name;           // +0x04
    std::uint32_t values2[3];    // +0x10 chunk 2
    std::uint32_t values3[3];    // +0x1C chunk 3
    std::uint32_t values10[3];   // +0x28 chunk 10
};
static_assert(offsetof(PrintRecord2, name) == 0x04);
static_assert(sizeof(PrintRecord2) == 0x34);

// What chunks 2 and 3 of both record types hold: a print's alpha and its life and lifetime in
// ticks (the client world's +0x7A8 picks chunk 3).
struct PrintLook {
    float alpha;
    int life;
    int lifetime;
};
inline const PrintLook& LookOf(const PrintRecord& r, bool second) {
    return *reinterpret_cast<const PrintLook*>(second ? r.values3 : r.values2);
}
inline const PrintLook& LookOf(const PrintRecord2& r, bool second) {
    return *reinterpret_cast<const PrintLook*>(second ? r.values3 : r.values2);
}

// prints.db (007C89C8, loaded at startup by 006D0630), indexed by the ground's surface class
// (TerrainFields::SurfaceClass): chunk 1 blood and bloody footprints, chunk 2 footprints, chunk 3
// scorch marks; each table is followed by a hash_map by name (not used here).
struct PrintsDatabase {
    stlp::vector<PrintRecord> blood;          // +0x00
    std::uint8_t bloodByName[0x14];           // +0x0C
    stlp::vector<PrintRecord2> footprints;    // +0x20
    std::uint8_t footprintsByName[0x14];      // +0x2C
    stlp::vector<PrintRecord> scorch;         // +0x40
};
static_assert(offsetof(PrintsDatabase, footprints) == 0x20 && offsetof(PrintsDatabase, scorch) == 0x40);
inline const PrintsDatabase& ThePrintsDatabase() { return *reinterpret_cast<const PrintsDatabase*>(0x007C89C8); }

// ---- Water level changes (006D4400) ---------------------------------------------------------------

// A gradual change of one water level (script SetWaterLevel(index, level, time)): `value` moves
// by `step` per world update for `steps` updates.
struct TweenEntry {
    int key;        // water index
    int steps;      // updates left
    float value;
    float step;
};
static_assert(sizeof(TweenEntry) == 16);

// CArray<TweenEntry> of the world (at 007AFF60 + 0x70): new entries zero-filled, never
// constructed. RemoveAll 006D43E0 and SetSize 006D4840 are registered on the template
// (model_db.cpp); the rest are thiscall functions on the array.
using TweenArray = mfc::CArray<TweenEntry>;
void __fastcall TweenSetTarget(TweenArray* tweens, void*, int key, float target, int steps);   // 006D4410 (via 00593FA0)
bool __fastcall TweenTick(TweenArray* tweens);              // 006D44E0: true while any entry moves
void __fastcall TweenSave(const TweenArray* tweens, void*, ScriptStream* stream);   // 006D4570 (006D47E0 identical)
// 006D45D0. Reads the array and applies each entry's value to `world` (006CC7F0).
void __fastcall TweenLoad(TweenArray* tweens, void*, ScriptStream* stream, void* world);
// 006D4660. Reads the array only (the inlined SetSize of another CArray instantiation).
void __fastcall TweenLoadEntries(TweenArray* tweens, void*, ScriptStream* stream);
// The read part of both loads: the count (the original reads it into a slot that still holds
// `unread` if the stream writes nothing), SetSize, then the entries.
void TweenReadEntries(TweenArray& tweens, ScriptStream* stream, int unread);

// model_names.cpp (A-S4). 004DBCA0 (__fastcall: ECX out, EDX cursor): a length-prefixed name (a
// four-byte length, then the characters) at *cursor into `out` (emptied for length 0), the
// cursor moved past it (the skeleton links' child / parent names, figure_links.cpp).
void __fastcall ReadPrefixedName(mfc::CString* out, const std::uint8_t** cursor);
