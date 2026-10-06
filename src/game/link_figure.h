#pragma once

// The 3D library's figure meshes and their loading from .lnk link files (004F2650-004FBD80). A
// CLinkFigureLoader (model.h; one per world view, 00563710) loads a figure by name: its .lnk file
// lists the parts, each a FIG / FIG8 (or older headerless) record in figures.res / menus.res, morphed
// by three coefficients and appended to one CLinkMesh. See docs/notes/models.md, "Link figures".
// Names provisional.

#include "figure_mesh.h"   // AlignedArray, VertexPacket, NormalPacket, TexCoord, MeshCorner, FigHeader
#include "model.h"         // ClipBounds, TriangleIndexArray, CLinkFigureLoader

#include "lib/stlp.h"

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
    std::uint8_t unknown_34[0x50 - 0x34];
};
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

// An STLport map<string, T> as the mesh holds it: the tree header node, the element count and the
// (empty) comparison object. Its members are STLport instantiations (config/library.csv) reached
// through their addresses.
struct LinkStringMap {
    void* header;              // +0x00
    std::uint32_t count;       // +0x04
    std::uint8_t compare;      // +0x08
    std::uint8_t unknown_09[3];
};
static_assert(sizeof(LinkStringMap) == 0x0C);

// A figure's mesh (vtable 0073D790; base 0073D7CC; constructor 004F2C00, destructor 004F2E40).
// The parts' elements are concatenated in the arrays; 0xB0 bytes.
class CLinkMesh {
public:
    // 004F30F0 (RET 0x34). Appends a part: false (0) when `name` is already a part; otherwise each
    // array grows by its count and gets the elements, and the part's ranges are recorded. Returns 1.
    int AddPart(const stlp::string* name, const VertexPacket* vertices, int vertexCount, const NormalPacket* normals,
                int normalCount, const TexCoord* texCoords, int texCoordCount, const MeshCorner* corners,
                int cornerCount, const std::uint32_t* words, int wordCount, const std::uint16_t* indices,
                int indexCount);

    void* vtable_;                    // +0x00
    int refCount_;                    // +0x04
    stlp::string name_;               // +0x08
    LinkStringMap headers_;           // +0x14 map<string, LinkPartHeader>
    std::uint8_t unknown_20[0x10];
    float morph_[3];                  // +0x30 the coefficients the figure was loaded with
    AlignedArray<VertexPacket> vertices_;   // +0x3C
    AlignedArray<NormalPacket> normals_;    // +0x4C
    AlignedArray<TexCoord> texCoords_;      // +0x5C
    TriangleIndexArray corners_;            // +0x6C 6-byte triangle corners (MeshCorner)
    AlignedArray<std::uint32_t> words_;     // +0x7C
    AlignedArray<std::uint16_t> indices_;   // +0x8C
    LinkStringMap ranges_;            // +0x9C map<string, LinkPartRanges>
    CLinkFigureLoader* loader_;       // +0xA8
    ITransformPipeline* pipeline_;    // +0xAC
};
static_assert(offsetof(CLinkMesh, headers_) == 0x14);
static_assert(offsetof(CLinkMesh, morph_) == 0x30);
static_assert(offsetof(CLinkMesh, vertices_) == 0x3C);
static_assert(offsetof(CLinkMesh, corners_) == 0x6C);
static_assert(offsetof(CLinkMesh, ranges_) == 0x9C);
static_assert(offsetof(CLinkMesh, pipeline_) == 0xAC);
static_assert(sizeof(CLinkMesh) == 0xB0);

// The record of a part in a resource archive (as the loaders read it; `cursor` moves past it):
// 004F7F00 the FIG / FIG8 record, 004F87D0 the older one without a header.
// Both are members of CLinkFigureLoader (model.h): LoadFigPart, LoadOldPart.
