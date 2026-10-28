#pragma once

// The scene file loader of CFakeUnit: reads a binary scene (meshes with their vertex animation, the node
// hierarchy, helpers) into the arrays of the figure being built. See docs/notes/objects.md ("Facts").
// File layout (all integers little endian, no padding):
//   header (0x14): u32 version (0x1468FCCA: records carry an extra normal list), u32 record count,
//                  u32 skipped record count, u32 helper count, u16 flags (returned to the caller)
//   records (0x2C header each, one mesh and one node per record), then the skipped records (same
//   format, read and dropped), then the helpers (0x3E header each).

#include "core_transform.h"
#include "engine_object.h"
#include "material.h"
#include "unit.h"

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
#include <cstring>
#include <memory>
#include <string>
#include <vector>

class CAnimationNode;

namespace scene {
struct TextureNameRecord;
} // namespace scene

namespace figure {
void NamedRecordDestruct(scene::TextureNameRecord* self);
} // namespace figure

namespace scene {

// `Unmake` null: nothing is destroyed.
template <typename T, void(*Make)(void*, int), void(*Unmake)(void*, int) = nullptr>
struct OriginalElements {
    static void Construct(T* elements, int count) { Make(elements, count); }
    static void Destruct(T* elements, int count) {
        if constexpr (Unmake != nullptr) Unmake(elements, count);
    }
};

// Positions and normals are kept as the bits that were read: the loader copies some of them through the FPU.
// The later passes read the same bits as floats.
union PointBits {
    std::uint32_t v[3];
    Vector3 point;
};
union UvBits {
    std::uint32_t v[2];
    float uv[2];
};
using PointBuffer = std::vector<PointBits>;
using UvBuffer = std::vector<UvBits>;
union Block64 {
    std::uint8_t bytes[64];   // a matrix (the figures copy the blocks)
    float block[16];          // four normals / positions in lanes: x[4], y[4], z[4], w[4]
};
static_assert(sizeof(PointBits) == 12 && sizeof(UvBits) == 8 && sizeof(Block64) == 64);
using BlockBuffer = std::vector<Block64>;
using DwordBuffer = std::vector<std::uint32_t>;

using DwordMap = mfc::CMap<std::uint32_t, std::uint32_t>;   // hashSize 17, blockSize 10
using MapAssoc = DwordMap::Assoc;
static_assert(sizeof(DwordMap) == 0x1C && sizeof(MapAssoc) == 0x10);

// One face of a record.
struct FileFace {
    std::uint32_t position[3];
    std::uint32_t texture[3];
    std::uint32_t unused_18;
};
static_assert(sizeof(FileFace) == 0x1C);

// A mesh face as the mesh keeps it. The loader copies the file face's six indices to vertex and vertex2 and
// its three position indices again to position, then replaces vertex and vertex2 by the merged vertex of each
// corner. The loader does not write the plane.
struct MeshFace {
    // The edges from vertex 1 to vertex 2 and to vertex 0 are normalised unless shorter than 1e-5, crossed,
    // the result normalised (a zero length gives the zero vector).
    void ComputeNormal(const PointBuffer* positions);
    std::uint32_t vertex[3];
    std::uint32_t vertex2[3];
    std::uint32_t position[3];     // the file's position indices
    Vector3 normal;
    float distance;                // dot(position[1]'s point, normal)
};
static_assert(sizeof(MeshFace) == 0x34);
using FaceBuffer = std::vector<MeshFace>;

// A material group: its name and the faces it uses.
struct FaceGroup {
    void Assign(const FaceGroup* from);
    static void ConstructElements(FaceGroup* groups, int count);
    void Destruct();

    std::string name;
    mfc::impl::CDWordArray faces;      // face indices
};
struct FaceGroupElements {
    static void Construct(FaceGroup* p, int n) { FaceGroup::ConstructElements(p, n); }
    static void Destruct(FaceGroup* p, int n) {
        for (; n--; ++p) p->Destruct();
    }
};
using FaceGroupArray = mfc::CArray<FaceGroup, FaceGroupElements>;
void FaceGroupArrayDestruct(FaceGroupArray* groups);

// The node's key records (the frame is read as a signed short).
struct NodeRotationKey {
    std::int16_t frame;
    std::uint16_t unused_02;
    std::uint32_t unused_04;
    Vector3 value;              // a pivot: the skeleton's translation keys
};
static_assert(sizeof(NodeRotationKey) == 0x14);
struct NodePositionKey {        // an angle and an axis: a rotation
    std::int16_t frame;
    std::uint16_t unused_02;
    std::uint32_t unused_04;
    float angle;
    Vector3 axis;
};
static_assert(sizeof(NodePositionKey) == 0x18);
struct NodeMorphKey {           // the mesh's points at the frame
    std::int16_t frame;
    std::uint16_t unused_02;
    std::vector<Vector3> points;
};
struct NodeMorphKeyElements {
    static void Construct(NodeMorphKey* keys, int count);
    static void Destruct(NodeMorphKey* keys, int count);
};

// A node's frame record (BuildSkeleton puts the pivot and rotation in the first).
struct NodeFrame {
    std::uint32_t unused_00[3];
    Vector3 pivot;
    Quaternion rotation;
};
static_assert(sizeof(NodeFrame) == 40);
// Each rotation the identity. The array is never destroyed element by element.
void ConstructNodeFrames(void* records, int count);
using NodeFrameArray = mfc::CArray<NodeFrame, OriginalElements<NodeFrame, ConstructNodeFrames>>;

// A node. The loader fills the key arrays; BuildSkeleton the rest.
struct SceneNode {
    void Destruct();
    // After ConstructElements' zero fill.
    void Construct();

    std::uint32_t unused_000;
    std::string name;
    std::string parent;
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
    std::vector<NodeRotationKey> rotationKeys;
    std::vector<NodePositionKey> positionKeys;
    mfc::CArray<NodeMorphKey, NodeMorphKeyElements> morphKeys;
    std::vector<Vector3> normals;                               // version 0x1468FCCA only
    NodeFrameArray frames;              // the loader copies them to the figures' bone links
    std::vector<SceneNode*> children;
};
static_assert(offsetof(SceneNode, name) == 0x04);

// A distinct face corner of a mesh.
struct MeshCornerKey {
    std::uint16_t vertex, position, texture;
};
static_assert(sizeof(MeshCornerKey) == 6);

#undef GetClassName

// A mesh ("CFakeFigure").
struct SceneMesh : CTransformObject {
    SceneMesh();
    ~SceneMesh() override;

    int Create() override;                 // returns 1
    void Destroy() override;               // nothing
    const char* GetName() override;
    const char* GetClassName() override;
    int GetClassId() override;
    void SaveTransform(lib::File* file) override;   // nothing
    void LoadTransform(lib::File* file) override;   // nothing

    std::uint8_t unused_054[0x94 - 0x54];
    std::string name;
    // Nothing fills spareBlocks098 / 0C8; the destructor frees them.
    std::vector<std::uint8_t> spareBlocks098;
    // The positions and the normals packed four to a 64-byte block (x[4], y[4], z[4], w[4]); the figures copy
    // them.
    BlockBuffer positionBlocks;
    BlockBuffer normalBlocks;
    std::vector<std::uint8_t> spareBlocks0C8;
    PointBuffer positions;              // the file's bits
    UvBuffer texCoords;
    PointBuffer normals;                // one per position
    // The distinct corners (sorted), the distinct (vertex, position) word pairs (sorted, padded to a multiple
    // of four) and each face corner's index in the corner table (three words per face).
    std::vector<MeshCornerKey> corners;
    DwordBuffer pairs;
    std::vector<std::uint16_t> cornerIndices;
    FaceBuffer faces;
    FaceGroupArray groups;
    // The bounds (the figure copies them): the box corners are relative to the centre.
    Vector3 center;
    Vector3 boxMin;
    Vector3 boxMax;
    float radius;
    // The material of the mesh's last group (from its name); the figure copies them.
    int materialValue;
    int materialType;
};
static_assert(offsetof(SceneMesh, name) == 0x94);

// A mesh is built by its constructor and destroyed through its virtual destructor; a node by its bodies.
void InitSceneMesh(SceneMesh* p);
inline void InitSceneNode(SceneNode* p) { p->Construct(); }
inline void DestroySceneNode(SceneNode* p) { p->Destruct(); }
using MeshElements = mfc::impl::EachElements<SceneMesh, InitSceneMesh, mfc::impl::DeleteThroughFirst<SceneMesh>>;
using NodeElements = mfc::impl::EachElements<SceneNode, InitSceneNode, DestroySceneNode>;
using MeshArray = mfc::CArray<SceneMesh, MeshElements>;
using NodeArray = mfc::CArray<SceneNode, NodeElements>;

// The fake unit's skin the bones are built into.
struct SceneSkin {
    // The clock goes to 0.
    void Rewind();
    // When there is a root.
    int BindMeshes(const std::vector<CFigureMesh*>* parts);
    SceneSkin();
    ~SceneSkin();
    // The name "none". Returns 1.
    int Create();
    void Clear();
    void SetOwner(std::uint32_t value);

    // The fake unit the skin belongs to; 0 when the unit lets its parts go. Nothing here reads it.
    std::uint32_t owner;
    std::vector<CAnimationNode*> bones;   // every bone, in build order
    CAnimationNode* root;
    // An unused setter stores its float argument here as a double, then zeroes it.
    std::uint32_t clock[2];
    std::string name;
    std::vector<std::string> names;
    std::uint32_t spare38;                      // nothing reads it
    std::vector<void*> spareArray;        // only emptied and freed here
    std::uint32_t spare4C;                      // nothing reads it
};
static_assert(offsetof(SceneSkin, bones) == 0x04);

// Also `bone`'s children; the low word of `frames` is the animation's frame count.
void BuildBone(SceneSkin* skin, CAnimationNode* bone, const SceneNode* node, int frames);

inline const char* FirstKeyNotAtZeroText() { return "ResolveNodesAMF.\n This version requires start from position 0 !\n"; }
inline const char* NoRootNodeText() { return "Can't find top-level object in hierarchy\n"; }

inline constexpr Vector3 kZeroVector = /*0079B9B0*/ {0.0f, 0.0f, 0.0f};
inline const char* NonNullPivotText() { return "node %s have NONULL pivot\nx = %g\ty = %g\tz = %g\n"; }

// The helpers are materials, destroyed through their virtual destructor.
void InitHelper(CMaterial* p);
using HelperElements = mfc::impl::EachElements<CMaterial, InitHelper, mfc::impl::DeleteThroughFirst<CMaterial>>;
using HelperArray = mfc::CArray<CMaterial, HelperElements>;

// The figures: plain figure meshes as the figure factory builds one, destroyed through their virtual
// destructor.
void InitFigurePart(CPlainFigureMesh* p);
using FigurePartElements =
    mfc::impl::EachElements<CPlainFigureMesh, InitFigurePart, mfc::impl::DeleteThroughFirst<CPlainFigureMesh>>;
using FigurePartArray = mfc::CArray<CPlainFigureMesh, FigurePartElements>;

// The bone links: per figure, a copy of its node's frame records.
using BoneLinkRecords = mfc::impl::KeyFrames::Array;
inline void InitBoneLinks(BoneLinkRecords* p) { mfc::impl::ConstructArrayOf<mfc::impl::BoneLinkArrayClass>(*p); }
using BoneLinkElements =
    mfc::impl::EachElements<BoneLinkRecords, InitBoneLinks, mfc::impl::DeleteThroughObject<BoneLinkRecords>>;
using BoneLinkArray = mfc::CArray<BoneLinkRecords, BoneLinkElements>;

// The fake unit ("CFakeUnit"): a unit with the skin and the scene's arrays, which LoadFakeUnitScene fills. No
// scenario makes one (class 0x39 in the map object factory): only the builds check it.
class FakeUnitScene : public CUnit {
public:
    FakeUnitScene();
    ~FakeUnitScene() override;

    int Create() override;
    void Destroy() override;
    const char* GetClassName() override;
    int GetClassId() override;
    // On success each figure gets a new object of class 0x45 from the world's side-specific manager. Returns
    // the loader's 0, or 1.
    virtual int LoadScene(const char* file, const char* regFile);

    std::uint8_t unused_4DC[4];         // the skin's alignment (its clock is a double)
    SceneSkin skin;
    FigurePartArray figures;
    MeshArray meshes;
    BoneLinkArray boneLinks;
    HelperArray helpers;

private:
    // The start of both the destructor and Destroy.
    void ReleaseParts();
};

// The figure's .reg file's records: texture names and texture links by id.
struct TextureNameRecord {
    std::uint32_t id;
    std::string name;
};
struct TextureLinkRecord {
    std::uint32_t id;
    std::uint32_t value;
};
using TextureLinkArray = std::vector<TextureLinkRecord>;
// Zero-filled, the name empty; destroyed by the record's destructor.
struct TextureNameElements {
    static void Construct(TextureNameRecord* elements, int count) {
        std::memset(static_cast<void*>(elements), 0, static_cast<std::size_t>(count) * sizeof(TextureNameRecord));
        for (int i = 0; i < count; ++i) std::construct_at(&elements[i].name);
    }
    static void Destruct(TextureNameRecord* elements, int count) {
        for (int i = 0; i < count; ++i) figure::NamedRecordDestruct(&elements[i]);
    }
    static void Relocate(TextureNameRecord* to, TextureNameRecord* from, int count) { mfc::MoveElements(to, from, count); }
};
using TextureNameArray = mfc::CArray<TextureNameRecord, TextureNameElements>;
using TextureNameRecords = mfc::impl::ArrayAbi<TextureNameRecord, TextureNameElements>;
// The scene loader's {id, name} array.
struct TextureNameArrayClass final
    : mfc::impl::CompiledCollection<TextureNameArrayClass, &TextureNameRecords::Destruct<TextureNameArrayClass>,
                                    &TextureNameRecords::Serialize> {};

constexpr std::uint32_t kVersionWithNormals = 0x1468FCCA;

// The loader's `this`: only its first word is written.
class SceneInfo {
public:
    // Only the file's Read is used. `meshes` and `nodes` get one element per record. Returns 1.
    mfc::BOOL Load(lib::File* file, MeshArray* meshes, HelperArray* helpers, NodeArray* nodes);

    std::uint16_t flags_;
};

int LoadFigureRegistry(FakeUnitScene* unit, const std::string* path, TextureLinkArray* links,
                                  TextureNameArray* names);
// 0 without a root node.
int BuildSkeleton(MeshArray* meshArray, NodeArray* nodeArray, int frames, SceneSkin* skin);
// Welds the meshes' vertices to the registry's nodes.
void WeldMeshVertices(FakeUnitScene* unit, NodeArray* nodes);
void PackMeshPositions(FakeUnitScene* unit);
void ComputeMeshBounds(SceneMesh* mesh);

// Also builds the unit's figures from the scene.
mfc::BOOL LoadFakeUnitScene(const char* fileName, const char* registryName, FakeUnitScene* fakeUnit);

// The loader's messages and the vtable of the map it constructs inline.
namespace scenedata {
inline void* DwordMapVtable() { return mfc::impl::DwordMapClass::Vtable(); }

inline const char* CantOpenText() { return "Can't open file %s.\n"; }
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

