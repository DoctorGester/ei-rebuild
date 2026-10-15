#pragma once

// The scene file loader of CFakeUnit (00471B10): reads a binary .sc* scene (meshes with their
// vertex animation, the node hierarchy, helpers) into the arrays of the figure being built.
// Called by 00446C10 (`LoadFakeUnitScene`, the CFakeUnit slot 62 loader) with the opened CFile.
// Names are provisional; see docs/notes/objects.md ("Facts").
//
// File layout (all integers little endian, no padding):
//   header (0x14): u32 version (0x1468FCCA: records carry an extra normal list), u32 record count,
//                  u32 skipped record count, u32 helper count, u16 flags (returned to the caller)
//   records (0x2C header each, one mesh and one node per record), then the skipped records (same
//   format, read and dropped), then the helpers (0x3E header each).

#include "core_transform.h"
#include "engine_object.h"
#include "material.h"

#include "../lib/containers.h"
#include "../lib/mfc.h"
#include "../lib/vecmath.h"

#include <cstddef>
#include <cstdint>

class CAnimationNode;   // figure.h
class CFigureMesh;      // figure_mesh.h
class CObject3D;        // map_object_figures.h

namespace scene {

// An MFC CArray of records (vtable, data, size, capacity, grow step: 0 is an eighth of the size, 4..1024)
// seen as bytes: the scene's mesh, node, helper and figure arrays, whose records are reached through
// byte offsets. Resize them only through a typed instantiation (scene_loader.cpp); SetSize here would
// move one-byte elements.
using VtArray = mfc::CArray<char, mfc::RawElements<char>>;

// A record the scene code only reaches through byte offsets.
template <std::size_t K>
struct Rec {
    std::uint8_t bytes[K];
};

// ConstructElements / DestructElements(elements, count) of an instantiation the original calls out
// of line (`Unmake` null: nothing is destroyed).
template <typename T, void(__stdcall* Make)(void*, int), void(__stdcall* Unmake)(void*, int) = nullptr>
struct OriginalElements {
    static void Construct(T* elements, int count) { Make(elements, count); }
    static void Destruct(T* elements, int count) {
        if constexpr (Unmake != nullptr) Unmake(elements, count);
    }
};

// The caller's untyped array resized through the typed instantiation of the same layout.
template <typename Records>
Records& Typed(VtArray& a) {
    static_assert(sizeof(Records) == sizeof(VtArray));
    return reinterpret_cast<Records&>(a);
}

// The "aligned" arrays (vertex lists, 32-byte aligned blocks with the raw pointer stored before the
// data; lib::AlignedArray), per element type. Positions and normals are kept as the bits that were
// read: the loader copies some of them through the FPU, as the original does.
struct PointBits {
    std::uint32_t v[3];
};
struct UvBits {
    std::uint32_t v[2];
};
using PointBuffer = lib::AlignedArray<PointBits>;
using UvBuffer = lib::AlignedArray<UvBits>;
struct Block64 {
    std::uint8_t bytes[64];   // a matrix, or four normals / positions in lanes
};
using BlockBuffer = lib::AlignedArray<Block64>;
using DwordBuffer = lib::AlignedArray<std::uint32_t>;

// MFC CMap<DWORD, DWORD, DWORD, DWORD> (0x1C bytes) and its association (0x10 bytes).
using DwordMap = mfc::CMap<std::uint32_t, std::uint32_t>;   // vtable 0073C818, hashSize 17, blockSize 10
using MapAssoc = DwordMap::Assoc;
static_assert(sizeof(DwordMap) == 0x1C && sizeof(MapAssoc) == 0x10);

// One face of a record (0x1C): three position indices, three texture-coordinate indices, one more
// dword the loader does not use.
struct FileFace {
    std::uint32_t position[3];
    std::uint32_t texture[3];
    std::uint32_t unused_18;
};
static_assert(sizeof(FileFace) == 0x1C);

// A mesh face as the mesh keeps it (0x34). The loader copies the file face's six indices to
// +00..+14 and its three position indices again to +18..+20, then replaces both index triples at
// +00 and +0C by the merged vertex of each corner. +24..+30 are not written.
struct MeshFace {
    // face_normal.cpp (A-S4). 0049AEC0 (__thiscall, RET 4): the face's plane from the points
    // `positions` (an array of 3-float points) holds: the edges from vertex 1 to vertex 2 and to
    // vertex 0 are normalised unless shorter than 1e-5, crossed, the result normalised (a zero
    // length gives the vector at 007A9C98), then the distance.
    void ComputeNormal(const PointBuffer* positions);
    std::uint32_t vertex[3];       // +00
    std::uint32_t vertex2[3];      // +0C
    std::uint32_t position[3];     // +18 the file's position indices
    Vector3 normal;                // +24 unit normal of the plane (0049AEC0)
    float distance;                // +30 the plane's distance: dot(position[1]'s point, normal)
};
static_assert(sizeof(MeshFace) == 0x34);
using FaceBuffer = lib::AlignedArray<MeshFace>;

// A material group (0x18): its name and the faces it uses.
struct FaceGroup {
    // 00474C50 (RET 4; S13, scene_loader.cpp; the compiler's operator=): the name, then the faces
    // (CDWordArray::Copy: SetSize to the source's, the indices copied).
    void Assign(const FaceGroup* from);
    // 00454DB0 (ConstructElements, __stdcall RET 8; S7a): the zero fill, then each name empty and
    // the faces' CDWordArray constructor.
    static void __stdcall ConstructElements(FaceGroup* groups, int count);
    // The destructor (inlined by 00454CE0; S7a): the faces emptied (SetSize 0) and the name emptied,
    // then both destroyed.
    void Destruct();

    mfc::CString name;        // +00
    mfc::CArray<std::uint32_t> faces;   // +04 CDWordArray of face indices
};
static_assert(sizeof(FaceGroup) == 0x18);
// 00454CE0 (S7a): CArray<FaceGroup>'s destructor (vtable 0073C2BC): each group destroyed, the
// storage freed.
void __fastcall FaceGroupArrayDestruct(mfc::CArray<FaceGroup>* groups);

// Mesh (element of the caller's +0x544 array, 0x18C bytes, constructed by 004545C0).
namespace mesh {
constexpr std::size_t kSize = 0x18C;
constexpr std::size_t kName = 0x94;        // CString
constexpr std::size_t kPositions = 0xD8;   // aligned array of 12-byte points
constexpr std::size_t kTexCoords = 0xE8;   // aligned array of 8-byte (u, v)
constexpr std::size_t kFaces = 0x138;      // aligned array of MeshFace
constexpr std::size_t kGroups = 0x148;     // CArray of FaceGroup (vtable at +0x148)
} // namespace mesh

// Node (element of the caller's local array, 0x120 bytes, constructed by 00454F80).
namespace node {
constexpr std::size_t kSize = 0x120;
constexpr std::size_t kName = 0x04;        // CString
constexpr std::size_t kParent = 0x08;      // CString
constexpr std::size_t kOffset = 0x54;      // 3 floats
constexpr std::size_t kRotKeys = 0xA8;     // CArray of 0x14 records: u16 frame, (+4 kept), 3 floats at +8
constexpr std::size_t kPosKeys = 0xBC;     // CArray of 0x18 records: u16 frame, +8 dword, 3 floats at +0xC
constexpr std::size_t kMorphKeys = 0xD0;   // CArray of 0x14 records: u16 frame, aligned point array at +4
constexpr std::size_t kNormals = 0xE4;     // CArray of 12-byte records (version 0x1468FCCA only)
} // namespace node

// The node's key records (the loader fills them; the frame is read as a signed short).
struct NodeRotationKey {        // 0x14: the frame, (+4 kept), a vector
    std::int16_t frame;
    std::uint16_t unused_02;
    std::uint32_t unused_04;
    Vector3 value;              // +0x08 (a pivot: the skeleton's translation keys)
};
static_assert(sizeof(NodeRotationKey) == 0x14);
struct NodePositionKey {        // 0x18: the frame, an angle and an axis (a rotation)
    std::int16_t frame;
    std::uint16_t unused_02;
    std::uint32_t unused_04;
    float angle;                // +0x08
    Vector3 axis;               // +0x0C
};
static_assert(sizeof(NodePositionKey) == 0x18);
struct NodeMorphKey {           // 0x14: the frame and the mesh's points at it
    std::int16_t frame;
    std::uint16_t unused_02;
    lib::AlignedArray<Vector3> points;   // +0x04
};
static_assert(sizeof(NodeMorphKey) == 0x14);
// The node's morph key array constructs (00454F30) and destroys (00453700) its keys out of line
// (scene_loader.cpp).
struct NodeMorphKeyElements {
    static void Construct(NodeMorphKey* keys, int count);
    static void Destruct(NodeMorphKey* keys, int count);
};

// A node's frame record (40 bytes; BuildSkeleton puts the pivot and rotation in the first).
struct NodeFrame {
    std::uint32_t unused_00[3];
    Vector3 pivot;                      // +0x0C
    Quaternion rotation;                // +0x18
};
static_assert(sizeof(NodeFrame) == 40);

// A node (the node:: offsets above). The loader fills the key arrays (their instantiations zero
// new elements, the morph keys' construct them out of line); scene_skeleton.cpp names the rest.
struct SceneNode {
    // 004543E0 (S7a). The destructor: the arrays last to first (each its vtable back, then its
    // storage freed; the morph keys' point blocks first), then the parent's and the node's names.
    void Destruct();
    // The constructor body ConstructElements 00454F80 inlines after its zero fill (S6b,
    // scene_loader.cpp): the names empty, the four rotations identity, each array empty with its
    // vtable.
    void Construct();

    std::uint32_t unused_000;
    mfc::CString name;                  // +0x004
    mfc::CString parent;                // +0x008
    std::uint32_t unused_00C;
    std::uint32_t spare010;             // +0x010 cleared by BuildSkeleton; nothing reads it
    std::uint32_t spare014;             // +0x014 cleared by BuildSkeleton; nothing reads it
    std::uint8_t unused_018[0x30 - 0x18];
    Quaternion spareRotation030;        // +0x030 identity from the constructor; nothing reads it
    Quaternion spareRotation040;        // +0x040 identity from the constructor; nothing reads it
    std::uint32_t unused_050;
    Vector3 offset;                     // +0x054 the pivot (must be 0, 0, 0: BuildBone)
    Vector3 axis;                       // +0x060 the first position key's axis
    float angle;                        // +0x06C its angle
    Vector3 pivot;                      // +0x070 the first rotation key's vector
    Quaternion rotation;                // +0x07C
    Vector3 restPivot;                  // +0x08C
    Quaternion restRotation;            // +0x098
    mfc::CArray<NodeRotationKey> rotationKeys;                  // +0x0A8
    mfc::CArray<NodePositionKey> positionKeys;                  // +0x0BC
    mfc::CArray<NodeMorphKey, NodeMorphKeyElements> morphKeys;  // +0x0D0
    mfc::CArray<Vector3> normals;                               // +0x0E4 (version 0x1468FCCA only)
    // +0x0F8 NodeFrame records (elements constructed by 00453730); the loader copies them to the
    // figures' bone links.
    VtArray frames;
    mfc::CArray<SceneNode*, mfc::RawElements<SceneNode*>> children;                 // +0x10C
};
static_assert(offsetof(SceneNode, name) == node::kName);
static_assert(offsetof(SceneNode, spareRotation030) == 0x30 && offsetof(SceneNode, spareRotation040) == 0x40);
static_assert(offsetof(SceneNode, offset) == node::kOffset);
static_assert(offsetof(SceneNode, restPivot) == 0x8C);
static_assert(offsetof(SceneNode, restRotation) == 0x98);
static_assert(offsetof(SceneNode, rotationKeys) == node::kRotKeys);
static_assert(offsetof(SceneNode, positionKeys) == node::kPosKeys);
static_assert(offsetof(SceneNode, morphKeys) == node::kMorphKeys);
static_assert(offsetof(SceneNode, normals) == node::kNormals);
static_assert(offsetof(SceneNode, children) == 0x10C);
static_assert(sizeof(SceneNode) == node::kSize);

// A distinct face corner of a mesh (6 bytes; 0044CFF0 builds the table): merged vertex, file
// position and texture vertex.
struct MeshCornerKey {
    std::uint16_t vertex, position, texture;
};
static_assert(sizeof(MeshCornerKey) == 6);

// A mesh (mesh:: offsets): a transform object (an engine object whose slot 7, GetName, gives its
// name; vtable 0073C274). Only the members the scene code uses are declared.
struct SceneMesh : CTransformObject {
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x0073C274);
    // The constructor body ConstructElements 004545C0 inlines after its zero fill (S6b,
    // scene_loader.cpp): the transform object's (00453400), the name empty, the groups' array
    // empty (00454A50), then the mesh's vtable.
    void Construct();

    std::uint8_t unused_054[0x94 - 0x54];
    mfc::CString name;                  // +0x094
    std::uint8_t unused_098[0xA8 - 0x98];
    // +0x0A8 / +0x0B8 the positions and the normals packed four to a 64-byte block (x[4], y[4],
    // z[4], w[4]; 0044E3B0 / 0044DCF0); the figures copy them (CFigureMesh +0x1C0 / +0x1D0).
    BlockBuffer positionBlocks;
    BlockBuffer normalBlocks;
    std::uint8_t unused_0C8[0xD8 - 0xC8];
    PointBuffer positions;              // +0x0D8 the file's bits
    UvBuffer texCoords;                 // +0x0E8
    PointBuffer normals;                // +0x0F8 one per position (0044DCF0)
    // +0x108 / +0x118 / +0x128 (0044CFF0): the distinct corners (sorted), the distinct (vertex,
    // position) word pairs (sorted, padded to a multiple of four) and each face corner's index in
    // the corner table (three words per face).
    lib::AlignedArray<MeshCornerKey> corners;
    DwordBuffer pairs;
    lib::AlignedArray<std::uint16_t> cornerIndices;
    FaceBuffer faces;                   // +0x138
    mfc::CArray<FaceGroup> groups;      // +0x148
    // +0x15C the bounds (0044E8A0; the figure copies them to its +0x160): the centre, the box
    // corners relative to it and the radius.
    Vector3 center;
    Vector3 boxMin;                     // +0x168
    Vector3 boxMax;                     // +0x174
    float radius;                       // +0x180
    // +0x184 / +0x188 the material of the mesh's last group (0044A2A0 from its name; 0044B980);
    // the figure copies them to its +0x158 / +0x15C.
    int materialValue;
    int materialType;
};
static_assert(offsetof(SceneMesh, name) == mesh::kName);
static_assert(offsetof(SceneMesh, positions) == mesh::kPositions);
static_assert(offsetof(SceneMesh, texCoords) == mesh::kTexCoords);
static_assert(offsetof(SceneMesh, faces) == mesh::kFaces);
static_assert(offsetof(SceneMesh, groups) == mesh::kGroups);
static_assert(offsetof(SceneMesh, positionBlocks) == 0xA8);
static_assert(offsetof(SceneMesh, normals) == 0xF8);
static_assert(offsetof(SceneMesh, corners) == 0x108);
static_assert(offsetof(SceneMesh, cornerIndices) == 0x128);
static_assert(offsetof(SceneMesh, center) == 0x15C);
static_assert(offsetof(SceneMesh, materialValue) == 0x184);
static_assert(sizeof(SceneMesh) == mesh::kSize);

// The caller's node and mesh arrays seen with their element types (never resized through these).
using NodeArray = mfc::CArray<SceneNode, mfc::RawElements<SceneNode>>;
using MeshArray = mfc::CArray<SceneMesh, mfc::RawElements<SceneMesh>>;

// The fake unit's skin the bones are built into (BuildSkeleton's last argument): its bone list and
// root bone and its clock.
struct SceneSkin {
    // 00508990 (skin_rewind.cpp, A-U9). The root rewinds the skeleton (CAnimationNode::Rewind, its
    // body inlined) and the clock goes to 0.
    void Rewind();
    // 00508BF0 (RET 4, EAX 1; skin_rewind.cpp, A-U11). The root (when there is one) binds the parts
    // (CAnimationNode::AttachMeshes).
    int BindMeshes(const lib::AlignedArray<void*>* parts);
    // scene_skin_lifetime.cpp (A-U12).
    // 00508700. The constructor: empty arrays and names, no root, the clock -1. Returns this.
    SceneSkin* Construct();
    // 005087B0. The destructor: the root bone destroyed and deleted (the bone list emptied), the
    // names emptied and destroyed, the arrays' blocks freed.
    void Destruct();
    // 00508870. The root bone destroyed and deleted (the bone list's block freed), the clock 0, the
    // name "none", no names, +0x4C 0. Returns 1.
    int Create();
    // 005088E0. The root bone destroyed and deleted (the bone list emptied), +0x00 0, no names.
    void Clear();
    // 00508930 (RET 4). The owner becomes `value`.
    void SetOwner(std::uint32_t value);

    // +0x00 the fake unit the skin belongs to (CFakeUnit's Create 00446AF0; 0 when the unit lets
    // its parts go, fake_unit.cpp); 0 after Destruct / Clear. Nothing here reads it.
    std::uint32_t owner;
    lib::AlignedArray<CAnimationNode*> bones;   // +0x04 every bone, in build order (SetAtGrow 00452C40)
    CAnimationNode* root;                       // +0x14
    // +0x18 the clock (the unused 00508A30 stores its float argument here as a double, then zeroes it).
    std::uint32_t clock[2];
    mfc::CString name;                          // +0x20 "none" after Create
    mfc::CStringArray names;                    // +0x24
    std::uint32_t spare38;                      // +0x38 0 after the constructor; nothing reads it
    lib::AlignedArray<void*> spareArray;        // +0x3C only emptied and freed here
    std::uint32_t spare4C;                      // +0x4C 0 after Create; nothing reads it
};
static_assert(offsetof(SceneSkin, bones) == 0x04);
static_assert(offsetof(SceneSkin, root) == 0x14);
static_assert(offsetof(SceneSkin, name) == 0x20);
static_assert(offsetof(SceneSkin, spareArray) == 0x3C);
static_assert(sizeof(SceneSkin) == 0x50);

// 004492F0 (scene_bones.cpp; __fastcall, RET 8). Adds `bone` to the skin's list and builds it (and
// its children) from `node`; the low word of `frames` is the animation's frame count.
void __fastcall BuildBone(SceneSkin* skin, CAnimationNode* bone, const SceneNode* node, int frames);

// 007843AC: "ResolveNodesAMF. ..." (a first rotation key not at frame 0); 007843F0: "Can't find
// top-level object in hierarchy" (BuildSkeleton's fatal errors).
inline const char* FirstKeyNotAtZeroText() { return reinterpret_cast<const char*>(std::uintptr_t(0x007843AC)); }
inline const char* NoRootNodeText() { return reinterpret_cast<const char*>(std::uintptr_t(0x007843F0)); }

// 0079B9B0: the exe's zero vector (the scene code copies it).
inline const Vector3& ZeroVector() { return *reinterpret_cast<const Vector3*>(std::uintptr_t(0x0079B9B0)); }
// 0078441C: "node %s have NONULL pivot (x=%g, y=%g, z=%g)" (BuildBone's fatal error).
inline const char* NonNullPivotText() { return reinterpret_cast<const char*>(std::uintptr_t(0x0078441C)); }

// The helpers (+0x56C) are materials (material.h).
using HelperArray = mfc::CArray<CMaterial, mfc::RawElements<CMaterial>>;

// The fake unit (CFakeUnit, vtable 0073BFE8) as LoadFakeUnitScene fills it: a CObject3D
// (map_object_figures.h) up to +0x4E0, then the skin and the scene's arrays.
struct FakeUnitScene {
    std::uint8_t object3D_[0x4E0];      // the CObject3D part: Object3D()
    SceneSkin skin;                     // +0x4E0 (0x50 bytes)
    VtArray figures;                    // +0x530 CFigureMesh records (0x220, constructed by 00453240)
    VtArray meshes;                     // +0x544 SceneMesh records
    VtArray boneLinks;                  // +0x558 per figure a CArray of NodeFrame records (0x14 each)
    VtArray helpers;                    // +0x56C CMaterial records (HelperArray)

    CObject3D* Object3D() { return reinterpret_cast<CObject3D*>(this); }
};
static_assert(offsetof(FakeUnitScene, skin) == 0x4E0);
static_assert(offsetof(FakeUnitScene, figures) == 0x530);
static_assert(offsetof(FakeUnitScene, helpers) == 0x56C);

// The figure's .reg file's records (0044B980): texture names and texture links by id.
struct TextureNameRecord {
    std::uint32_t id;
    mfc::CString name;
};
struct TextureLinkRecord {
    std::uint32_t id;
    std::uint32_t value;
};
// A texture of the unit's material holder (CObject3D::attached; 0051B2F0 finds one by id, 0051B470
// adds a link): only its name is written here.
struct SceneTexture {
    std::uint8_t engineObject[0x1C];    // +0x00 the CEngineObject part
    mfc::CString name;                  // +0x1C
};

constexpr std::uint32_t kVersionWithNormals = 0x1468FCCA;

// The loader's `this`: the caller's scene info; only its first word is written.
class SceneInfo {
public:
    // 00471B10. Reads the scene from `file` (an open CFile; only slot 13, Read, is used). `meshes`
    // and `nodes` get one element per record, `helpers` one per helper. Returns 1.
    mfc::BOOL Load(mfc::CFileVirtuals* file, VtArray* meshes, VtArray* helpers, VtArray* nodes);

    std::uint16_t flags_;
};

// 00446C10. Loads the scene file into a CFakeUnit (its slot 62, 00446BA0, calls this) and builds the
// unit's figures from it. See scene_loader.cpp.
mfc::BOOL __stdcall LoadFakeUnitScene(const char* fileName, const char* registryName, FakeUnitScene* fakeUnit);

// The data the loader reads by address: the vtables of the arrays it constructs inline and its
// messages.
namespace scenedata {
inline void* PointArrayVtable() { return reinterpret_cast<void*>(std::uintptr_t(0x0073C860)); }       // A
inline void* TexCoordArrayVtable() { return reinterpret_cast<void*>(std::uintptr_t(0x0073C854)); }    // B
inline void* FileFaceArrayVtable() { return reinterpret_cast<void*>(std::uintptr_t(0x0073C848)); }    // C
inline void* GroupArrayVtable() { return reinterpret_cast<void*>(std::uintptr_t(0x0073C2BC)); }       // D
inline void* RotKeyArrayVtable() { return reinterpret_cast<void*>(std::uintptr_t(0x0073C83C)); }      // E
inline void* PosKeyArrayVtable() { return reinterpret_cast<void*>(std::uintptr_t(0x0073C830)); }      // F
inline void* MorphHeadArrayVtable() { return reinterpret_cast<void*>(std::uintptr_t(0x0073C824)); }   // G
inline void* NormalArrayVtable() { return reinterpret_cast<void*>(std::uintptr_t(0x0073C86C)); }      // H
inline std::uintptr_t DwordMapVtable() { return std::uintptr_t(0x0073C818); }
inline void* MapArrayVtable() { return reinterpret_cast<void*>(std::uintptr_t(0x0073C80C)); }
inline void* IndexArrayVtable() { return reinterpret_cast<void*>(std::uintptr_t(0x0073C800)); }
inline void* NodeArrayVtable() { return reinterpret_cast<void*>(std::uintptr_t(0x0073C15C)); }
inline void* TextureNameArrayVtable() { return reinterpret_cast<void*>(std::uintptr_t(0x0073C144)); }   // {id, CString}
inline void* TextureLinkArrayVtable() { return reinterpret_cast<void*>(std::uintptr_t(0x0073C150)); }   // {id, value}

inline const char* CantOpenText() { return reinterpret_cast<const char*>(std::uintptr_t(0x00784394)); }   // "Can't open file %s.\n"
// "Scene \"%s\" have more then one top-level object!..."
inline const char* ManyTopsText() { return reinterpret_cast<const char*>(std::uintptr_t(0x00784344)); }
inline const char* NewLineText() { return reinterpret_cast<const char*>(std::uintptr_t(0x00784340)); }    // "\n"
inline const char* SceneExtText() { return reinterpret_cast<const char*>(std::uintptr_t(0x00784338)); }   // ".amf"
inline const char* ScenesDirText() { return reinterpret_cast<const char*>(std::uintptr_t(0x00784330)); }  // "scenes\\"
inline const char* IniDirText() { return reinterpret_cast<const char*>(std::uintptr_t(0x00784328)); }     // "Ini's\\"
inline const char* RegExtText() { return reinterpret_cast<const char*>(std::uintptr_t(0x00784320)); }     // ".reg"
inline const char* ModelNameText() { return reinterpret_cast<const char*>(std::uintptr_t(0x00784304)); }  // "Model File Name = \"%s\".\n"
inline const char* BmpExtText() { return reinterpret_cast<const char*>(std::uintptr_t(0x007842FC)); }     // ".bmp"
} // namespace scenedata

} // namespace scene
