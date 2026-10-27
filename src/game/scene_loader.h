#pragma once

// The scene file loader of CFakeUnit: reads a binary .sc* scene (meshes with their
// vertex animation, the node hierarchy, helpers) into the arrays of the figure being built.
// Called by `LoadFakeUnitScene` (the CFakeUnit scene loader) with the opened CFile.
// Names are provisional; see docs/notes/objects.md ("Facts").
// File layout (all integers little endian, no padding):
//   header (0x14): u32 version (0x1468FCCA: records carry an extra normal list), u32 record count,
//                  u32 skipped record count, u32 helper count, u16 flags (returned to the caller)
//   records (0x2C header each, one mesh and one node per record), then the skipped records (same
//   format, read and dropped), then the helpers (0x3E header each).

#include "core_transform.h"
#include "engine_object.h"
#include "material.h"
#include "unit.h"   // CUnit, the fake unit's base

#include "../lib/containers.h"
#include "../lib/crt.h"
#include "../lib/mfc.h"
#include "../lib/mfc_array.h"
#include "../lib/mfc_coll.h"
#include "../lib/vecmath.h"
#include "../lib/mfc_slots.h"
#include "game_data.h"

#include <cstddef>
#include <cstdint>

class CAnimationNode;
class CFigureMesh;
class CPlainFigureMesh;
class CObject3D;

namespace scene {
struct TextureNameRecord;
} // namespace scene

namespace figure {
// A record whose only member to destroy is the CString at +4 (and its scalar deleting destructor,
// mfc::impl::ScalarDeleting over this).
void __fastcall NamedRecordDestruct(scene::TextureNameRecord* self);
// An aligned array's four fields cleared; returns it.
void* __fastcall ClearArray(std::uint32_t* self);
} // namespace figure

namespace scene {

// ConstructElements / DestructElements(elements, count) of an instantiation the game calls out of line
// (`Unmake` null: nothing is destroyed).
template <typename T, void(__stdcall* Make)(void*, int), void(__stdcall* Unmake)(void*, int) = nullptr>
struct OriginalElements {
    static void Construct(T* elements, int count) { Make(elements, count); }
    static void Destruct(T* elements, int count) {
        if constexpr (Unmake != nullptr) Unmake(elements, count);
    }
};

// The "aligned" arrays (vertex lists, 32-byte aligned blocks with the raw pointer stored before the data;
// lib::AlignedArray), per element type. Positions and normals are kept as the bits that were read: the
// loader copies some of them through the FPU. The later passes read the same bits as floats (`point`,
// `uv`).
union PointBits {
    std::uint32_t v[3];
    Vector3 point;
};
union UvBits {
    std::uint32_t v[2];
    float uv[2];
};
using PointBuffer = lib::AlignedArray<PointBits>;
using UvBuffer = lib::AlignedArray<UvBits>;
union Block64 {
    std::uint8_t bytes[64];   // a matrix (the figures copy the blocks)
    float block[16];          // four normals / positions in lanes: x[4], y[4], z[4], w[4]
};
static_assert(sizeof(PointBits) == 12 && sizeof(UvBits) == 8 && sizeof(Block64) == 64);
using BlockBuffer = lib::AlignedArray<Block64>;
using DwordBuffer = lib::AlignedArray<std::uint32_t>;

// MFC CMap<DWORD, DWORD, DWORD, DWORD> and its association.
using DwordMap = mfc::CMap<std::uint32_t, std::uint32_t>;   // hashSize 17, blockSize 10
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
    // Face_normal.cpp. The face's plane from the points `positions` (an array of 3-float points) holds: the
    // edges from vertex 1 to vertex 2 and to vertex 0 are normalised unless shorter than 1e-5, crossed, the
    // result normalised (a zero length gives the zero vector), then the distance.
    void ComputeNormal(const PointBuffer* positions);
    std::uint32_t vertex[3];       // +00
    std::uint32_t vertex2[3];      // +0C
    std::uint32_t position[3];     // +18 the file's position indices
    Vector3 normal;                // +24 unit normal of the plane
    float distance;                // +30 the plane's distance: dot(position[1]'s point, normal)
};
static_assert(sizeof(MeshFace) == 0x34);
using FaceBuffer = lib::AlignedArray<MeshFace>;

// A material group (0x18): its name and the faces it uses.
struct FaceGroup {
    // operator= (scene_loader.cpp): the name, then the faces (CDWordArray::Copy: SetSize to the source's,
    // the indices copied).
    void Assign(const FaceGroup* from);
    // ConstructElements: the zero fill, then each name empty and the faces' CDWordArray constructor.
    static void __stdcall ConstructElements(FaceGroup* groups, int count);
    // The destructor: the faces emptied (SetSize 0) and the name emptied, then both destroyed.
    void Destruct();

    mfc::CString name;                 // +00
    mfc::impl::CDWordArray faces;      // +04 face indices
};
static_assert(sizeof(FaceGroup) == 0x18);
// CArray<FaceGroup>'s elements: constructed by ConstructElements, destroyed by FaceGroup::Destruct.
struct FaceGroupElements {
    static void Construct(FaceGroup* p, int n) { FaceGroup::ConstructElements(p, n); }
    static void Destruct(FaceGroup* p, int n) {
        for (; n--; ++p) p->Destruct();
    }
};
using FaceGroupArray = mfc::CArray<FaceGroup, FaceGroupElements>;
// CArray<FaceGroup>'s destructor (record_arrays.h's FaceGroupArrayClass): each group
// destroyed, the storage freed.
void __fastcall FaceGroupArrayDestruct(FaceGroupArray* groups);

// Mesh (element of the caller's +0x544 array).
namespace mesh {
constexpr std::size_t kSize = 0x18C;
constexpr std::size_t kName = 0x94;        // CString
constexpr std::size_t kPositions = 0xD8;   // aligned array of 12-byte points
constexpr std::size_t kTexCoords = 0xE8;   // aligned array of 8-byte (u, v)
constexpr std::size_t kFaces = 0x138;      // aligned array of MeshFace
constexpr std::size_t kGroups = 0x148;     // CArray of FaceGroup
} // namespace mesh

// Node (element of the caller's local array).
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
    Vector3 value;              // (a pivot: the skeleton's translation keys)
};
static_assert(sizeof(NodeRotationKey) == 0x14);
struct NodePositionKey {        // 0x18: the frame, an angle and an axis (a rotation)
    std::int16_t frame;
    std::uint16_t unused_02;
    std::uint32_t unused_04;
    float angle;
    Vector3 axis;
};
static_assert(sizeof(NodePositionKey) == 0x18);
struct NodeMorphKey {           // 0x14: the frame and the mesh's points at it
    std::int16_t frame;
    std::uint16_t unused_02;
    lib::AlignedArray<Vector3> points;
};
static_assert(sizeof(NodeMorphKey) == 0x14);
// The node's morph key array constructs and destroys its keys out of line
// (scene_loader.cpp).
struct NodeMorphKeyElements {
    static void Construct(NodeMorphKey* keys, int count);
    static void Destruct(NodeMorphKey* keys, int count);
};

// A node's frame record (40 bytes; BuildSkeleton puts the pivot and rotation in the first).
struct NodeFrame {
    std::uint32_t unused_00[3];
    Vector3 pivot;
    Quaternion rotation;
};
static_assert(sizeof(NodeFrame) == 40);
// The records zero filled, each rotation the identity. The node's frame array (never destroyed element by
// element).
void __stdcall ConstructNodeFrames(void* records, int count);
using NodeFrameArray = mfc::CArray<NodeFrame, OriginalElements<NodeFrame, ConstructNodeFrames>>;

// A node (the node:: offsets above). The loader fills the key arrays (their instantiations zero
// new elements, the morph keys' construct them out of line); scene_skeleton.cpp names the rest.
struct SceneNode {
    // The destructor: the arrays last to first (each its vtable back, then its
    // storage freed; the morph keys' point blocks first), then the parent's and the node's names.
    void Destruct();
    // The constructor body ConstructElements inlines after its zero fill (scene_loader.cpp): the names
    // empty, the four rotations identity, each array empty with its vtable.
    void Construct();

    std::uint32_t unused_000;
    mfc::CString name;
    mfc::CString parent;
    std::uint32_t unused_00C;
    std::uint32_t spare010;             // cleared by BuildSkeleton; nothing reads it
    std::uint32_t spare014;             // cleared by BuildSkeleton; nothing reads it
    std::uint8_t unused_018[0x30 - 0x18];
    Quaternion spareRotation030;        // identity from the constructor; nothing reads it
    Quaternion spareRotation040;        // identity from the constructor; nothing reads it
    std::uint32_t unused_050;
    Vector3 offset;                     // the pivot (must be 0, 0, 0: BuildBone)
    Vector3 axis;                       // the first position key's axis
    float angle;                        // its angle
    Vector3 pivot;                      // the first rotation key's vector
    Quaternion rotation;
    Vector3 restPivot;
    Quaternion restRotation;
    mfc::CArray<NodeRotationKey> rotationKeys;
    mfc::CArray<NodePositionKey> positionKeys;
    mfc::CArray<NodeMorphKey, NodeMorphKeyElements> morphKeys;
    mfc::CArray<Vector3> normals;                               // (version 0x1468FCCA only)
    // +0x0F8 (elements constructed by KeyFrames' ConstructElements); the loader copies them to the figures'
    // bone links.
    NodeFrameArray frames;
    mfc::CArray<SceneNode*> children;
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

// A distinct face corner of a mesh (6 bytes): merged vertex, file position and texture vertex.
struct MeshCornerKey {
    std::uint16_t vertex, position, texture;
};
static_assert(sizeof(MeshCornerKey) == 6);

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

// A mesh (mesh:: offsets; "CFakeFigure", class id 0x47): a transform object (an engine object whose GetName
// virtual gives its name). A real virtual class: the compiler makes its vtable. Only the members the scene
// code uses are declared.
struct SceneMesh : CTransformObject {
    SceneMesh();
    ~SceneMesh() override;
    // The deleting destructor frees into the game's heap.

    // The virtuals not listed are CTransformObject's.
    int Create() override;                 // returns 1
    void Destroy() override;               // nothing
    const char* GetName() override;        // returns the name
    const char* GetClassName() override;
    int GetClassId() override;             // class id 0x47
    void SaveTransform(mfc::CFileVirtuals* file) override;   // nothing
    void LoadTransform(mfc::CFileVirtuals* file) override;   // nothing

    std::uint8_t unused_054[0x94 - 0x54];
    mfc::CString name;
    // +0x098 / +0x0C8 aligned blocks no loader code we know fills; the destructor frees them.
    lib::AlignedArray<std::uint8_t> spareBlocks098;
    // +0x0A8 / +0x0B8 the positions and the normals packed four to a 64-byte block (x[4], y[4], z[4],
    // w[4]); the figures copy them (CFigureMesh +0x1C0 / +0x1D0).
    BlockBuffer positionBlocks;
    BlockBuffer normalBlocks;
    lib::AlignedArray<std::uint8_t> spareBlocks0C8;
    PointBuffer positions;              // the file's bits
    UvBuffer texCoords;
    PointBuffer normals;                // one per position
    // +0x108 / +0x118 / +0x128: the distinct corners (sorted), the distinct (vertex,
    // position) word pairs (sorted, padded to a multiple of four) and each face corner's index in
    // the corner table (three words per face).
    lib::AlignedArray<MeshCornerKey> corners;
    DwordBuffer pairs;
    lib::AlignedArray<std::uint16_t> cornerIndices;
    FaceBuffer faces;
    FaceGroupArray groups;
    // +0x15C the bounds (the figure copies them to its +0x160): the centre, the box corners relative to it
    // and the radius.
    Vector3 center;
    Vector3 boxMin;
    Vector3 boxMax;
    float radius;
    // +0x184 / +0x188 the material of the mesh's last group (from its name); the figure copies them to its
    // +0x158 / +0x15C.
    int materialValue;
    int materialType;
};
static_assert(offsetof(SceneMesh, name) == mesh::kName);
static_assert(offsetof(SceneMesh, spareBlocks098) == 0x98 && offsetof(SceneMesh, spareBlocks0C8) == 0xC8);
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

// The scene's mesh and node arrays (record_arrays.h registers their instantiations). A mesh is built by its
// C++ constructor (InitSceneMesh, scene_loader.cpp) and destroyed through its virtual destructor; a node by
// its bodies.
void InitSceneMesh(SceneMesh* p);
inline void InitSceneNode(SceneNode* p) { p->Construct(); }
inline void DestroySceneNode(SceneNode* p) { p->Destruct(); }
using MeshElements = mfc::impl::EachElements<SceneMesh, InitSceneMesh, mfc::impl::DeleteThroughFirst<SceneMesh>>;
using NodeElements = mfc::impl::EachElements<SceneNode, InitSceneNode, DestroySceneNode>;
using MeshArray = mfc::CArray<SceneMesh, MeshElements>;
using NodeArray = mfc::CArray<SceneNode, NodeElements>;

// The fake unit's skin the bones are built into (BuildSkeleton's last argument): its bone list and
// root bone and its clock.
struct SceneSkin {
    // The root rewinds the skeleton (CAnimationNode::Rewind, its body inlined) and the clock goes to 0.
    void Rewind();
    // The root (when there is one) binds the parts (CAnimationNode::AttachMeshes).
    int BindMeshes(const lib::AlignedArray<CFigureMesh*>* parts);
    SceneSkin();
    ~SceneSkin();
    // The root bone destroyed and deleted (the bone list's block freed), the clock 0, the
    // name "none", no names, +0x4C 0. Returns 1.
    int Create();
    // The root bone destroyed and deleted (the bone list emptied), +0x00 0, no names.
    void Clear();
    // The owner becomes `value`.
    void SetOwner(std::uint32_t value);

    // +0x00 the fake unit the skin belongs to (CFakeUnit's Create; 0 when the unit lets its parts go,
    // fake_unit.cpp); 0 after Destruct / Clear. Nothing here reads it.
    std::uint32_t owner;
    lib::AlignedArray<CAnimationNode*> bones;   // every bone, in build order
    CAnimationNode* root;
    // +0x18 the clock (an unused setter stores its float argument here as a double, then zeroes it).
    std::uint32_t clock[2];
    mfc::CString name;                          // "none" after Create
    mfc::CStringArray names;
    std::uint32_t spare38;                      // 0 after the constructor; nothing reads it
    lib::AlignedArray<void*> spareArray;        // only emptied and freed here
    std::uint32_t spare4C;                      // 0 after Create; nothing reads it
};
static_assert(offsetof(SceneSkin, bones) == 0x04);
static_assert(offsetof(SceneSkin, root) == 0x14);
static_assert(offsetof(SceneSkin, name) == 0x20);
static_assert(offsetof(SceneSkin, spareArray) == 0x3C);
static_assert(sizeof(SceneSkin) == 0x50);

// Adds `bone` to the skin's list and builds it (and its children) from `node`; the low word of `frames` is
// the animation's frame count.
void __fastcall BuildBone(SceneSkin* skin, CAnimationNode* bone, const SceneNode* node, int frames);

// The fatal-error texts of BuildSkeleton: a first rotation key not at frame 0, and no top-level object in the
// hierarchy.
inline const char* FirstKeyNotAtZeroText() { return "ResolveNodesAMF.\n This version requires start from position 0 !\n"; }
inline const char* NoRootNodeText() { return "Can't find top-level object in hierarchy\n"; }

// The scene code's zero vector (a static initialiser's stores).
inline constexpr Vector3 kZeroVector = /*0079B9B0*/ {0.0f, 0.0f, 0.0f};
// "node %s have NONULL pivot (x=%g, y=%g, z=%g)" (BuildBone's fatal error).
inline const char* NonNullPivotText() { return "node %s have NONULL pivot\nx = %g\ty = %g\tz = %g\n"; }

// The helpers are materials (material.h): built by CMaterial's constructor (InitHelper,
// material.cpp), destroyed through its virtual destructor.
void InitHelper(CMaterial* p);
using HelperElements = mfc::impl::EachElements<CMaterial, InitHelper, mfc::impl::DeleteThroughFirst<CMaterial>>;
using HelperArray = mfc::CArray<CMaterial, HelperElements>;

// The figures: 0x220-byte class 0x42 figure meshes as the figure factory builds one
// (InitFigurePart, figure_mesh_life.cpp: CPlainFigureMesh's constructor), destroyed through its virtual
// destructor.
void InitFigurePart(CPlainFigureMesh* p);
using FigurePartElements =
    mfc::impl::EachElements<CPlainFigureMesh, InitFigurePart, mfc::impl::DeleteThroughFirst<CPlainFigureMesh>>;
using FigurePartArray = mfc::CArray<CPlainFigureMesh, FigurePartElements>;

// The bone links: per figure, a copy of its node's frame records (sized as key frames), the arrays
// built empty and destroyed through their virtual destructor.
using BoneLinkRecords = mfc::impl::KeyFrames::Array;
inline void InitBoneLinks(BoneLinkRecords* p) { mfc::impl::ConstructArrayOf<mfc::impl::BoneLinkArrayClass>(*p); }
using BoneLinkElements =
    mfc::impl::EachElements<BoneLinkRecords, InitBoneLinks, mfc::impl::DeleteThroughObject<BoneLinkRecords>>;
using BoneLinkArray = mfc::CArray<BoneLinkRecords, BoneLinkElements>;

// The fake unit (CFakeUnit, GetClassName "CFakeUnit", 0x580 bytes; fake_unit.cpp): a unit (unit.h) with the
// skin and the scene's arrays, which LoadFakeUnitScene fills. A real virtual class: the compiler makes its
// vtable. No scenario makes one (class 0x39 in the map object factory): only the builds check it.
class FakeUnitScene : public CUnit {
public:
    FakeUnitScene();
    ~FakeUnitScene() override;
    // The deleting destructor frees into the game's heap.

    // The virtuals that differ from CUnit's (fake_unit.cpp).
    int Create() override;                  // the skin created and owned, then CreateUnit
    void Destroy() override;                // the parts let go, the figures destroyed
    const char* GetClassName() override;
    int GetClassId() override;              // class id 0x50
    // Loads the scene (LoadFakeUnitScene); on success each figure gets a new object of class 0x45 from the
    // world's side-specific manager. Returns the loader's 0, or 1.
    virtual int LoadScene(const char* file, const char* regFile);

    std::uint8_t unused_4DC[4];         // the skin's alignment (its clock is a double)
    SceneSkin skin;
    FigurePartArray figures;
    MeshArray meshes;
    BoneLinkArray boneLinks;            // per figure a CArray of NodeFrame records (0x14 each)
    HelperArray helpers;

private:
    // The parts let go (the start of both the destructor and Destroy): the skin emptied and
    // unowned, the unit's Destroy.
    void ReleaseParts();
};
static_assert(sizeof(FakeUnitScene) == 0x580);
static_assert(offsetof(FakeUnitScene, skin) == 0x4E0);
static_assert(offsetof(FakeUnitScene, figures) == 0x530);
static_assert(offsetof(FakeUnitScene, helpers) == 0x56C);

// The figure's .reg file's records: texture names and texture links by id.
struct TextureNameRecord {
    std::uint32_t id;
    mfc::CString name;
};
struct TextureLinkRecord {
    std::uint32_t id;
    std::uint32_t value;
};
// The loader's arrays of them (LoadFigureRegistry adds to both): the links plain (vtable
// TextureLinkArrayClass), the names' strings set empty and each record destroyed (vtable
// TextureNameArrayClass).
using TextureLinkArray = mfc::CArray<TextureLinkRecord>;
struct TextureNameElements {
    static void Construct(TextureNameRecord* elements, int count) {
        mfc::impl::ConstructElementsAt<mfc::impl::NamedDwords>(elements, count);
    }
    static void Destruct(TextureNameRecord* elements, int count) {
        for (int i = 0; i < count; ++i) figure::NamedRecordDestruct(&elements[i]);
    }
};
using TextureNameArray = mfc::CArray<TextureNameRecord, TextureNameElements>;
static_assert(sizeof(TextureNameRecord) == 8 && sizeof(TextureLinkArray) == 0x14);

constexpr std::uint32_t kVersionWithNormals = 0x1468FCCA;

// The loader's `this`: the caller's scene info; only its first word is written.
class SceneInfo {
public:
    // Reads the scene from `file` (an open CFile; only Read is used). `meshes` and `nodes` get one element
    // per record, `helpers` one per helper. Returns 1.
    mfc::BOOL Load(mfc::CFileVirtuals* file, MeshArray* meshes, HelperArray* helpers, NodeArray* nodes);

    std::uint16_t flags_;
};

// The figure's .reg file read into the texture links and names.
int __fastcall LoadFigureRegistry(FakeUnitScene* unit, const mfc::CString* path, TextureLinkArray* links,
                                  TextureNameArray* names);
// The skin's bones built from the scene nodes; 0 without a root node.
int __fastcall BuildSkeleton(MeshArray* meshArray, NodeArray* nodeArray, int frames, SceneSkin* skin);
// The meshes' vertices welded to the registry's nodes.
void __fastcall WeldMeshVertices(FakeUnitScene* unit, NodeArray* nodes);
// The meshes' positions packed; one mesh's bounds.
void __fastcall PackMeshPositions(FakeUnitScene* unit);
void __fastcall ComputeMeshBounds(SceneMesh* mesh);

// Loads the scene file into a CFakeUnit (the CFakeUnit scene-loading virtual calls this) and builds the
// unit's figures from it. See scene_loader.cpp.
mfc::BOOL __stdcall LoadFakeUnitScene(const char* fileName, const char* registryName, FakeUnitScene* fakeUnit);

// The data the loader reads by address: the vtables of the arrays it constructs inline and its
// messages.
namespace scenedata {
inline void* DwordMapVtable() { return mfc::impl::DwordMapClass::Vtable(); }

inline const char* CantOpenText() { return "Can't open file %s.\n"; }
// "Scene \"%s\" have more then one top-level object!..."
inline const char* ManyTopsText() { return "Scene \"%s\" have more then one top-level object!\nCheck links!\n TopLevelObjects:\n"; }
inline const char* NewLineText() { return "\n"; }
inline const char* SceneExtText() { return ".amf"; }
inline const char* ScenesDirText() { return "scenes\\"; }
inline const char* IniDirText() { return "Ini's\\"; }
inline const char* RegExtText() { return ".reg"; }
inline const char* ModelNameText() { return "Model File Name = \"%s\".\n"; }
inline const char* BmpExtText() { return ".bmp"; }
} // namespace scenedata

} // namespace scene

