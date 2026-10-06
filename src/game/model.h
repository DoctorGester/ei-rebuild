#pragma once

// Model and resource loading: the 3D library side of figures (004F0000-00500000) and the
// databases read at startup (prints.db, acks.db; 006D0000-006E0000). See docs/notes/models.md.
// Names are provisional.

#include "chunk_archive.h"
#include "maths.h"
#include "script.h"   // MfcArray, ScriptStream

#include "lib/stlp.h"

#include <cstddef>
#include <cstdint>

// ---- 3D library (004F0000-00500000) ---------------------------------------------------------------

// Row-major 4x4 matrix of the transform library.
struct Matrix4 {
    float m[16];
};
static_assert(sizeof(Matrix4) == 64);

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

// The transform pipeline of the 3D library (the object behind GetTLPipeline). Only the matrix
// stacks are known.
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

    std::uint8_t unknown_00[0x14];
    MatrixStack stacks_[11];    // +0x14 (models.md: stacks 0-2, 5-10 are in use)
};
static_assert(offsetof(CTransformPipeline, stacks_) == 0x14);

// 004F1370. Viewport matrix for a screen rectangle {left, top, right, bottom}: scale and offset by
// the half extents, y flipped. Returns out.
Matrix4* __stdcall ViewportMatrix(Matrix4* out, const int* rect);
// 004F13F0. Shear along z that maps `dir` onto the z axis. Returns out.
Matrix4* __stdcall ShearMatrix(Matrix4* out, const Vector3* dir);
// 004F0E80. Perspective projection; `fov` in radians (full angle). Returns out.
Matrix4* __stdcall ProjectionMatrix(Matrix4* out, float fov, float aspect, float zNear, float zFar);

// Triangle index buffer of a figure part: 3 uint16 indices per triangle, data 32-byte aligned with
// the operator_new block stored just before it. 16 bytes.
struct TriangleIndexArray {
    std::uint16_t* data;   // +0x00
    int size;              // +0x04 triangles
    int capacity;          // +0x08
    int growBy;            // +0x0C 0 = size / 8 clamped to 4..1024

    // 004F57B0. Resizes to `count` triangles (growBy -1 keeps the current step), keeping the
    // contents; 0 frees the buffer.
    void Resize(int count, int newGrowBy);
};
static_assert(sizeof(TriangleIndexArray) == 16);

// A node of CLinkFigureLoader's archive list (std::list<pair<string, CMMIOResource*>>).
struct LinkArchiveNode {
    LinkArchiveNode* next;
    LinkArchiveNode* prev;
    stlp::string name;      // +0x08
    void* resource;         // +0x14 CMMIOResource
};
static_assert(sizeof(LinkArchiveNode) == 0x18);

// Loads .lnk link files and the figures they name (vtable 0073D808; created at startup with
// figures.res and menus.res). Only the archive list is known.
class CLinkFigureLoader {
public:
    // 004F6D30. Opens `path` as a memory-mapped archive and appends it to the list, unless an
    // archive of that exact name is already open. Returns 1 on success or when already open.
    int AddArchive(const stlp::string* path);

    std::uint8_t unknown_00[0x0C];
    LinkArchiveNode* archives_;   // +0x0C list sentinel
};
static_assert(offsetof(CLinkFigureLoader, archives_) == 0x0C);

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

// CArray<TweenEntry> of the world (at 007AFF60 + 0x70). MFC SetSize semantics; new entries are
// zero-filled, never constructed.
struct TweenArray : MfcArray<TweenEntry> {
    void RemoveAll();                                  // 006D43E0
    void SetSize(int count, int newGrowBy);            // 006D4840
    void SetTarget(int key, float target, int steps);  // 006D4410 (reached via 00593FA0)
    bool Tick();                                       // 006D44E0: true while any entry moves
    void Save(ScriptStream* stream) const;             // 006D4570 (006D47E0 is identical)
    // 006D45D0. Reads the array and applies each entry's value to `world` (006CC7F0).
    void Load(ScriptStream* stream, void* world);
    // 006D4660. Reads the array only (the inlined SetSize of another CArray instantiation).
    void LoadEntries(ScriptStream* stream);
    // The read part of both loads: the count (the original reads it into a slot that still holds
    // `unread` if the stream writes nothing), SetSize, then the entries.
    void ReadEntries(ScriptStream* stream, int unread);
};
static_assert(sizeof(TweenArray) == 0x14);

// model_names.cpp (A-S4). 004DBCA0 (__fastcall: ECX out, EDX cursor): a length-prefixed name (a
// four-byte length, then the characters) at *cursor into `out` (emptied for length 0), the
// cursor moved past it (the skeleton links' child / parent names, figure_links.cpp).
void __fastcall ReadPrefixedName(mfc::CString* out, const std::uint8_t** cursor);
