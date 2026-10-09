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
    float center[3];       // +0x04
    float radius;          // +0x10 half the side of the cube around the centre
    float boxMin[3];       // +0x14 box corners, relative to the centre
    float boxMax[3];       // +0x20

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
    // Stack 2 holds the matrix ClipTest projects with.
    static constexpr int kClipStack = 2;

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
    // The top of stack `index` (matrices[count - 1], as the original computes it even when empty).
    const Matrix4* Top(int index) const { return stacks_[index].matrices + (stacks_[index].count - 1); }

    std::uint8_t unknown_00[0x0C];
    ITransformPipeline* transform_;   // +0x0C 3DfpFPU.dll's transform object (projects and clips vertices)
    std::uint8_t unknown_10[4];
    MatrixStack stacks_[11];    // +0x14 (models.md: stacks 0-2, 5-10 are in use)
};
static_assert(offsetof(CTransformPipeline, stacks_) == 0x14 && offsetof(CTransformPipeline, transform_) == 0x0C);

// The scene's pipeline (the global pointer at 007AF068; world_client_view.cpp calls it the render
// device): stacks 1 and 2 take the scene matrices (004D9B80), 5 the world draw's, 9 the views.
inline CTransformPipeline* ScenePipeline() { return *reinterpret_cast<CTransformPipeline* const*>(0x007AF068); }

struct tagRECT;
class CTextureDevice;

// The scene renderer (the global at 007AF060, vtable 0073D93C, 26 slots; 00500960 returns it).
// Only the slots rewritten code calls are named. The game's code stores the vtable (novtable).
class __declspec(novtable) CSceneRenderer {
public:
    // 0: 00501AB0. Sets the device's fog colour render state (D3DRENDERSTATE_FOGCOLOR) to `colour`.
    virtual void SetFogColourState(std::uint32_t colour);
    virtual void Unknown1();
    virtual void Unknown2();
    virtual void Unknown3();
    virtual void Unknown4();
    virtual void Unknown5();
    virtual void Unknown6();
    // 7: 00501CC0 (RET 4). Sets the colour the next draws are tinted with (kept at +0x18).
    virtual void SetColour(std::uint32_t colour);
    virtual void Unknown8();
    virtual void Unknown9();
    virtual void Unknown10();
    // 11: 00501AF0 (RET 8, EAX 1). Binds `texture` (a texture object, or null) to stage `stage`.
    virtual int SetTexture(int stage, void* texture);
    // 12: 00501BB0 (RET 0x10; renderer_triangles.cpp). Draws pre-transformed vertices (D3DTLVERTEX,
    // FVF 0x1C4) as an indexed triangle list with the current state. Returns 1.
    virtual int DrawTriangles(const void* vertices, int vertexCount, const std::uint16_t* indices, unsigned indexCount);
    virtual void Unknown13();
    // 14: 00501D00 (RET 0x18). Draws `figure` through its drawer (+0x94, 005A7640) unless it is
    // hidden (flags +8 bit 0x80); `alpha` reaches the mesh draw, `screen` (or null) gets the
    // rectangle drawn, `material` (null, or an interface sprite's material) is passed on. The second
    // and third arguments are not read there.
    virtual int DrawFigure(void* figure, const void* unknownA, const void* unknownB, void* material, int alpha,
                           tagRECT* screen);
    virtual void Unknown15();
    virtual void Unknown16();
    virtual void Unknown17();
    virtual void Unknown18();
    // 19: 005041F0 (RET 0x18; renderer_rect.cpp). A black rectangle (left, top, right, bottom in
    // screen pixels) of `alpha` over the current blend state, with `outline` a white frame. Returns 1.
    virtual int DrawRectangle(int left, int top, int right, int bottom, int alpha, int outline);

    // 004BA120 (RET 0xC; scene_renderer.cpp, A-S8). The fog (and clear) colour from components in
    // 0..1: each times 255 as a float, rounded to the nearest integer (FISTP); A = 0xFF, packed as
    // ((r | 0xFFFFFF00) << 8 | g) << 8 | b; stored at +0x14 and set through slot 0.
    void SetFogColour(float r, float g, float b);

    std::uint8_t unknown_04[4];
    CTransformPipeline* pipeline_;   // +0x08
    std::uint8_t unknown_0C[8];
    std::uint32_t fogColour_;        // +0x14 also the colour the frame is cleared with
    std::uint32_t colour_;           // +0x18 the draw colour (slot 7)
    std::uint8_t unknown_1C[4];
    CTextureDevice* device_;         // +0x20 the device object (007AF080)
};
static_assert(offsetof(CSceneRenderer, pipeline_) == 0x08 && offsetof(CSceneRenderer, fogColour_) == 0x14 &&
              offsetof(CSceneRenderer, colour_) == 0x18 && offsetof(CSceneRenderer, device_) == 0x20);

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
