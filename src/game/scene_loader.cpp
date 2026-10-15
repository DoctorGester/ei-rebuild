// The CFakeUnit scene loader (00471B10). See scene_loader.h for the file layout.
//
// The original inlines MFC's CArray::SetSize and CMap::operator[] many times over; here they are
// the shared templates: mfc::CArray with the element constructors of each instantiation (plain ones
// zero-fill; the others call the original's out of line), lib::AlignedArray for the vertex arrays.
// The template functions the original calls out of line (SetAtGrow, the local arrays' destructors,
// ...) are reached through their original addresses.
#include "scene_loader.h"

#include "console.h"
#include "figure_mesh.h"
#include "map_object_figures.h"
#include "world.h"

#include "../lib/crt.h"
#include "../lib/mfc_coll.h"
#include "ei/hook.h"
#include "ei/vcall.h"
#include "originals_core.h"
#include "originals_engine.h"
#include "originals_library.h"
#include "originals_render.h"

#include <cstring>
#include <new>

#pragma warning(disable : 4740) // naked forwarders

namespace scene {
namespace {

// Through the file's vtable (slot 13), as the original reads.
void Read(mfc::CFileVirtuals* file, void* data, std::uint32_t size) { file->Read(data, size); }

// FLD + FSTP of a float, as the original copies some of the values it read (a signalling NaN comes
// out quiet).
__declspec(naked) std::uint32_t __cdecl FpuCopyBits(const void* /*value*/) {
    __asm {
        mov eax, dword ptr [esp + 4]
        fld dword ptr [eax]
        push eax
        fstp dword ptr [esp]
        pop eax
        ret
    }
}

// A float stored from the bits the loader copied.
void SetBits(float& to, std::uint32_t bits) { std::memcpy(&to, &bits, 4); }

// --- Element types and constructors of the arrays the loader fills ----------------------------------

__declspec(naked) void __stdcall ConstructMeshes(void*, int) EI_ORIGINAL(0x004545C0)
__declspec(naked) void __stdcall DestructMeshes(void*, int) EI_ORIGINAL(0x00476F50)
__declspec(naked) void __stdcall ConstructNodes(void*, int) EI_ORIGINAL(0x00454F80)
__declspec(naked) void __stdcall DestructNodes(void*, int) EI_ORIGINAL(0x00477100)
__declspec(naked) void __stdcall ConstructGroups(void*, int) EI_ORIGINAL(0x00454DB0)       // FaceGroup
__declspec(naked) void __stdcall DestructGroups(void*, int) EI_ORIGINAL(0x00476F20)
__declspec(naked) void __stdcall ConstructNodeMorphKeys(void*, int) EI_ORIGINAL(0x00454F30) // node +0xD0
__declspec(naked) void __stdcall DestructNodeMorphKeys(void*, int) EI_ORIGINAL(0x00453700)
__declspec(naked) void __stdcall ConstructMaps(void*, int) EI_ORIGINAL(0x00477380)         // CMap (0x1C)

// The records the file stores (the construction of all of them zero-fills: 00477130 .. 00477530,
// 00454ED0, 00454F00, 00452DC0, i.e. mfc::PlainElements).
struct FileUv {
    std::uint32_t v[3];   // u, v, unused
};
using FilePointArray = mfc::CArray<PointBits>;                // A: 12-byte positions
using FileUvArray = mfc::CArray<FileUv>;                      // B
using FileFaceArray = mfc::CArray<FileFace>;                  // C
using GroupRecordArray = mfc::CArray<FaceGroup, OriginalElements<FaceGroup, ConstructGroups, DestructGroups>>;   // D
// The file's key records: the frame word, then the values (the loader copies them through the
// FPU into the node's keys).
struct FileRotationKey {       // E (0x14): a vector at +8
    std::uint16_t frame;
    std::uint16_t unused_02;
    std::uint32_t unused_04;
    std::uint32_t value[3];
};
struct FilePositionKey {       // F (0x18): an axis at +8, the angle at +0x14
    std::uint16_t frame;
    std::uint16_t unused_02;
    std::uint32_t unused_04;
    std::uint32_t axis[3];
    std::uint32_t angle;
};
struct FileMorphHead {         // G (8): the frame (low word) and a second dword, read separately
    std::uint32_t frame;
    std::uint32_t extra;       // read and dropped
};
struct FileNormal {            // H (0x14): a normal at +8
    std::uint32_t unused_00[2];
    std::uint32_t value[3];
};
static_assert(sizeof(FileRotationKey) == 0x14 && sizeof(FilePositionKey) == 0x18);
static_assert(sizeof(FileMorphHead) == 8 && sizeof(FileNormal) == 0x14);
using RotKeyRecords = mfc::CArray<FileRotationKey>;          // E
using PosKeyRecords = mfc::CArray<FilePositionKey>;          // F
using MorphHeadRecords = mfc::CArray<FileMorphHead>;         // G
using NormalRecords = mfc::CArray<FileNormal>;               // H

using MeshRecordArray = mfc::CArray<SceneMesh, OriginalElements<SceneMesh, ConstructMeshes, DestructMeshes>>;
using NodeRecordArray = mfc::CArray<SceneNode, OriginalElements<SceneNode, ConstructNodes, DestructNodes>>;

using MapRecordArray = mfc::CArray<DwordMap, OriginalElements<DwordMap, ConstructMaps>>;

// Out-of-line members of the instantiations the loader reaches (the template with the original's address).
// The CMap<DWORD, DWORD> members: 00477340 GetAssocAt (the association of `key`, null if none; `hash`
// receives its bucket), 00476050 operator[] (the value of `key`, inserted as 0 when missing), 00476140
// the destructor.
__declspec(naked) MapAssoc* __fastcall MapGetAssocAt(const DwordMap*, void*, const std::uint32_t&, std::uint32_t&) EI_ORIGINAL(0x00477340)
__declspec(naked) std::uint32_t& __fastcall MapAt(DwordMap*, void*, const std::uint32_t&) EI_ORIGINAL(0x00476050)
__declspec(naked) void __fastcall MapDestruct(DwordMap*) EI_ORIGINAL(0x00476140)

using IndexArray = mfc::CArray<std::uint32_t>;   // CArray<DWORD, const DWORD&> (vtable 0073C800)
__declspec(naked) void __fastcall IndexSetAtGrow(IndexArray*, void*, int, const std::uint32_t&) EI_ORIGINAL(0x004773F0)
__declspec(naked) int __fastcall IndexAdd(IndexArray*, void*, const std::uint32_t&) EI_ORIGINAL(0x00476640)
__declspec(naked) void __fastcall IndexDestruct(IndexArray*) EI_ORIGINAL(0x00476780)
__declspec(naked) void __fastcall GroupsSetSize(mfc::CArray<FaceGroup>*, void*, int, int) EI_ORIGINAL(0x00474DB0)   // the mesh's groups (+0x148)
__declspec(naked) void __fastcall AssignGroup(FaceGroup*, void*, const FaceGroup*) EI_ORIGINAL(0x00474C50)

} // namespace


void NodeMorphKeyElements::Construct(NodeMorphKey* keys, int count) { ConstructNodeMorphKeys(keys, count); }
void NodeMorphKeyElements::Destruct(NodeMorphKey* keys, int count) { DestructNodeMorphKeys(keys, count); }

namespace {

// Destructors of the record's local arrays (they free the data; the elements are plain).
__declspec(naked) void __fastcall DestroyPoints(FilePointArray*) EI_ORIGINAL(0x00474F30)        // A
__declspec(naked) void __fastcall DestroyTexCoords(FileUvArray*) EI_ORIGINAL(0x00475110)     // B
__declspec(naked) void __fastcall DestroyFileFaces(FileFaceArray*) EI_ORIGINAL(0x004752F0)     // C
__declspec(naked) void __fastcall DestroyGroups(GroupRecordArray*) EI_ORIGINAL(0x00454CE0)        // D
__declspec(naked) void __fastcall DestroyRotKeys(RotKeyRecords*) EI_ORIGINAL(0x00475680)       // E
__declspec(naked) void __fastcall DestroyPosKeys(PosKeyRecords*) EI_ORIGINAL(0x004759E0)       // F
__declspec(naked) void __fastcall DestroyMorphHeads(MorphHeadRecords*) EI_ORIGINAL(0x00475D40)    // G
__declspec(naked) void __fastcall DestroyNormals(NormalRecords*) EI_ORIGINAL(0x00476AB0)       // H
__declspec(naked) void __fastcall DestroyMapArray(MapRecordArray*) EI_ORIGINAL(0x004763C0)

// The skipped records' locals, which the original constructs and sizes out of line.
__declspec(naked) void __fastcall ConstructRotKeyArray(RotKeyRecords*) EI_ORIGINAL(0x00475500)
__declspec(naked) void __fastcall ConstructPosKeyArray(PosKeyRecords*) EI_ORIGINAL(0x00475860)
__declspec(naked) void __fastcall ConstructMorphHeadArray(MorphHeadRecords*) EI_ORIGINAL(0x00475BC0)
__declspec(naked) void __fastcall ConstructNormalArray(NormalRecords*) EI_ORIGINAL(0x00476930)
__declspec(naked) void __fastcall SetSizeRotKeys(RotKeyRecords*, void*, int, int) EI_ORIGINAL(0x00475520)
__declspec(naked) void __fastcall SetSizePosKeys(PosKeyRecords*, void*, int, int) EI_ORIGINAL(0x00475880)
__declspec(naked) void __fastcall SetSizeNormals(NormalRecords*, void*, int, int) EI_ORIGINAL(0x00476950)
__declspec(naked) void __fastcall SetSizeMorphHeads(MorphHeadRecords*, void*, int, int) EI_ORIGINAL(0x00475BE0)
__declspec(naked) void __fastcall ConstructPointBlock(PointBuffer*) EI_ORIGINAL(0x00475F10)
__declspec(naked) void __fastcall SetSizePointBlock(PointBuffer*, void*, int, int) EI_ORIGINAL(0x00475F40)
__declspec(naked) void __fastcall DestroyPointBlock(PointBuffer*) EI_ORIGINAL(0x00475F20)

// The helpers array (+0x56C).
__declspec(naked) void __fastcall SetSizeHelpers(VtArray*, void*, int, int) EI_ORIGINAL(0x00453550)

// MFC.
void __fastcall SetSizeDwordArray(mfc::CArray<std::uint32_t>* a0, void*, int a2, int a3) { UIntArraySetSizeOriginal(a0, nullptr, a2, a3); }

// --- Inlined MFC templates -----------------------------------------------------------------------

// An empty array of the instantiation with `vtable`.
template <typename Records>
void InitArray(Records& a, void* vtable) {
    a = {};
    a.vtable = vtable;
}

void InitMap(DwordMap& map) {
    map.vtable = reinterpret_cast<void*>(scenedata::DwordMapVtable());
    map.table = nullptr;
    map.hashSize = 17;
    map.count = 0;
    map.freeList = nullptr;
    map.blocks = nullptr;
    map.blockSize = 10;
}

// --- Records ---------------------------------------------------------------------------------------

// The 0x2C-byte record header.
struct RecordHeader {
    std::uint16_t nameLength;      // +00
    std::uint16_t parentLength;    // +02
    int pointCount;                // +04 A: 12-byte positions (also the size of every morph key)
    int texCoordCount;             // +08 B: 12-byte (u, v, unused)
    int faceCount;                 // +0C C: FileFace
    int groupCount;                // +10 D: FaceGroup
    std::uint32_t offset[3];       // +14 the node's offset
    int rotKeyCount;               // +20 E: 0x14 bytes
    int posKeyCount;               // +24 F: 0x18 bytes (and H, the normals)
    int morphKeyCount;             // +28 G + 1
};
static_assert(sizeof(RecordHeader) == 0x2C);

#pragma pack(push, 1)
struct GroupHeader {
    std::uint16_t nameLength;
    std::uint32_t faceCount;
};
#pragma pack(pop)
static_assert(sizeof(GroupHeader) == 6);

// The arrays of one record, local to the loader (fr = the original's frame offsets).
struct RecordArrays {
    FilePointArray points;       // A (fr-0x21C)
    FileUvArray texCoords;       // B (fr-0x230)
    FileFaceArray faces;         // C (fr-0x268)
    GroupRecordArray groups;     // D (fr-0x280)
    RotKeyRecords rotKeys;       // E (fr-0x1CC)
    PosKeyRecords posKeys;       // F (fr-0x1F4)
    MorphHeadRecords morphHeads; // G (fr-0x1E0)
    NormalRecords normals;       // H (fr-0x208)

    std::uint32_t* Point(std::uint32_t index) { return points.data[index].v; }
    FileFace* Face(std::uint32_t index) { return &faces[static_cast<int>(index)]; }
    FaceGroup* Group(int index) { return &groups[index]; }
};

// The vertex merge of one mesh (fr-0x1B0 .. fr-0x12C).
struct VertexMerge {
    DwordMap seen;             // key -> 1 once the vertex exists in the current group
    DwordMap index;            // key -> merged vertex index
    MapRecordArray maps;       // never used: one CMap per group, allocated and freed
    IndexArray sources;        // merged vertex -> file position index
    PointBuffer positions;
    UvBuffer texCoords;
};

// One corner of a face: finds or adds its merged vertex (keyed by position and texture index) and
// writes the vertex index into both index triples of the mesh face.
void MergeCorner(RecordArrays& r, VertexMerge& m, MeshFace* meshFace, std::uint32_t faceIndex, int corner) {
    FileFace* face = r.Face(faceIndex);
    std::uint32_t texture = r.texCoords.size ? face->texture[corner] : 0;
    std::uint32_t key = face->position[corner] << 16 | texture;
    std::uint32_t hash;
    MapAssoc* assoc = MapGetAssocAt(&m.seen, nullptr, key, hash);
    if (!assoc || assoc->value == 0) {
        const std::uint32_t* point = r.Point(face->position[corner]);
        std::uint32_t x = FpuCopyBits(&point[0]);
        std::uint32_t u = 0, v = 0;
        if (r.texCoords.size) {
            const std::uint32_t* uv = r.texCoords.data[face->texture[corner]].v;
            u = uv[0];
            v = uv[1];
        }
        m.positions.SetAtGrow(m.positions.size, PointBits{{x, point[1], point[2]}});
        // The third corner goes through the out-of-line Add instantiations (same effect).
        if (corner < 2) {
            m.texCoords.SetAtGrow(m.texCoords.size, UvBits{{u, v}});
            IndexSetAtGrow(&m.sources, nullptr, m.sources.size, face->position[corner]);
        } else {
            m.texCoords.Add(UvBits{{u, v}});
            IndexAdd(&m.sources, nullptr, face->position[corner]);
        }
        MapAt(&m.index, nullptr, key) = static_cast<std::uint32_t>(m.positions.size - 1);
        MapAt(&m.seen, nullptr, key) = 1;
    }
    meshFace->vertex[corner] = MapAt(&m.index, nullptr, key);
    meshFace->vertex2[corner] = MapAt(&m.index, nullptr, key);
}

class Loader {
public:
    Loader(mfc::CFileVirtuals* file, std::uint32_t version) : file_(file), version_(version) {}

    void LoadRecord(int i, VtArray* meshes, VtArray* nodes);
    void SkipRecord();
    void LoadHelper(CMaterial* helper);

private:
    void SizeRecordArrays(RecordArrays& r, const RecordHeader& h);
    void ReadGroups(RecordArrays& r, const RecordHeader& h);
    void LoadNodeKeys(RecordArrays& r, const RecordHeader& h, SceneNode* node);
    void LoadMorphKeys(RecordArrays& r, const RecordHeader& h, SceneNode* node);
    void BuildMesh(RecordArrays& r, SceneMesh* mesh, SceneNode* node);

    mfc::CFileVirtuals* file_;
    std::uint32_t version_;
};

void Loader::SizeRecordArrays(RecordArrays& r, const RecordHeader& h) {
    r.points.SetSize(h.pointCount);
    r.texCoords.SetSize(h.texCoordCount);
    r.faces.SetSize(h.faceCount);
    r.groups.SetSize(h.groupCount);
    r.rotKeys.SetSize(h.rotKeyCount);
    r.posKeys.SetSize(h.posKeyCount);
    r.morphHeads.SetSize(h.morphKeyCount - 1);
}

// Each group: u16 name length, u32 face count, the name, the face indices.
void Loader::ReadGroups(RecordArrays& r, const RecordHeader& h) {
    for (int g = 0; g < h.groupCount; ++g) {
        GroupHeader gh;
        Read(file_, &gh, sizeof(gh));
        FaceGroup* group = r.Group(g);
        Read(file_, group->name.GetBufferSetLength(gh.nameLength), gh.nameLength);
        SetSizeDwordArray(&group->faces, nullptr, static_cast<int>(gh.faceCount), -1);
        Read(file_, group->faces.data, gh.faceCount * 4);
    }
}

// The node's rotation and position keys (and normals in the newer version), copied field by
// field from the file records.
void Loader::LoadNodeKeys(RecordArrays& r, const RecordHeader& h, SceneNode* node) {
    Read(file_, r.rotKeys.data, r.rotKeys.size * 0x14);
    auto& rot = node->rotationKeys;
    rot.SetSize(r.rotKeys.size);
    for (int k = 0; k < rot.size; ++k) {
        const FileRotationKey& from = r.rotKeys.data[k];
        NodeRotationKey& to = rot.data[k];
        to.frame = static_cast<std::int16_t>(from.frame);
        std::uint32_t z = FpuCopyBits(&from.value[2]);
        std::uint32_t y = FpuCopyBits(&from.value[1]);
        std::uint32_t x = FpuCopyBits(&from.value[0]);
        SetBits(to.value.x, x);
        SetBits(to.value.y, y);
        SetBits(to.value.z, z);
    }

    Read(file_, r.posKeys.data, r.posKeys.size * 0x18);
    auto& pos = node->positionKeys;
    pos.SetSize(r.posKeys.size);
    for (int k = 0; k < pos.size; ++k) {
        const FilePositionKey& from = r.posKeys.data[k];
        NodePositionKey& to = pos.data[k];
        to.frame = static_cast<std::int16_t>(from.frame);
        std::uint32_t z = FpuCopyBits(&from.axis[2]);
        std::uint32_t y = FpuCopyBits(&from.axis[1]);
        std::uint32_t x = FpuCopyBits(&from.axis[0]);
        SetBits(to.axis.x, x);
        SetBits(to.axis.y, y);
        SetBits(to.axis.z, z);
        SetBits(to.angle, from.angle);
    }

    if (version_ != kVersionWithNormals) return;
    r.normals.SetSize(h.posKeyCount);
    auto& normals = node->normals;
    normals.SetSize(h.posKeyCount);
    Read(file_, r.normals.data, r.normals.size * 0x14);
    for (int k = 0; k < normals.size; ++k) {
        const FileNormal& from = r.normals.data[k];
        Vector3& to = normals.data[k];
        std::uint32_t z = FpuCopyBits(&from.value[2]);
        std::uint32_t y = FpuCopyBits(&from.value[1]);
        std::uint32_t x = FpuCopyBits(&from.value[0]);
        SetBits(to.x, x);
        SetBits(to.y, y);
        SetBits(to.z, z);
    }
}

// The vertex animation keys: per key two dwords (the first word is the frame) and a full set of
// positions, kept in the node as an aligned point array.
void Loader::LoadMorphKeys(RecordArrays& r, const RecordHeader& h, SceneNode* node) {
    auto& keys = node->morphKeys;
    keys.SetSize(r.morphHeads.size);
    for (int k = 0; k < r.morphHeads.size; ++k) {
        int count = h.pointCount;
        PointBits* points = count ? PointBuffer::Allocate(count) : nullptr;
        FileMorphHead& head = r.morphHeads.data[k];
        Read(file_, &head.frame, 4);
        Read(file_, &head.extra, 4);
        Read(file_, points, h.pointCount * 12);
        NodeMorphKey& key = keys.data[k];
        key.frame = static_cast<std::int16_t>(head.frame);
        key.points.SetSize(count);
        for (int p = 0; p < count; ++p) std::memcpy(&key.points.data[p], &points[p], sizeof(PointBits));
        if (points) PointBuffer::Free(points);
    }
}

// Faces, merged vertices and groups of the mesh; then the morph keys are reordered to the merged
// vertices.
void Loader::BuildMesh(RecordArrays& r, SceneMesh* mesh, SceneNode* node) {
    FaceBuffer& meshFaces = mesh->faces;
    meshFaces.SetSize(r.faces.size);
    for (int f = 0; f < r.faces.size; ++f) {
        const std::uint32_t* from = reinterpret_cast<const std::uint32_t*>(r.Face(f));
        std::uint32_t* to = reinterpret_cast<std::uint32_t*>(&meshFaces.data[f]);
        for (int d = 0; d < 6; ++d) to[d] = from[d];
        to[6] = from[0];
        to[7] = from[1];
        to[8] = from[2];
    }

    VertexMerge m;
    InitMap(m.seen);
    InitMap(m.index);
    InitArray(m.maps, scenedata::MapArrayVtable());
    InitArray(m.sources, scenedata::IndexArrayVtable());
    m.positions = {};
    m.texCoords = {};

    // One map per group, never used (the original allocates them all the same).
    m.maps.SetSize(r.groups.size);

    for (int g = 0; g < r.groups.size; ++g) {
        m.seen.RemoveAll();
        FaceGroup* group = r.Group(g);
        for (int j = 0; j < group->faces.size; ++j) {
            std::uint32_t faceIndex = r.Group(g)->faces.data[j];
            for (int corner = 0; corner < 3; ++corner) {
                MeshFace* meshFace = &mesh->faces.data[faceIndex];
                MergeCorner(r, m, meshFace, faceIndex, corner);
            }
        }
    }

    mesh->positions.Copy(m.positions);
    mesh->texCoords.Copy(m.texCoords);
    m.positions.SetSize(0);
    m.texCoords.SetSize(0);
    mfc::CArray<FaceGroup>* groups = &mesh->groups;
    GroupsSetSize(groups, nullptr, r.groups.size, -1);
    for (int g = 0; g < r.groups.size; ++g) {
        AssignGroup(&groups->data[g], nullptr, r.Group(g));
    }

    // Every morph key's points, reordered to the merged vertices.
    auto& morph = node->morphKeys;
    for (int k = 0; k < morph.size; ++k) {
        lib::AlignedArray<Vector3> reordered{};
        reordered.SetSize(m.sources.size);
        for (int v = 0; v < m.sources.size; ++v) {
            std::uint32_t source = m.sources.data[v];
            std::memcpy(&reordered.data[v], &morph.data[k].points.data[source], sizeof(Vector3));   // as dwords
        }
        morph.data[k].points.Copy(reordered);
        reordered.FreeData();
    }

    m.texCoords.FreeData();
    m.positions.FreeData();
    IndexDestruct(&m.sources);
    DestroyMapArray(&m.maps);
    MapDestruct(&m.index);
    MapDestruct(&m.seen);
}

// Record i: node i and mesh i.
void Loader::LoadRecord(int i, VtArray* meshes, VtArray* nodes) {
    mfc::CString name;
    mfc::CString parent;
    RecordArrays r;
    InitArray(r.normals, scenedata::NormalArrayVtable());
    InitArray(r.points, scenedata::PointArrayVtable());
    InitArray(r.texCoords, scenedata::TexCoordArrayVtable());
    InitArray(r.faces, scenedata::FileFaceArrayVtable());
    InitArray(r.groups, scenedata::GroupArrayVtable());
    InitArray(r.rotKeys, scenedata::RotKeyArrayVtable());
    InitArray(r.posKeys, scenedata::PosKeyArrayVtable());
    InitArray(r.morphHeads, scenedata::MorphHeadArrayVtable());

    RecordHeader h;
    Read(file_, &h, sizeof(h));
    SceneNode* node = &Typed<NodeArray>(*nodes).data[i];
    SceneMesh* mesh = &Typed<MeshArray>(*meshes).data[i];
    std::uint32_t x = FpuCopyBits(&h.offset[0]);
    std::uint32_t y = FpuCopyBits(&h.offset[1]);
    std::uint32_t z = FpuCopyBits(&h.offset[2]);
    SetBits(node->offset.x, x);
    SetBits(node->offset.y, y);
    SetBits(node->offset.z, z);

    SizeRecordArrays(r, h);
    Read(file_, name.GetBufferSetLength(h.nameLength), h.nameLength);
    Read(file_, parent.GetBufferSetLength(h.parentLength), h.parentLength);
    node->name = name;
    node->parent = parent;
    mesh->name = name;

    Read(file_, r.points.data, h.pointCount * 12);
    Read(file_, r.texCoords.data, h.texCoordCount * 12);
    Read(file_, r.faces.data, r.faces.size * sizeof(FileFace));
    ReadGroups(r, h);
    LoadNodeKeys(r, h, node);
    LoadMorphKeys(r, h, node);
    BuildMesh(r, mesh, node);

    DestroyNormals(&r.normals);
    DestroyMorphHeads(&r.morphHeads);
    DestroyPosKeys(&r.posKeys);
    DestroyRotKeys(&r.rotKeys);
    DestroyGroups(&r.groups);
    DestroyFileFaces(&r.faces);
    DestroyTexCoords(&r.texCoords);
    DestroyPoints(&r.points);
}

// A record after the meshes: read in full and dropped. Only its names, keys, normals and morph
// keys are read (its point, texture, face and group counts are expected to be zero).
void Loader::SkipRecord() {
    mfc::CString name;
    mfc::CString parent;
    RotKeyRecords rotKeys;
    PosKeyRecords posKeys;
    MorphHeadRecords morphHeads;
    NormalRecords normals;
    ConstructRotKeyArray(&rotKeys);
    ConstructPosKeyArray(&posKeys);
    ConstructMorphHeadArray(&morphHeads);
    ConstructNormalArray(&normals);

    RecordHeader h;
    Read(file_, &h, sizeof(h));
    SetSizeRotKeys(&rotKeys, nullptr, h.rotKeyCount, -1);
    SetSizePosKeys(&posKeys, nullptr, h.posKeyCount, -1);
    SetSizeNormals(&normals, nullptr, h.posKeyCount, -1);
    SetSizeMorphHeads(&morphHeads, nullptr, h.morphKeyCount - 1, -1);
    Read(file_, name.GetBufferSetLength(h.nameLength), h.nameLength);
    Read(file_, parent.GetBufferSetLength(h.parentLength), h.parentLength);
    Read(file_, rotKeys.data, rotKeys.size * 0x14);
    Read(file_, posKeys.data, posKeys.size * 0x18);
    if (version_ == kVersionWithNormals) Read(file_, normals.data, normals.size * 0x14);
    for (int k = 0; k < morphHeads.size; ++k) {
        PointBuffer points;
        ConstructPointBlock(&points);
        SetSizePointBlock(&points, nullptr, h.pointCount, -1);
        Read(file_, &morphHeads.data[k].frame, 4);
        Read(file_, &morphHeads.data[k].extra, 4);
        Read(file_, points.data, h.pointCount * 12);
        DestroyPointBlock(&points);
    }
    DestroyNormals(&normals);
    DestroyMorphHeads(&morphHeads);
    DestroyPosKeys(&posKeys);
    DestroyRotKeys(&rotKeys);
}

// A helper (0x3E-byte header, then three names).
void Loader::LoadHelper(CMaterial* helper) {
#pragma pack(push, 1)
    struct HelperHeader {
        std::uint32_t ambient[3];    // +00 colors.ambient (r, g, b)
        std::uint32_t diffuse[3];    // +0C colors.diffuse
        std::uint32_t specular[3];   // +18 colors.specular
        std::uint32_t power;         // +24 colors.power
        std::uint32_t unused_28[2];  // +28
        std::uint32_t diffuseAlpha;  // +30 colors.diffuse.w
        std::uint32_t unused_34;     // +34
        std::uint16_t nameLength;    // +38
        std::uint16_t name2Length;   // +3A
        std::uint16_t name3Length;   // +3C
    };
#pragma pack(pop)
    static_assert(sizeof(HelperHeader) == 0x3E);

    mfc::CString text;
    HelperHeader h;
    Read(file_, &h, sizeof(h));
    std::memcpy(&helper->colors.ambient, h.ambient, 12);
    std::memcpy(&helper->colors.diffuse, h.diffuse, 12);
    std::memcpy(&helper->colors.specular, h.specular, 12);
    std::memcpy(&helper->colors.diffuse.w, &h.diffuseAlpha, 4);
    std::memcpy(&helper->colors.power, &h.power, 4);
    Read(file_, text.GetBufferSetLength(h.nameLength), h.nameLength);
    helper->name = static_cast<const char*>(text);
    Read(file_, text.GetBufferSetLength(h.name2Length), h.name2Length);
    helper->name2 = static_cast<const char*>(text);
    Read(file_, text.GetBufferSetLength(h.name3Length), h.name3Length);
    helper->name3 = static_cast<const char*>(text);
}

} // namespace

mfc::BOOL SceneInfo::Load(mfc::CFileVirtuals* file, VtArray* meshes, VtArray* helpers, VtArray* nodes) {
    struct FileHeader {
        std::uint32_t version;
        int recordCount;
        int skippedCount;
        int helperCount;
        std::uint16_t flags;
        std::uint16_t unused_12;
    } header;
    static_assert(sizeof(FileHeader) == 0x14);
    Read(file, &header, sizeof(header));
    flags_ = header.flags;

    Typed<MeshRecordArray>(*meshes).SetSize(header.recordCount);
    Typed<NodeRecordArray>(*nodes).SetSize(header.recordCount);

    Loader loader(file, header.version);
    for (int i = 0; i < meshes->size; ++i) loader.LoadRecord(i, meshes, nodes);
    for (int i = 0; i < header.skippedCount; ++i) loader.SkipRecord();

    SetSizeHelpers(helpers, nullptr, header.helperCount, -1);
    for (int i = 0; i < helpers->size; ++i) loader.LoadHelper(&Typed<HelperArray>(*helpers).data[i]);
    return 1;
}
EI_REPLACE_METHOD(0x00471B10, SceneInfo::Load);

void FaceGroup::Assign(const FaceGroup* from) {
    name = from->name;
    faces.Copy(from->faces);
}
EI_REPLACE_METHOD(0x00474C50, FaceGroup::Assign);

// ---- Element constructors and destructors (S7a) --------------------------------------------------

namespace {

mfc::impl::CDWordArray* Dwords(mfc::CArray<std::uint32_t>* faces) {
    return reinterpret_cast<mfc::impl::CDWordArray*>(faces);
}

// The arrays' vtables the destructors write back.
constexpr std::uintptr_t kFaceGroupArrayVtable = std::uintptr_t(0x0073C2BC);
constexpr std::uintptr_t kRotationKeysVtable = std::uintptr_t(0x0073C1AC);
constexpr std::uintptr_t kPositionKeysVtable = std::uintptr_t(0x0073C1B8);
constexpr std::uintptr_t kMorphKeysVtable = std::uintptr_t(0x0073C114);
constexpr std::uintptr_t kNormalsVtable = std::uintptr_t(0x0073C120);
constexpr std::uintptr_t kFramesVtable = std::uintptr_t(0x0073C12C);
constexpr std::uintptr_t kChildrenVtable = std::uintptr_t(0x0073C138);

template <typename A>
void FreeArray(A& array, std::uintptr_t vtable) {
    array.vtable = reinterpret_cast<void*>(vtable);
    array.FreeData();
}

} // namespace

void __stdcall FaceGroup::ConstructElements(FaceGroup* groups, int count) {
    std::memset(static_cast<void*>(groups), 0, static_cast<std::size_t>(count) * sizeof(FaceGroup));
    for (; count > 0; --count, ++groups) {
        new (&groups->name) mfc::CString();
        Dwords(&groups->faces)->Construct();
    }
}
EI_REPLACE(0x00454DB0, FaceGroup::ConstructElements);

void FaceGroup::Destruct() {
    Dwords(&faces)->SetSize(0, -1);
    name.Empty();
    Dwords(&faces)->Destruct();
    name.~CString();
}

void __fastcall FaceGroupArrayDestruct(mfc::CArray<FaceGroup>* groups) {
    groups->vtable = reinterpret_cast<void*>(kFaceGroupArrayVtable);
    if (!groups->data) return;
    for (int i = 0; i < groups->size; ++i) groups->data[i].Destruct();
    gamecrt::operator_delete(groups->data);
}
EI_REPLACE(0x00454CE0, FaceGroupArrayDestruct);

void SceneNode::Destruct() {
    FreeArray(children, kChildrenVtable);
    FreeArray(frames, kFramesVtable);
    FreeArray(normals, kNormalsVtable);
    FreeArray(morphKeys, kMorphKeysVtable);
    FreeArray(positionKeys, kPositionKeysVtable);
    FreeArray(rotationKeys, kRotationKeysVtable);
    parent.~CString();
    name.~CString();
}
EI_REPLACE_METHOD(0x004543E0, SceneNode::Destruct);

// ---- Element constructors (S6b; their ConstructElements are record_arrays.cpp's) -----------------

// 00454F80's element. The original then sizes the rotation and position keys, the frames and the
// children to 0 (00455170, 004552B0, 00450F20, 00453790), which leaves empty arrays as they are.
void SceneNode::Construct() {
    new (&name) mfc::CString();
    new (&parent) mfc::CString();
    spareRotation030.SetIdentity();
    spareRotation040.SetIdentity();
    rotation.SetIdentity();
    restRotation.SetIdentity();
    rotationKeys.Construct(kRotationKeysVtable);
    positionKeys.Construct(kPositionKeysVtable);
    morphKeys.Construct(kMorphKeysVtable);
    normals.Construct(kNormalsVtable);
    frames.Construct(kFramesVtable);
    children.Construct(kChildrenVtable);   // 00455150
}

// 004545C0's element.
void SceneMesh::Construct() {
    ConstructBase();
    new (&name) mfc::CString();
    groups.Construct(kFaceGroupArrayVtable);
    ei::SetVtable(this, kVtableAddress);
}

// --- 00446C10: building a CFakeUnit's figure from its scene ---------------------------------------

namespace {

int Length(const mfc::CString& s) { return s.GetLength(); }

// The figure meshes are engine objects (class 0x42; CFigureMesh keeps its vtable as a field): slot
// 7 (GetName) gives the name.
CEngineObject* FigureObject(CFigureMesh* figure) { return reinterpret_cast<CEngineObject*>(figure); }

// Not rewritten.
__declspec(naked) void __stdcall ConstructFigures(void*, int) EI_ORIGINAL(0x00453240)
__declspec(naked) void __stdcall DestructFigures(void*, int) EI_ORIGINAL(0x00453210)
__declspec(naked) void __stdcall ConstructBoneLinks(void*, int) EI_ORIGINAL(0x004534F0)
__declspec(naked) void __stdcall DestructBoneLinks(void*, int) EI_ORIGINAL(0x004534C0)
using FigureRecordArray = mfc::CArray<CFigureMesh, OriginalElements<CFigureMesh, ConstructFigures, DestructFigures>>;
// Per figure, a copy of its node's frame records (NodeFrame, 0x28 bytes each).
using BoneLinkArray = mfc::CArray<VtArray, OriginalElements<VtArray, ConstructBoneLinks, DestructBoneLinks>>;
__declspec(naked) void __fastcall SetSizeBoneLinks(VtArray*, void*, int, int) EI_ORIGINAL(0x00450370)       // unit +0x558
void __fastcall SetSizeFigures(VtArray* a0, void*, int a2, int a3) { FigureArraySetSizeOriginal((void*)a0, nullptr, a2, a3); }
__declspec(naked) void __fastcall SetSizeBoneLinkRecords(VtArray*, void*, int, int) EI_ORIGINAL(0x00450F20) // 0x28-byte records
__declspec(naked) void __fastcall DestructNode(void*) EI_ORIGINAL(0x004543E0)
// Reads the figure's .reg file: the texture names ({id, name}) and links ({id, value}).
__declspec(naked) void __fastcall LoadFigureRegistry(void*, const mfc::CString*, VtArray*, VtArray*) EI_ORIGINAL(0x0044B980)
__declspec(naked) void __fastcall LinkNodes(void*, VtArray*) EI_ORIGINAL(0x0044C4A0)
__declspec(naked) mfc::BOOL __fastcall BuildSkeleton(VtArray*, VtArray*, int, void*) EI_ORIGINAL(0x00448A40)
__declspec(naked) void __fastcall PlaceMeshes(void*) EI_ORIGINAL(0x0044E3B0)
__declspec(naked) void __fastcall PrepareMeshes(VtArray*) EI_ORIGINAL(0x0044DCF0)
__declspec(naked) void __fastcall PrepareMeshes2(VtArray*) EI_ORIGINAL(0x0044CFF0)
__declspec(naked) void __fastcall PrepareMesh(void*) EI_ORIGINAL(0x0044E8A0)
__declspec(naked) SceneTexture* __fastcall FindTexture(void*, void*, std::uint32_t) EI_ORIGINAL(0x0051B2F0)
__declspec(naked) SceneTexture* __fastcall AddTextureLink(void*, void*, std::uint32_t, std::uint32_t) EI_ORIGINAL(0x0051B470)
int __fastcall RegisterTexture(void* a0, void*, void* a2, std::uint32_t a3) { return MaterialRegisterOriginal(a0, nullptr, a2, a3); }
__declspec(naked) void __fastcall FinishFigure(void*) EI_ORIGINAL(0x005A6D40)
__declspec(naked) void __fastcall AddFigure(void*, void*, void*) EI_ORIGINAL(0x005164B0)
__declspec(naked) int __fastcall BindSkin(void*, void*, void*) EI_ORIGINAL(0x00508BF0)
__declspec(naked) void __fastcall ResetSkin(void*) EI_ORIGINAL(0x00508990)
// SceneInfo::Load through its original address, so the verify build's wrapper sees the call.
__declspec(naked) mfc::BOOL __fastcall LoadScene(SceneInfo*, void*, void*, VtArray*, VtArray*, VtArray*) EI_ORIGINAL(0x00471B10)

// The loader's locals that need cleaning up, as small owners. Declared in the original's
// construction order, so C++ destroys them in its order.
struct SceneNodes : VtArray {
    SceneNodes() : VtArray{scenedata::NodeArrayVtable(), nullptr, 0, 0, 0} {}
    ~SceneNodes() {
        if (!data) return;
        for (int i = 0; i < size; ++i) DestructNode(&Typed<NodeArray>(*this).data[i]);
        gamecrt::operator_delete(data);
    }
};
struct Registry {
    std::uint8_t bytes[0x18];   // CFileRegistry, never used
    Registry() { reinterpret_cast<RegistryOriginals*>(bytes)->Construct(); }
    ~Registry() { reinterpret_cast<RegistryOriginals*>(bytes)->Destroy(); }
};
struct TextureLinks : VtArray {
    TextureLinks() : VtArray{scenedata::TextureLinkArrayVtable(), nullptr, 0, 0, 0} {}
    ~TextureLinks() {
        if (data) gamecrt::operator_delete(data);
    }
    const TextureLinkRecord& Record(int i) const { return reinterpret_cast<const TextureLinkRecord*>(data)[i]; }
};
struct TextureNames : VtArray {
    TextureNames() : VtArray{scenedata::TextureNameArrayVtable(), nullptr, 0, 0, 0} {}
    ~TextureNames() {
        if (!data) return;
        for (int i = 0; i < size; ++i) DestructTextureName(&Records()[i]);
        gamecrt::operator_delete(data);
    }
    TextureNameRecord* Records() { return reinterpret_cast<TextureNameRecord*>(data); }
    std::uint32_t Id(int i) { return Records()[i].id; }
    mfc::CString& Name(int i) { return Records()[i].name; }
};

struct SceneLoad {
    mfc::CString path;
    SceneNodes nodes;
    Registry registry;
    mfc::CFile file;
    mfc::CString unused90, unused8C, unused94;
    console::CStringArray unused40;
    TextureLinks textureLinks;
    TextureNames textureNames;
    console::CStringArray roots;   // the nodes without parent
    // The scene's flags. BuildSkeleton is given them as a dword minus one; the original leaves the
    // upper word uninitialised (its callees only read the low word, with MOVSX).
    union {
        SceneInfo info;
        std::uint32_t infoDword;
    };

    explicit SceneLoad(const char* fileName) : path(fileName), infoDword(0) {}
};

} // namespace

// 00446C10. Loads the scene `fileName` (an .amf) into the fake unit: meshes, helpers and nodes,
// then the figure's .reg file (`registryName`, or "Ini's\\<name>.reg" for a scene under
// "scenes\\"), the figures, skeleton and textures. Returns 0 if the file cannot be opened or the
// scene has more than one top-level node (a fatal error). The caller also passes the unit in ECX;
// it is not read.
mfc::BOOL __stdcall LoadFakeUnitScene(const char* fileName, const char* registryName, FakeUnitScene* fakeUnit) {
    FakeUnitScene* u = fakeUnit;
    VtArray* meshes = &u->meshes;
    VtArray* helpers = &u->helpers;
    VtArray* figures = &u->figures;
    VtArray* boneLinks = &u->boneLinks;

    SceneLoad s(fileName);
    SetSizeBoneLinks(boneLinks, nullptr, 0, -1);
    SetSizeHelpers(helpers, nullptr, 0, -1);
    SetSizeFigures(figures, nullptr, 0, -1);

    if (!s.file.Open(fileName, 0x8000, nullptr)) {
        mfc::CString message;
        message.Format(scenedata::CantOpenText(), fileName);
        AfxMessageBox(message, 0x10, 0);
        return 0;
    }
    LoadScene(&s.info, nullptr, &s.file, meshes, helpers, &s.nodes);
    s.file.Close();

    // Exactly one node may have no parent.
    int tops = 0;
    for (int i = 0; i < s.nodes.size; ++i) {
        SceneNode& n = Typed<NodeArray>(s.nodes).data[i];
        if (Length(n.parent) == 0) {
            ++tops;
            s.roots.SetAtGrow(s.roots.size, n.name);
        }
    }
    if (tops > 1) {
        mfc::CString message;
        message.Format(scenedata::ManyTopsText(), fileName);
        for (int i = 0; i < s.roots.size; ++i) message += s.roots[i] + scenedata::NewLineText();
        game::FatalError(message);
        return 0;
    }

    // The figure's registry file.
    int ext = s.path.Find(scenedata::SceneExtText());
    if (ext != -1) {
        if (!registryName) {
            if (s.path.Find(scenedata::ScenesDirText()) == 0) {
                s.path = scenedata::IniDirText() + s.path.Right(Length(s.path) - 7);
                --ext;
            }
            s.path = s.path.Left(ext) + scenedata::RegExtText();
        } else {
            s.path = mfc::CString(registryName);
        }
        LoadFigureRegistry(fakeUnit, &s.path, &s.textureLinks, &s.textureNames);
        LinkNodes(fakeUnit, &s.nodes);
    }

    // One figure per mesh.
    FigureRecordArray& figureArray = Typed<FigureRecordArray>(*figures);
    MeshArray& meshArray = Typed<MeshArray>(*meshes);
    NodeArray& nodeArray = Typed<NodeArray>(s.nodes);
    figureArray.SetSize(meshes->size);
    for (int i = 0; i < meshes->size; ++i) {
        figureArray.data[i].name_ = meshArray.data[i].GetName();
        figureArray.data[i].materialValue_ = static_cast<std::uint32_t>(meshArray.data[i].materialValue);
        figureArray.data[i].meshType_ = static_cast<std::uint32_t>(meshArray.data[i].materialType);
    }

    if (!BuildSkeleton(meshes, &s.nodes, static_cast<int>(s.infoDword - 1), &u->skin))
        game::FatalError(scenedata::ModelNameText(), fileName);
    PlaceMeshes(fakeUnit);

    // Each figure takes the bone links of the nodes of the same name.
    BoneLinkArray& linkArray = Typed<BoneLinkArray>(*boneLinks);
    linkArray.SetSize(figures->size);
    for (int f = 0; f < figures->size; ++f) {
        for (int n = 0; n < s.nodes.size; ++n) {
            const char* name = FigureObject(&figureArray.data[f])->GetName();
            SceneNode& nd = nodeArray.data[n];
            if (gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(static_cast<const char*>(nd.name)),
                                reinterpret_cast<const unsigned char*>(name)) != 0)
                continue;
            VtArray* links = &linkArray.data[f];
            const VtArray* from = &nd.frames;
            SetSizeBoneLinkRecords(links, nullptr, from->size, -1);
            std::memcpy(links->data, from->data, from->size * 0x28);
        }
    }

    PrepareMeshes(meshes);
    PrepareMeshes2(meshes);
    for (int i = 0; i < meshes->size; ++i) PrepareMesh(&meshArray.data[i]);

    // The figures take the meshes' matrices (64-byte blocks: the figure's vertex and normal
    // packets); their colour arrays get four entries per matrix.
    auto copyBlocks = [](auto& to, const BlockBuffer& from) {
        to.SetSize(from.size);
        std::memcpy(static_cast<void*>(to.data), from.data, static_cast<std::size_t>(from.size) << 6);
    };
    for (int i = 0; i < meshes->size; ++i) {
        CFigureMesh& figure = figureArray.data[i];
        const SceneMesh& mesh = meshArray.data[i];
        copyBlocks(figure.vertices_, mesh.positionBlocks);
        copyBlocks(figure.normals_, mesh.normalBlocks);
        figure.colorsA_.SetSize(figure.normals_.size * 4);
        figure.colorsB_.SetSize(figure.normals_.size * 4);
        std::memcpy(&figure.center_, &mesh.center, sizeof(Vector3));
        std::memcpy(&figure.boxMin_, &mesh.boxMin, sizeof(Vector3));
        std::memcpy(&figure.boxMax_, &mesh.boxMax, sizeof(Vector3));
        std::memcpy(&figure.radius_, &mesh.radius, sizeof(float));
    }

    // The textures named in the registry file (without ".bmp") and their links.
    for (int t = 0; t < s.textureNames.size; ++t) {
        void* owner = u->Object3D()->attached;
        SceneTexture* texture = FindTexture(owner, nullptr, s.textureNames.Id(t));
        bool bmp = gamecrt::mbsicmp(reinterpret_cast<const unsigned char*>(static_cast<const char*>(s.textureNames.Name(t).Right(4))),
                                    reinterpret_cast<const unsigned char*>(scenedata::BmpExtText())) == 0;
        if (bmp) {
            const mfc::CString& name = s.textureNames.Name(t);
            texture->name = static_cast<const char*>(name.Left(Length(name) - 4));
        } else {
            texture->name = static_cast<const char*>(s.textureNames.Name(t));
        }
        for (int l = 0; l < s.textureLinks.size; ++l) {
            // The manager context's slot 33 (CWorld::MaterialManager).
            void* manager = static_cast<CWorld*>(ManagerContextOf(fakeUnit))->MaterialManager();
            std::uint32_t id = s.textureLinks.Record(l).id;
            if (id != s.textureNames.Id(t)) continue;
            SceneTexture* linked = AddTextureLink(owner, nullptr, id, s.textureLinks.Record(l).value);
            linked->name = static_cast<const char*>(s.textureNames.Name(t));
            RegisterTexture(manager, nullptr, linked, s.textureLinks.Record(l).value);
        }
    }

    for (int f = 0; f < figures->size; ++f) {
        FinishFigure(&figureArray.data[f]);
        AddFigure(fakeUnit, nullptr, &figureArray.data[f]);
    }
    BindSkin(&u->skin, nullptr, &u->Object3D()->parts);
    ResetSkin(&u->skin);
    const std::uint32_t offset[3] = {0, 0, 0x44FA0000};   // (0, 0, 500)
    std::memcpy(&u->Object3D()->position, offset, sizeof(offset));
    return 1;
}
EI_REPLACE(0x00446C10, LoadFakeUnitScene);

} // namespace scene
