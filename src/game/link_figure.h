#pragma once

// The 3D library's figure meshes and their loading from .lnk link files (004F2650-004FBD80). A
// CLinkFigureLoader (model.h; one per world view, 00563710) loads a figure by name: its .lnk file
// lists the parts, each a FIG / FIG8 (or older headerless) record in figures.res / menus.res, morphed
// by three coefficients and appended to one CLinkMesh. See docs/notes/models.md, "Link figures".
// Names provisional.

#include "figure_mesh.h"   // AlignedArray, VertexPacket, NormalPacket, TexCoord, MeshCorner, FigHeader
#include "model.h"         // ClipBounds, TriangleIndexArray, CLinkFigureLoader

#include "lib/stlp.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>

// The transform pipeline of 3DfpFPU.dll (GetTLPipeline), as far as the loaders call it.
class ITransformPipeline {
public:
    // Slots 0-6 are not called from this code.
    virtual void Unknown0();
    virtual void Unknown1();
    virtual void Unknown2();
    virtual void Unknown3();
    virtual void Unknown4();
    virtual void Unknown5();
    virtual void Unknown6();
    // 7: fits `bounds` around `count` vertex packets.
    virtual void FitBounds(ClipBounds* bounds, const VertexPacket* vertices, int count);
};

// A part's header in the mesh's map +0x14 (map<string, LinkPartHeader>; 0x50 bytes, zeroed when
// the map inserts it).
struct LinkPartHeader {
    std::uint32_t unknown_00;   // +0x00 the second dword after the FIG header
    std::uint32_t unknown_04;   // +0x04 the first one
    ClipBounds bounds;          // +0x08 fitted to the part's morphed vertices
    std::uint8_t unknown_34[0x0C];
    RECT screen;                // +0x40 where the part was last drawn (empty: not drawn)
};
static_assert(offsetof(LinkPartHeader, screen) == 0x40);
static_assert(offsetof(LinkPartHeader, bounds) == 0x08);
static_assert(sizeof(LinkPartHeader) == 0x50);

// Where a part's elements lie in the mesh's arrays (map +0x9C: map<string, LinkPartRanges>).
struct LinkPartRanges {
    struct Range {
        int start;
        int count;
    };
    Range vertices;   // +0x00
    Range normals;    // +0x08
    Range texCoords;  // +0x10
    Range corners;    // +0x18
    Range words;      // +0x20
    Range indices;    // +0x28
};
static_assert(sizeof(LinkPartRanges) == 0x30);

// A node of an STLport map<string, T>: the tree links, the key, the value at +0x1C. The header
// node has the same links: parent = root, left = leftmost, right = rightmost.
struct LinkMapNode {
    int color;              // +0x00
    LinkMapNode* parent;    // +0x04
    LinkMapNode* left;      // +0x08
    LinkMapNode* right;     // +0x0C
    stlp::string key;       // +0x10

    // The in-order successor (STLport's _Rb_tree_base_iterator::_M_increment, 0043DBE0).
    LinkMapNode* Next();
    template <typename V>
    V& Value() {
        return *reinterpret_cast<V*>(this + 1);
    }
};
static_assert(sizeof(LinkMapNode) == 0x1C);

// An STLport map<string, T> as the mesh holds it: the tree header node, the element count and the
// (empty) comparison object. Its members are STLport instantiations (config/library.csv) reached
// through their addresses.
struct LinkStringMap {
    LinkMapNode* header;       // +0x00
    std::uint32_t count;       // +0x04
    std::uint8_t compare;      // +0x08
    std::uint8_t unknown_09[3];
};
static_assert(sizeof(LinkStringMap) == 0x0C);

// A figure's mesh (vtable 0073D790; base 0073D7CC; constructor 004F2C00, destructor 004F2E40).
// The parts' elements are concatenated in the arrays; 0xB0 bytes. The game's constructor stores the
// vtable (novtable). Slots 7-14 (004F3870 ... 004F4850) draw parts through the device and are left to
// the renderer.
class __declspec(novtable) CLinkMesh {
public:
    virtual void* DeleteThis(unsigned flags);   // 0: 004F2E20 (deleting destructor)
    // 1: 004F45E0. screenRect_ = the union of the parts' non-empty screen rectangles (inverted
    // +-2000000000 when there is none).
    virtual void UpdateScreenRect();
    // 2: 004F3040 (RET 4, AL). Whether `name` is a part.
    virtual bool HasPart(const stlp::string* name);
    // 3: 004F2FB0 (RET 4). Appends the part names, in map order, to `out`.
    virtual void CollectPartNames(stlp::vector<stlp::string>* out);
    // 4: 004F30D0 (RET 4, AL). The loader (004F7480) loads part `name` into this mesh.
    virtual bool LoadPart(const stlp::string* name);
    // 5: 004F3530 (RET 4, AL). Removes part `name`: false when there is none. The later elements of
    // each array move down over its elements; parts whose index range starts after the removed
    // one's get every start lowered by the removed part's starts (sic: not by its counts).
    virtual bool RemovePart(const stlp::string* name);
    // 6: 004F3780 (RET 0x18, AL). Part `name`'s first vertex packet, corner and index and its vertex
    // and index counts; false (nothing written) when there is no such part.
    virtual bool GetPartData(const stlp::string* name, VertexPacket** vertices, MeshCorner** corners,
                             std::uint16_t** indices, int* vertexCount, int* indexCount);

    // 004F2F90 (RET 0xC). Sets the morph coefficients.
    void SetMorph(float s, float t, float u);

    // 004F30F0 (RET 0x34, AL). Appends a part: false when `name` is already a part; otherwise each
    // array grows by its count and gets the elements, and the part's ranges are recorded; true.
    bool AddPart(const stlp::string* name, const VertexPacket* vertices, int vertexCount, const NormalPacket* normals,
                int normalCount, const TexCoord* texCoords, int texCoordCount, const MeshCorner* corners,
                int cornerCount, const std::uint32_t* words, int wordCount, const std::uint16_t* indices,
                int indexCount);

    int refCount_;                    // +0x04
    stlp::string name_;               // +0x08
    LinkStringMap headers_;           // +0x14 map<string, LinkPartHeader>
    RECT screenRect_;                 // +0x20
    float morph_[3];                  // +0x30 the coefficients the figure was loaded with
    lib::AlignedArray<VertexPacket> vertices_;   // +0x3C
    lib::AlignedArray<NormalPacket> normals_;    // +0x4C
    lib::AlignedArray<TexCoord> texCoords_;      // +0x5C
    TriangleIndexArray corners_;            // +0x6C 6-byte triangle corners (MeshCorner)
    lib::AlignedArray<std::uint32_t> words_;     // +0x7C
    lib::AlignedArray<std::uint16_t> indices_;   // +0x8C
    LinkStringMap ranges_;            // +0x9C map<string, LinkPartRanges>
    CLinkFigureLoader* loader_;       // +0xA8
    ITransformPipeline* pipeline_;    // +0xAC
};
static_assert(offsetof(CLinkMesh, refCount_) == 0x04);
static_assert(offsetof(CLinkMesh, headers_) == 0x14);
static_assert(offsetof(CLinkMesh, screenRect_) == 0x20);
static_assert(offsetof(CLinkMesh, morph_) == 0x30);
static_assert(offsetof(CLinkMesh, vertices_) == 0x3C);
static_assert(offsetof(CLinkMesh, corners_) == 0x6C);
static_assert(offsetof(CLinkMesh, ranges_) == 0x9C);
static_assert(offsetof(CLinkMesh, pipeline_) == 0xAC);
static_assert(sizeof(CLinkMesh) == 0xB0);

// The record of a part in a resource archive (as the loaders read it; `cursor` moves past it):
// 004F7F00 the FIG / FIG8 record, 004F87D0 the older one without a header.
// Both are members of CLinkFigureLoader (model.h): LoadFigPart, LoadOldPart.
