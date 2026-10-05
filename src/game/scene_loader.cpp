// The CFakeUnit scene loader (00471B10). See scene_loader.h for the file layout.
//
// The original inlines MFC's CArray::SetSize and CMap::operator[] many times over; here they are
// one helper each (SetSizeInline for arrays allocated with new, SetSizeAligned for the aligned
// vertex arrays). The template functions the original calls out of line (element constructors,
// SetAtGrow, Copy, the local arrays' destructors, ...) are reached through their original
// addresses.
#include "scene_loader.h"

#include "console.h"

#include "../lib/crt.h"
#include "ei/hook.h"
#include "ei/vcall.h"
#include "originals_core.h"
#include "originals_engine.h"
#include "originals_library.h"
#include "originals_render.h"

#include <cstring>

#pragma warning(disable : 4740) // naked forwarders

namespace scene {
namespace {

constexpr int kFileRead = 13;   // CFile slot 13

void Read(void* file, void* data, std::uint32_t size) { ei::VCall<std::uint32_t>(file, kFileRead, data, size); }

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

template <typename T = std::uint8_t>
T* At(void* base, std::size_t offset) { return reinterpret_cast<T*>(static_cast<char*>(base) + offset); }

std::uint32_t Dword(const void* p, std::size_t offset) {
    std::uint32_t v;
    std::memcpy(&v, static_cast<const char*>(p) + offset, 4);
    return v;
}
std::uint16_t Word(const void* p, std::size_t offset) {
    std::uint16_t v;
    std::memcpy(&v, static_cast<const char*>(p) + offset, 2);
    return v;
}
void SetDword(void* p, std::size_t offset, std::uint32_t v) { std::memcpy(static_cast<char*>(p) + offset, &v, 4); }
void SetWord(void* p, std::size_t offset, std::uint16_t v) { std::memcpy(static_cast<char*>(p) + offset, &v, 2); }

// --- Template instantiations called out of line (not rewritten) ---------------------------------

// ConstructElements / DestructElements(elements, count) of the arrays the loader fills.
using ElementFn = void(__stdcall*)(void* elements, int count);
__declspec(naked) void __stdcall ConstructMeshes(void*, int) EI_ORIGINAL(0x004545C0)
__declspec(naked) void __stdcall DestructMeshes(void*, int) EI_ORIGINAL(0x00476F50)
__declspec(naked) void __stdcall ConstructNodes(void*, int) EI_ORIGINAL(0x00454F80)
__declspec(naked) void __stdcall DestructNodes(void*, int) EI_ORIGINAL(0x00477100)
__declspec(naked) void __stdcall ConstructPoints(void*, int) EI_ORIGINAL(0x00477130)       // A: 12 bytes, zeroed
__declspec(naked) void __stdcall ConstructTexCoords(void*, int) EI_ORIGINAL(0x00477160)    // B: 12 bytes, zeroed
__declspec(naked) void __stdcall ConstructFileFaces(void*, int) EI_ORIGINAL(0x00477190)    // C: 0x1C, zeroed
__declspec(naked) void __stdcall ConstructGroups(void*, int) EI_ORIGINAL(0x00454DB0)       // D: FaceGroup
__declspec(naked) void __stdcall DestructGroups(void*, int) EI_ORIGINAL(0x00476F20)
__declspec(naked) void __stdcall ConstructRotKeys(void*, int) EI_ORIGINAL(0x004771C0)      // E: 0x14, zeroed
__declspec(naked) void __stdcall ConstructPosKeys(void*, int) EI_ORIGINAL(0x004771F0)      // F: 0x18, zeroed
__declspec(naked) void __stdcall ConstructMorphHeads(void*, int) EI_ORIGINAL(0x00477220)   // G: 8, zeroed
__declspec(naked) void __stdcall ConstructNormals(void*, int) EI_ORIGINAL(0x00477530)      // H: 0x14, zeroed
__declspec(naked) void __stdcall ConstructNodeRotKeys(void*, int) EI_ORIGINAL(0x00454ED0)  // node +0xA8
__declspec(naked) void __stdcall ConstructNodePosKeys(void*, int) EI_ORIGINAL(0x00454F00)  // node +0xBC
__declspec(naked) void __stdcall ConstructNodeMorphKeys(void*, int) EI_ORIGINAL(0x00454F30) // node +0xD0
__declspec(naked) void __stdcall DestructNodeMorphKeys(void*, int) EI_ORIGINAL(0x00453700)
__declspec(naked) void __stdcall ConstructMaps(void*, int) EI_ORIGINAL(0x00477380)         // CMap (0x1C)

// Aligned blocks: count elements, 32-byte aligned, raw pointer stored before the data.
using AlignedAllocFn = void*(__fastcall*)(int count);
__declspec(naked) void* __fastcall AllocPoints(int) EI_ORIGINAL(0x00477250)       // 12-byte elements
__declspec(naked) void* __fastcall AllocMorphPoints(int) EI_ORIGINAL(0x00452C20)  // 12-byte elements
__declspec(naked) void* __fastcall AllocMeshFaces(int) EI_ORIGINAL(0x00476EF0)    // MeshFace

void FreeAligned(void* data) { gamecrt::operator_delete(static_cast<void**>(data)[-1]); }

// The aligned point array (12-byte points) the loader collects vertex positions in.
struct PointArray : ArrayBody {
    void SetAtGrow(int index, std::uint32_t x, std::uint32_t y, std::uint32_t z);   // 00452AF0 (point by value)
    void Copy(const PointArray& source);                                            // 0044F410
    void SetSize(int size, int growBy);                                             // 0044F1D0
    void Construct();                                                               // 00474C70
    void Destruct();                                                                // 0044F1B0
};
__declspec(naked) void PointArray::SetAtGrow(int, std::uint32_t, std::uint32_t, std::uint32_t) EI_ORIGINAL(0x00452AF0)
__declspec(naked) void PointArray::Copy(const PointArray&) EI_ORIGINAL(0x0044F410)
__declspec(naked) void PointArray::SetSize(int, int) EI_ORIGINAL(0x0044F1D0)
__declspec(naked) void PointArray::Construct() EI_ORIGINAL(0x00474C70)
__declspec(naked) void PointArray::Destruct() EI_ORIGINAL(0x0044F1B0)

// The aligned (u, v) array.
struct TexCoordArray : ArrayBody {
    void SetAtGrow(int index, std::uint32_t u, std::uint32_t v);   // 00476DD0
    int Add(std::uint32_t u, std::uint32_t v);                     // 0044FBB0
    void Copy(const TexCoordArray& source);                        // 00474C80
    void SetSize(int size, int growBy);                            // 00453100
    void Destruct();                                               // 0044FB90
};
__declspec(naked) void TexCoordArray::SetAtGrow(int, std::uint32_t, std::uint32_t) EI_ORIGINAL(0x00476DD0)
__declspec(naked) int TexCoordArray::Add(std::uint32_t, std::uint32_t) EI_ORIGINAL(0x0044FBB0)
__declspec(naked) void TexCoordArray::Copy(const TexCoordArray&) EI_ORIGINAL(0x00474C80)
__declspec(naked) void TexCoordArray::SetSize(int, int) EI_ORIGINAL(0x00453100)
__declspec(naked) void TexCoordArray::Destruct() EI_ORIGINAL(0x0044FB90)

// CArray<DWORD, const DWORD&> (vtable 0073C800): the file position index of every merged vertex.
struct IndexArray : VtArray {
    void SetAtGrow(int index, const std::uint32_t& value);   // 004773F0
    int Add(const std::uint32_t& value);                     // 00476640
    void Destruct();                                         // 00476780
};
__declspec(naked) void IndexArray::SetAtGrow(int, const std::uint32_t&) EI_ORIGINAL(0x004773F0)
__declspec(naked) int IndexArray::Add(const std::uint32_t&) EI_ORIGINAL(0x00476640)
__declspec(naked) void IndexArray::Destruct() EI_ORIGINAL(0x00476780)

// The mesh's group array (+0x148) and its element assignment.
struct GroupArray : VtArray {
    void SetSize(int size, int growBy);   // 00474DB0
};
__declspec(naked) void GroupArray::SetSize(int, int) EI_ORIGINAL(0x00474DB0)
__declspec(naked) void __fastcall AssignGroup(FaceGroup*, void*, const FaceGroup*) EI_ORIGINAL(0x00474C50)

} // namespace

__declspec(naked) MapAssoc* DwordMap::GetAssocAt(const std::uint32_t&, std::uint32_t&) const EI_ORIGINAL(0x00477340)
__declspec(naked) std::uint32_t& DwordMap::At(const std::uint32_t&) EI_ORIGINAL(0x00476050)
__declspec(naked) void DwordMap::Destruct() EI_ORIGINAL(0x00476140)

namespace {

// Destructors of the record's local arrays (they free the data; the elements are plain).
__declspec(naked) void __fastcall DestroyPoints(VtArray*) EI_ORIGINAL(0x00474F30)        // A
__declspec(naked) void __fastcall DestroyTexCoords(VtArray*) EI_ORIGINAL(0x00475110)     // B
__declspec(naked) void __fastcall DestroyFileFaces(VtArray*) EI_ORIGINAL(0x004752F0)     // C
__declspec(naked) void __fastcall DestroyGroups(VtArray*) EI_ORIGINAL(0x00454CE0)        // D
__declspec(naked) void __fastcall DestroyRotKeys(VtArray*) EI_ORIGINAL(0x00475680)       // E
__declspec(naked) void __fastcall DestroyPosKeys(VtArray*) EI_ORIGINAL(0x004759E0)       // F
__declspec(naked) void __fastcall DestroyMorphHeads(VtArray*) EI_ORIGINAL(0x00475D40)    // G
__declspec(naked) void __fastcall DestroyNormals(VtArray*) EI_ORIGINAL(0x00476AB0)       // H
__declspec(naked) void __fastcall DestroyMapArray(VtArray*) EI_ORIGINAL(0x004763C0)

// The skipped records' locals, which the original constructs and sizes out of line.
__declspec(naked) void __fastcall ConstructRotKeyArray(VtArray*) EI_ORIGINAL(0x00475500)
__declspec(naked) void __fastcall ConstructPosKeyArray(VtArray*) EI_ORIGINAL(0x00475860)
__declspec(naked) void __fastcall ConstructMorphHeadArray(VtArray*) EI_ORIGINAL(0x00475BC0)
__declspec(naked) void __fastcall ConstructNormalArray(VtArray*) EI_ORIGINAL(0x00476930)
__declspec(naked) void __fastcall SetSizeRotKeys(VtArray*, void*, int, int) EI_ORIGINAL(0x00475520)
__declspec(naked) void __fastcall SetSizePosKeys(VtArray*, void*, int, int) EI_ORIGINAL(0x00475880)
__declspec(naked) void __fastcall SetSizeNormals(VtArray*, void*, int, int) EI_ORIGINAL(0x00476950)
__declspec(naked) void __fastcall SetSizeMorphHeads(VtArray*, void*, int, int) EI_ORIGINAL(0x00475BE0)
__declspec(naked) void __fastcall ConstructPointBlock(ArrayBody*) EI_ORIGINAL(0x00475F10)
__declspec(naked) void __fastcall SetSizePointBlock(ArrayBody*, void*, int, int) EI_ORIGINAL(0x00475F40)
__declspec(naked) void __fastcall DestroyPointBlock(ArrayBody*) EI_ORIGINAL(0x00475F20)

// The helpers array (+0x56C).
__declspec(naked) void __fastcall SetSizeHelpers(VtArray*, void*, int, int) EI_ORIGINAL(0x00453550)

// MFC.
void __fastcall SetSizeDwordArray(VtArray* a0, void*, int a2, int a3) { UIntArraySetSizeOriginal(a0, nullptr, a2, a3); }

// Vtables of the record's local arrays.
constexpr std::uintptr_t kPointArrayVtable = 0x0073C860;      // A
constexpr std::uintptr_t kTexCoordArrayVtable = 0x0073C854;   // B
constexpr std::uintptr_t kFileFaceArrayVtable = 0x0073C848;   // C
constexpr std::uintptr_t kGroupArrayVtable = 0x0073C2BC;      // D
constexpr std::uintptr_t kRotKeyArrayVtable = 0x0073C83C;     // E
constexpr std::uintptr_t kPosKeyArrayVtable = 0x0073C830;     // F
constexpr std::uintptr_t kMorphHeadArrayVtable = 0x0073C824;  // G
constexpr std::uintptr_t kNormalArrayVtable = 0x0073C86C;     // H
constexpr std::uintptr_t kDwordMapVtable = 0x0073C818;
constexpr std::uintptr_t kMapArrayVtable = 0x0073C80C;
constexpr std::uintptr_t kIndexArrayVtable = 0x0073C800;

// --- Inlined MFC templates -----------------------------------------------------------------------

// The grow step of CArray::SetSize when the array must be reallocated.
int NewCapacity(const ArrayBody& a, int size) {
    int grow = a.grow;
    if (grow == 0) {
        grow = a.size / 8;
        if (grow < 4) grow = 4;
        else if (grow > 1024) grow = 1024;
    }
    int capacity = a.max + grow;
    return size < capacity ? capacity : size;
}

// CArray::SetSize(size, -1) as the original inlines it for arrays allocated with new.
// `destruct` is null for the instantiations whose elements need no destructor.
void SetSizeInline(ArrayBody& a, int size, int elementSize, ElementFn construct, ElementFn destruct) {
    if (size == 0) {
        if (a.data) {
            if (destruct) destruct(a.data, a.size);
            gamecrt::operator_delete(a.data);
            a.data = nullptr;
        }
        a.max = 0;
        a.size = 0;
    } else if (!a.data) {
        a.data = gamecrt::operator_new(size * elementSize);
        construct(a.data, size);
        a.max = size;
        a.size = size;
    } else if (size <= a.max) {
        if (size > a.size) construct(At(a.data, a.size * elementSize), size - a.size);
        else if (size < a.size && destruct) destruct(At(a.data, size * elementSize), a.size - size);
        a.size = size;
    } else {
        int capacity = NewCapacity(a, size);
        void* data = gamecrt::operator_new(capacity * elementSize);
        std::memcpy(data, a.data, a.size * elementSize);
        construct(At(data, a.size * elementSize), size - a.size);
        gamecrt::operator_delete(a.data);
        a.data = data;
        a.size = size;
        a.max = capacity;
    }
}

// The same for the aligned arrays (plain elements, no constructors).
void SetSizeAligned(ArrayBody& a, int size, int elementSize, AlignedAllocFn alloc) {
    if (size == 0) {
        if (a.data) {
            FreeAligned(a.data);
            a.data = nullptr;
        }
        a.max = 0;
        a.size = 0;
    } else if (!a.data) {
        a.data = alloc(size);
        a.max = size;
        a.size = size;
    } else if (size <= a.max) {
        a.size = size;
    } else {
        int capacity = NewCapacity(a, size);
        void* data = alloc(capacity);
        std::memcpy(data, a.data, a.size * elementSize);
        FreeAligned(a.data);
        a.data = data;
        a.size = size;
        a.max = capacity;
    }
}

// CMap::RemoveAll, inlined (the keys and values need no destructor).
void RemoveAll(DwordMap& map) {
    gamecrt::operator_delete(map.table);
    map.table = nullptr;
    map.count = 0;
    map.freeList = nullptr;
    mfc::PlexFreeDataChain(map.blocks);
    map.blocks = nullptr;
}

void InitArray(VtArray& a, std::uintptr_t vtable) {
    a.vtable = vtable;
    a.body = {};
}

void InitMap(DwordMap& map) {
    map.vtable = kDwordMapVtable;
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
    VtArray points;       // A (fr-0x21C)
    VtArray texCoords;    // B (fr-0x230)
    VtArray faces;        // C (fr-0x268)
    VtArray groups;       // D (fr-0x280)
    VtArray rotKeys;      // E (fr-0x1CC)
    VtArray posKeys;      // F (fr-0x1F4)
    VtArray morphHeads;   // G (fr-0x1E0)
    VtArray normals;      // H (fr-0x208)

    std::uint32_t* Point(std::uint32_t index) { return At<std::uint32_t>(points.body.data, index * 12); }
    FileFace* Face(std::uint32_t index) { return At<FileFace>(faces.body.data, index * sizeof(FileFace)); }
    FaceGroup* Group(int index) { return At<FaceGroup>(groups.body.data, index * sizeof(FaceGroup)); }
};

// The vertex merge of one mesh (fr-0x1B0 .. fr-0x12C).
struct VertexMerge {
    DwordMap seen;             // key -> 1 once the vertex exists in the current group
    DwordMap index;            // key -> merged vertex index
    VtArray maps;              // never used: one CMap per group, allocated and freed
    IndexArray sources;        // merged vertex -> file position index
    PointArray positions;
    TexCoordArray texCoords;
};

// One corner of a face: finds or adds its merged vertex (keyed by position and texture index) and
// writes the vertex index into both index triples of the mesh face.
void MergeCorner(RecordArrays& r, VertexMerge& m, MeshFace* meshFace, std::uint32_t faceIndex, int corner) {
    FileFace* face = r.Face(faceIndex);
    std::uint32_t texture = r.texCoords.body.size ? face->texture[corner] : 0;
    std::uint32_t key = face->position[corner] << 16 | texture;
    std::uint32_t hash;
    MapAssoc* assoc = m.seen.GetAssocAt(key, hash);
    if (!assoc || assoc->value == 0) {
        const std::uint32_t* point = r.Point(face->position[corner]);
        std::uint32_t x = FpuCopyBits(&point[0]);
        std::uint32_t u = 0, v = 0;
        if (r.texCoords.body.size) {
            const std::uint32_t* uv = At<std::uint32_t>(r.texCoords.body.data, face->texture[corner] * 12);
            u = uv[0];
            v = uv[1];
        }
        m.positions.SetAtGrow(m.positions.size, x, point[1], point[2]);
        // The third corner goes through the out-of-line Add instantiations (same effect).
        if (corner < 2) {
            m.texCoords.SetAtGrow(m.texCoords.size, u, v);
            m.sources.SetAtGrow(m.sources.body.size, face->position[corner]);
        } else {
            m.texCoords.Add(u, v);
            m.sources.Add(face->position[corner]);
        }
        m.index.At(key) = static_cast<std::uint32_t>(m.positions.size - 1);
        m.seen.At(key) = 1;
    }
    meshFace->vertex[corner] = m.index.At(key);
    meshFace->vertex2[corner] = m.index.At(key);
}

class Loader {
public:
    Loader(void* file, std::uint32_t version) : file_(file), version_(version) {}

    void LoadRecord(int i, VtArray* meshes, VtArray* nodes);
    void SkipRecord();
    void LoadHelper(void* helper);

private:
    void SizeRecordArrays(RecordArrays& r, const RecordHeader& h);
    void ReadGroups(RecordArrays& r, const RecordHeader& h);
    void LoadNodeKeys(RecordArrays& r, const RecordHeader& h, void* node);
    void LoadMorphKeys(RecordArrays& r, const RecordHeader& h, void* node);
    void BuildMesh(RecordArrays& r, void* mesh, void* node);

    void* file_;
    std::uint32_t version_;
};

void Loader::SizeRecordArrays(RecordArrays& r, const RecordHeader& h) {
    SetSizeInline(r.points.body, h.pointCount, 12, &ConstructPoints, nullptr);
    SetSizeInline(r.texCoords.body, h.texCoordCount, 12, &ConstructTexCoords, nullptr);
    SetSizeInline(r.faces.body, h.faceCount, sizeof(FileFace), &ConstructFileFaces, nullptr);
    SetSizeInline(r.groups.body, h.groupCount, sizeof(FaceGroup), &ConstructGroups, &DestructGroups);
    SetSizeInline(r.rotKeys.body, h.rotKeyCount, 0x14, &ConstructRotKeys, nullptr);
    SetSizeInline(r.posKeys.body, h.posKeyCount, 0x18, &ConstructPosKeys, nullptr);
    SetSizeInline(r.morphHeads.body, h.morphKeyCount - 1, 8, &ConstructMorphHeads, nullptr);
}

// Each group: u16 name length, u32 face count, the name, the face indices.
void Loader::ReadGroups(RecordArrays& r, const RecordHeader& h) {
    for (int g = 0; g < h.groupCount; ++g) {
        GroupHeader gh;
        Read(file_, &gh, sizeof(gh));
        FaceGroup* group = r.Group(g);
        Read(file_, group->name.GetBufferSetLength(gh.nameLength), gh.nameLength);
        SetSizeDwordArray(&group->faces, nullptr, static_cast<int>(gh.faceCount), -1);
        Read(file_, group->faces.body.data, gh.faceCount * 4);
    }
}

// The node's rotation and position keys (and normals in the newer version), copied field by
// field from the file records.
void Loader::LoadNodeKeys(RecordArrays& r, const RecordHeader& h, void* node) {
    Read(file_, r.rotKeys.body.data, r.rotKeys.body.size * 0x14);
    ArrayBody& rot = At<VtArray>(node, node::kRotKeys)->body;
    SetSizeInline(rot, r.rotKeys.body.size, 0x14, &ConstructNodeRotKeys, nullptr);
    for (int k = 0; k < rot.size; ++k) {
        const void* from = At(r.rotKeys.body.data, k * 0x14);
        void* to = At(rot.data, k * 0x14);
        SetWord(to, 0, Word(from, 0));
        std::uint32_t z = FpuCopyBits(At(const_cast<void*>(from), 0x10));
        std::uint32_t y = FpuCopyBits(At(const_cast<void*>(from), 0x0C));
        std::uint32_t x = FpuCopyBits(At(const_cast<void*>(from), 0x08));
        SetDword(to, 0x08, x);
        SetDword(to, 0x0C, y);
        SetDword(to, 0x10, z);
    }

    Read(file_, r.posKeys.body.data, r.posKeys.body.size * 0x18);
    ArrayBody& pos = At<VtArray>(node, node::kPosKeys)->body;
    SetSizeInline(pos, r.posKeys.body.size, 0x18, &ConstructNodePosKeys, nullptr);
    for (int k = 0; k < pos.size; ++k) {
        const void* from = At(r.posKeys.body.data, k * 0x18);
        void* to = At(pos.data, k * 0x18);
        SetWord(to, 0, Word(from, 0));
        std::uint32_t z = FpuCopyBits(At(const_cast<void*>(from), 0x10));
        std::uint32_t y = FpuCopyBits(At(const_cast<void*>(from), 0x0C));
        std::uint32_t x = FpuCopyBits(At(const_cast<void*>(from), 0x08));
        SetDword(to, 0x0C, x);
        SetDword(to, 0x10, y);
        SetDword(to, 0x14, z);
        SetDword(to, 0x08, Dword(from, 0x14));
    }

    if (version_ != kVersionWithNormals) return;
    SetSizeInline(r.normals.body, h.posKeyCount, 0x14, &ConstructNormals, nullptr);
    ArrayBody& normals = At<VtArray>(node, node::kNormals)->body;
    SetSizeInline(normals, h.posKeyCount, 12, &ZeroPoints, nullptr);
    Read(file_, r.normals.body.data, r.normals.body.size * 0x14);
    for (int k = 0; k < normals.size; ++k) {
        const void* from = At(r.normals.body.data, k * 0x14);
        void* to = At(normals.data, k * 12);
        std::uint32_t z = FpuCopyBits(At(const_cast<void*>(from), 0x10));
        std::uint32_t y = FpuCopyBits(At(const_cast<void*>(from), 0x0C));
        std::uint32_t x = FpuCopyBits(At(const_cast<void*>(from), 0x08));
        SetDword(to, 0, x);
        SetDword(to, 4, y);
        SetDword(to, 8, z);
    }
}

// The vertex animation keys: per key two dwords (the first word is the frame) and a full set of
// positions, kept in the node as an aligned point array.
void Loader::LoadMorphKeys(RecordArrays& r, const RecordHeader& h, void* node) {
    ArrayBody& keys = At<VtArray>(node, node::kMorphKeys)->body;
    SetSizeInline(keys, r.morphHeads.body.size, 0x14, &ConstructNodeMorphKeys, &DestructNodeMorphKeys);
    for (int k = 0; k < r.morphHeads.body.size; ++k) {
        int count = h.pointCount;
        void* points = count ? AllocPoints(count) : nullptr;
        void* head = At(r.morphHeads.body.data, k * 8);
        Read(file_, head, 4);
        Read(file_, At(head, 4), 4);
        Read(file_, points, h.pointCount * 12);
        void* key = At(keys.data, k * 0x14);
        SetWord(key, 0, Word(head, 0));
        ArrayBody& target = *At<ArrayBody>(key, 4);
        SetSizeAligned(target, count, 12, &AllocMorphPoints);
        for (int p = 0; p < count; ++p) {
            std::uint32_t* to = At<std::uint32_t>(At<ArrayBody>(At(keys.data, k * 0x14), 4)->data, p * 12);
            const std::uint32_t* from = At<std::uint32_t>(points, p * 12);
            to[0] = from[0];
            to[1] = from[1];
            to[2] = from[2];
        }
        if (points) FreeAligned(points);
    }
}

// Faces, merged vertices and groups of the mesh; then the morph keys are reordered to the merged
// vertices.
void Loader::BuildMesh(RecordArrays& r, void* mesh, void* node) {
    ArrayBody& meshFaces = *At<ArrayBody>(mesh, mesh::kFaces);
    SetSizeAligned(meshFaces, r.faces.body.size, sizeof(MeshFace), &AllocMeshFaces);
    for (int f = 0; f < r.faces.body.size; ++f) {
        const std::uint32_t* from = reinterpret_cast<const std::uint32_t*>(r.Face(f));
        std::uint32_t* to = At<std::uint32_t>(meshFaces.data, f * sizeof(MeshFace));
        for (int d = 0; d < 6; ++d) to[d] = from[d];
        to[6] = from[0];
        to[7] = from[1];
        to[8] = from[2];
    }

    VertexMerge m;
    InitMap(m.seen);
    InitMap(m.index);
    InitArray(m.maps, kMapArrayVtable);
    InitArray(m.sources, kIndexArrayVtable);
    static_cast<ArrayBody&>(m.positions) = {};
    static_cast<ArrayBody&>(m.texCoords) = {};

    // One map per group, never used (the original allocates them all the same).
    int groupCount = r.groups.body.size;
    if (groupCount) {
        m.maps.body.data = gamecrt::operator_new(groupCount * sizeof(DwordMap));
        ConstructMaps(m.maps.body.data, groupCount);
    }
    m.maps.body.max = groupCount;
    m.maps.body.size = groupCount;

    for (int g = 0; g < r.groups.body.size; ++g) {
        RemoveAll(m.seen);
        FaceGroup* group = r.Group(g);
        for (int j = 0; j < group->faces.body.size; ++j) {
            std::uint32_t faceIndex = static_cast<const std::uint32_t*>(r.Group(g)->faces.body.data)[j];
            for (int corner = 0; corner < 3; ++corner) {
                MeshFace* meshFace = At<MeshFace>(At<ArrayBody>(mesh, mesh::kFaces)->data, faceIndex * sizeof(MeshFace));
                MergeCorner(r, m, meshFace, faceIndex, corner);
            }
        }
    }

    At<PointArray>(mesh, mesh::kPositions)->Copy(m.positions);
    At<TexCoordArray>(mesh, mesh::kTexCoords)->Copy(m.texCoords);
    m.positions.SetSize(0, -1);
    m.texCoords.SetSize(0, -1);
    GroupArray* groups = At<GroupArray>(mesh, mesh::kGroups);
    groups->SetSize(r.groups.body.size, -1);
    for (int g = 0; g < r.groups.body.size; ++g) {
        AssignGroup(At<FaceGroup>(groups->body.data, g * sizeof(FaceGroup)), nullptr, r.Group(g));
    }

    // Every morph key's points, reordered to the merged vertices.
    for (int k = 0; k < At<VtArray>(node, node::kMorphKeys)->body.size; ++k) {
        PointArray reordered;
        reordered.Construct();
        reordered.SetSize(m.sources.body.size, -1);
        for (int v = 0; v < m.sources.body.size; ++v) {
            void* key = At(At<VtArray>(node, node::kMorphKeys)->body.data, k * 0x14);
            std::uint32_t source = static_cast<const std::uint32_t*>(m.sources.body.data)[v];
            const std::uint32_t* from = At<std::uint32_t>(At<ArrayBody>(key, 4)->data, source * 12);
            std::uint32_t* to = At<std::uint32_t>(reordered.data, v * 12);
            to[0] = from[0];
            to[1] = from[1];
            to[2] = from[2];
        }
        void* key = At(At<VtArray>(node, node::kMorphKeys)->body.data, k * 0x14);
        At<PointArray>(key, 4)->Copy(reordered);
        reordered.Destruct();
    }

    m.texCoords.Destruct();
    m.positions.Destruct();
    m.sources.Destruct();
    DestroyMapArray(&m.maps);
    m.index.Destruct();
    m.seen.Destruct();
}

// Record i: node i and mesh i.
void Loader::LoadRecord(int i, VtArray* meshes, VtArray* nodes) {
    mfc::CString name;
    mfc::CString parent;
    RecordArrays r;
    InitArray(r.normals, kNormalArrayVtable);
    InitArray(r.points, kPointArrayVtable);
    InitArray(r.texCoords, kTexCoordArrayVtable);
    InitArray(r.faces, kFileFaceArrayVtable);
    InitArray(r.groups, kGroupArrayVtable);
    InitArray(r.rotKeys, kRotKeyArrayVtable);
    InitArray(r.posKeys, kPosKeyArrayVtable);
    InitArray(r.morphHeads, kMorphHeadArrayVtable);

    RecordHeader h;
    Read(file_, &h, sizeof(h));
    void* node = At(nodes->body.data, i * node::kSize);
    void* mesh = At(meshes->body.data, i * mesh::kSize);
    std::uint32_t x = FpuCopyBits(&h.offset[0]);
    std::uint32_t y = FpuCopyBits(&h.offset[1]);
    std::uint32_t z = FpuCopyBits(&h.offset[2]);
    SetDword(node, node::kOffset, x);
    SetDword(node, node::kOffset + 4, y);
    SetDword(node, node::kOffset + 8, z);

    SizeRecordArrays(r, h);
    Read(file_, name.GetBufferSetLength(h.nameLength), h.nameLength);
    Read(file_, parent.GetBufferSetLength(h.parentLength), h.parentLength);
    *At<mfc::CString>(node, node::kName) = name;
    *At<mfc::CString>(node, node::kParent) = parent;
    *At<mfc::CString>(mesh, mesh::kName) = name;

    Read(file_, r.points.body.data, h.pointCount * 12);
    Read(file_, r.texCoords.body.data, h.texCoordCount * 12);
    Read(file_, r.faces.body.data, r.faces.body.size * sizeof(FileFace));
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
    VtArray rotKeys, posKeys, morphHeads, normals;
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
    Read(file_, rotKeys.body.data, rotKeys.body.size * 0x14);
    Read(file_, posKeys.body.data, posKeys.body.size * 0x18);
    if (version_ == kVersionWithNormals) Read(file_, normals.body.data, normals.body.size * 0x14);
    for (int k = 0; k < morphHeads.body.size; ++k) {
        ArrayBody points;
        ConstructPointBlock(&points);
        SetSizePointBlock(&points, nullptr, h.pointCount, -1);
        Read(file_, At(morphHeads.body.data, k * 8), 4);
        Read(file_, At(morphHeads.body.data, k * 8 + 4), 4);
        Read(file_, points.data, h.pointCount * 12);
        DestroyPointBlock(&points);
    }
    DestroyNormals(&normals);
    DestroyMorphHeads(&morphHeads);
    DestroyPosKeys(&posKeys);
    DestroyRotKeys(&rotKeys);
}

// A helper (0x3E-byte header, then three names).
void Loader::LoadHelper(void* helper) {
#pragma pack(push, 1)
    struct HelperHeader {
        std::uint32_t vectorA[3];    // +00
        std::uint32_t vectorB[3];    // +0C
        std::uint32_t vectorC[3];    // +18
        std::uint32_t value60;       // +24
        std::uint32_t unknown_28[2]; // +28
        std::uint32_t value2C;       // +30
        std::uint32_t unknown_34;    // +34
        std::uint16_t nameLength;    // +38
        std::uint16_t name2Length;   // +3A
        std::uint16_t name3Length;   // +3C
    };
#pragma pack(pop)
    static_assert(sizeof(HelperHeader) == 0x3E);

    mfc::CString text;
    HelperHeader h;
    Read(file_, &h, sizeof(h));
    std::memcpy(At(helper, helper::kVectorA), h.vectorA, 12);
    std::memcpy(At(helper, helper::kVectorB), h.vectorB, 12);
    std::memcpy(At(helper, helper::kVectorC), h.vectorC, 12);
    SetDword(helper, helper::kValue2C, h.value2C);
    SetDword(helper, helper::kValue60, h.value60);
    Read(file_, text.GetBufferSetLength(h.nameLength), h.nameLength);
    *At<mfc::CString>(helper, helper::kName) = static_cast<const char*>(text);
    Read(file_, text.GetBufferSetLength(h.name2Length), h.name2Length);
    *At<mfc::CString>(helper, helper::kName2) = static_cast<const char*>(text);
    Read(file_, text.GetBufferSetLength(h.name3Length), h.name3Length);
    *At<mfc::CString>(helper, helper::kName3) = static_cast<const char*>(text);
}

} // namespace

mfc::BOOL SceneInfo::Load(void* file, VtArray* meshes, VtArray* helpers, VtArray* nodes) {
    struct FileHeader {
        std::uint32_t version;
        int recordCount;
        int skippedCount;
        int helperCount;
        std::uint16_t flags;
        std::uint16_t unknown_12;
    } header;
    static_assert(sizeof(FileHeader) == 0x14);
    Read(file, &header, sizeof(header));
    flags_ = header.flags;

    SetSizeInline(meshes->body, header.recordCount, mesh::kSize, &ConstructMeshes, &DestructMeshes);
    SetSizeInline(nodes->body, header.recordCount, node::kSize, &ConstructNodes, &DestructNodes);

    Loader loader(file, header.version);
    for (int i = 0; i < meshes->body.size; ++i) loader.LoadRecord(i, meshes, nodes);
    for (int i = 0; i < header.skippedCount; ++i) loader.SkipRecord();

    SetSizeHelpers(helpers, nullptr, header.helperCount, -1);
    for (int i = 0; i < helpers->body.size; ++i) loader.LoadHelper(At(helpers->body.data, i * helper::kSize));
    return 1;
}
EI_REPLACE_METHOD(0x00471B10, SceneInfo::Load);

// --- 00446C10: building a CFakeUnit's figure from its scene ---------------------------------------

namespace {

// CString members used only here, called on existing strings through this view (no extra data).
struct Text : mfc::CString {
    int Find(const char* sub) const;      // 006FDBD9
};
__declspec(naked) int Text::Find(const char*) const EI_ORIGINAL(0x006FDBD9)
const Text& Ops(const mfc::CString& s) { return static_cast<const Text&>(s); }
int Length(const mfc::CString& s) { return reinterpret_cast<const int*>(static_cast<const char*>(s))[-2]; }

// String constants, passed by their original address (mods may patch the data).
const char* GameText(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }
constexpr std::uintptr_t kCantOpenText = 0x00784394;     // "Can't open file %s.\n"
constexpr std::uintptr_t kManyTopsText = 0x00784344;     // "Scene \"%s\" have more then one top-level object!..."
constexpr std::uintptr_t kNewLineText = 0x00784340;      // "\n"
constexpr std::uintptr_t kSceneExtText = 0x00784338;     // ".amf"
constexpr std::uintptr_t kScenesDirText = 0x00784330;    // "scenes\\"
constexpr std::uintptr_t kIniDirText = 0x00784328;       // "Ini's\\"
constexpr std::uintptr_t kRegExtText = 0x00784320;       // ".reg"
constexpr std::uintptr_t kModelNameText = 0x00784304;    // "Model File Name = \"%s\".\n"
constexpr std::uintptr_t kBmpExtText = 0x007842FC;       // ".bmp"

// The fake unit's fields used here.
namespace unit {
constexpr std::size_t kOffset1C = 0x1C;       // 3 floats, set to (0, 0, 500)
constexpr std::size_t kTextureOwner = 0x94;   // the object 0051B2F0 / 0051B470 work on
constexpr std::size_t kSkinTarget = 0xD0;
constexpr std::size_t kSkin = 0x4E0;
constexpr std::size_t kFigures = 0x530;       // CArray of figures (0x220 bytes, constructed by 00453240)
constexpr std::size_t kMeshes = 0x544;        // CArray of meshes (scene_loader.h)
constexpr std::size_t kBoneLinks = 0x558;     // CArray of 0x14-byte CArrays of 0x28-byte records
constexpr std::size_t kHelpers = 0x56C;       // CArray of helpers
} // namespace unit

// The figure (element of the unit's +0x530 array).
namespace figure {
constexpr std::size_t kSize = 0x220;
constexpr std::size_t kName = 0x120;          // CString
constexpr std::size_t kIndices = 0x138;       // aligned dword array
constexpr std::size_t kIndices2 = 0x148;      // aligned dword array
constexpr std::size_t kValue158 = 0x158;      // 2 dwords from the mesh's +0x184
constexpr std::size_t kVectors160 = 0x160;    // 3 x 3 dwords + 1 from the mesh's +0x15C
constexpr std::size_t kMatrices = 0x1C0;      // aligned array of 64-byte matrices
constexpr std::size_t kMatrices2 = 0x1D0;
} // namespace figure

constexpr std::size_t kMeshMatrices = 0xA8;   // mesh: aligned arrays of 64-byte matrices
constexpr std::size_t kMeshMatrices2 = 0xB8;
constexpr std::size_t kMeshVectors15C = 0x15C;
constexpr std::size_t kMeshValue184 = 0x184;
constexpr std::size_t kNodeBoneLinks = 0xF8;  // node: CArray of 0x28-byte records
constexpr std::size_t kTextureName = 0x1C;    // CString of the texture objects

constexpr std::uintptr_t kNodeArrayVtable = 0x0073C15C;
constexpr std::uintptr_t kTextureNameArrayVtable = 0x0073C144;   // {id, CString}
constexpr std::uintptr_t kTextureLinkArrayVtable = 0x0073C150;   // {id, value}

constexpr int kGetNameSlot = 7;              // CEngineObject::GetName
constexpr int kTextureManagerSlot = 33;      // manager context slot 33 (+0x84)

// Not rewritten.
__declspec(naked) void __stdcall ConstructFigures(void*, int) EI_ORIGINAL(0x00453240)
__declspec(naked) void __stdcall DestructFigures(void*, int) EI_ORIGINAL(0x00453210)
__declspec(naked) void __stdcall ConstructBoneLinks(void*, int) EI_ORIGINAL(0x004534F0)
__declspec(naked) void __stdcall DestructBoneLinks(void*, int) EI_ORIGINAL(0x004534C0)
__declspec(naked) void* __fastcall AllocDwords(int) EI_ORIGINAL(0x00406B50)
__declspec(naked) void __fastcall SetSizeBoneLinks(VtArray*, void*, int, int) EI_ORIGINAL(0x00450370)       // unit +0x558
void __fastcall SetSizeFigures(VtArray* a0, void*, int a2, int a3) { FigureArraySetSizeOriginal((void*)a0, nullptr, a2, a3); }
__declspec(naked) void __fastcall SetSizeBoneLinkRecords(VtArray*, void*, int, int) EI_ORIGINAL(0x00450F20) // 0x28-byte records
__declspec(naked) void __fastcall SetSizeMatrices(ArrayBody*, void*, int, int) EI_ORIGINAL(0x0044F960)
__declspec(naked) void __fastcall DestructNode(void*) EI_ORIGINAL(0x004543E0)
__declspec(naked) void __fastcall ConstructRegistry(void*) EI_ORIGINAL(0x00694520)
__declspec(naked) void __fastcall DestroyRegistry(void*) EI_ORIGINAL(0x00694540)
// Reads the figure's .reg file: the texture names ({id, name}) and links ({id, value}).
__declspec(naked) void __fastcall LoadFigureRegistry(void*, const mfc::CString*, VtArray*, VtArray*) EI_ORIGINAL(0x0044B980)
__declspec(naked) void __fastcall LinkNodes(void*, VtArray*) EI_ORIGINAL(0x0044C4A0)
__declspec(naked) mfc::BOOL __fastcall BuildSkeleton(VtArray*, VtArray*, int, void*) EI_ORIGINAL(0x00448A40)
__declspec(naked) void __fastcall PlaceMeshes(void*) EI_ORIGINAL(0x0044E3B0)
__declspec(naked) void __fastcall PrepareMeshes(VtArray*) EI_ORIGINAL(0x0044DCF0)
__declspec(naked) void __fastcall PrepareMeshes2(VtArray*) EI_ORIGINAL(0x0044CFF0)
__declspec(naked) void __fastcall PrepareMesh(void*) EI_ORIGINAL(0x0044E8A0)
__declspec(naked) void* __fastcall FindTexture(void*, void*, std::uint32_t) EI_ORIGINAL(0x0051B2F0)
__declspec(naked) void* __fastcall AddTextureLink(void*, void*, std::uint32_t, std::uint32_t) EI_ORIGINAL(0x0051B470)
int __fastcall RegisterTexture(void* a0, void*, void* a2, std::uint32_t a3) { return MaterialRegisterOriginal(a0, nullptr, a2, a3); }
__declspec(naked) void __fastcall FinishFigure(void*) EI_ORIGINAL(0x005A6D40)
__declspec(naked) void __fastcall AddFigure(void*, void*, void*) EI_ORIGINAL(0x005164B0)
__declspec(naked) int __fastcall BindSkin(void*, void*, void*) EI_ORIGINAL(0x00508BF0)
__declspec(naked) void __fastcall ResetSkin(void*) EI_ORIGINAL(0x00508990)
// SceneInfo::Load through its original address, so the verify build's wrapper sees the call.
__declspec(naked) mfc::BOOL __fastcall LoadScene(SceneInfo*, void*, void*, VtArray*, VtArray*, VtArray*) EI_ORIGINAL(0x00471B10)

// The loader's locals that need cleaning up, as small owners. Declared in the original's
// construction order, so C++ destroys them in its order.
struct NodeArray : VtArray {
    NodeArray() : VtArray{kNodeArrayVtable, {}} {}
    ~NodeArray() {
        if (!body.data) return;
        for (int i = 0; i < body.size; ++i) DestructNode(At(body.data, i * node::kSize));
        gamecrt::operator_delete(body.data);
    }
};
struct Registry {
    std::uint8_t bytes[0x18];   // CFileRegistry, never used
    Registry() { ConstructRegistry(bytes); }
    ~Registry() { DestroyRegistry(bytes); }
};
struct TextureLinks : VtArray {
    TextureLinks() : VtArray{kTextureLinkArrayVtable, {}} {}
    ~TextureLinks() {
        if (body.data) gamecrt::operator_delete(body.data);
    }
};
struct TextureNames : VtArray {
    TextureNames() : VtArray{kTextureNameArrayVtable, {}} {}
    ~TextureNames() {
        if (!body.data) return;
        for (int i = 0; i < body.size; ++i) DestructTextureName(At(body.data, i * 8));
        gamecrt::operator_delete(body.data);
    }
    std::uint32_t Id(int i) const { return Dword(body.data, i * 8); }
    mfc::CString& Name(int i) { return *At<mfc::CString>(body.data, i * 8 + 4); }
};

struct SceneLoad {
    mfc::CString path;
    NodeArray nodes;
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

char* Element(const VtArray* a, int i, std::size_t size) { return At<char>(a->body.data, i * size); }

} // namespace

// 00446C10. Loads the scene `fileName` (an .amf) into the fake unit: meshes, helpers and nodes,
// then the figure's .reg file (`registryName`, or "Ini's\\<name>.reg" for a scene under
// "scenes\\"), the figures, skeleton and textures. Returns 0 if the file cannot be opened or the
// scene has more than one top-level node (a fatal error). The caller also passes the unit in ECX;
// it is not read.
mfc::BOOL __stdcall LoadFakeUnitScene(const char* fileName, const char* registryName, void* fakeUnit) {
    char* u = static_cast<char*>(fakeUnit);
    VtArray* meshes = reinterpret_cast<VtArray*>(u + unit::kMeshes);
    VtArray* helpers = reinterpret_cast<VtArray*>(u + unit::kHelpers);
    VtArray* figures = reinterpret_cast<VtArray*>(u + unit::kFigures);
    VtArray* boneLinks = reinterpret_cast<VtArray*>(u + unit::kBoneLinks);

    SceneLoad s(fileName);
    SetSizeBoneLinks(boneLinks, nullptr, 0, -1);
    SetSizeHelpers(helpers, nullptr, 0, -1);
    SetSizeFigures(figures, nullptr, 0, -1);

    if (!s.file.Open(fileName, 0x8000, nullptr)) {
        mfc::CString message;
        message.Format(GameText(kCantOpenText), fileName);
        AfxMessageBox(message, 0x10, 0);
        return 0;
    }
    LoadScene(&s.info, nullptr, &s.file, meshes, helpers, &s.nodes);
    s.file.Close();

    // Exactly one node may have no parent.
    int tops = 0;
    for (int i = 0; i < s.nodes.body.size; ++i) {
        char* n = Element(&s.nodes, i, node::kSize);
        if (Length(*At<mfc::CString>(n, node::kParent)) == 0) {
            ++tops;
            s.roots.SetAtGrow(s.roots.size_, *At<mfc::CString>(n, node::kName));
        }
    }
    if (tops > 1) {
        mfc::CString message;
        message.Format(GameText(kManyTopsText), fileName);
        for (int i = 0; i < s.roots.size_; ++i) message += s.roots[i] + GameText(kNewLineText);
        game::FatalError(message);
        return 0;
    }

    // The figure's registry file.
    int ext = Ops(s.path).Find(GameText(kSceneExtText));
    if (ext != -1) {
        if (!registryName) {
            if (Ops(s.path).Find(GameText(kScenesDirText)) == 0) {
                s.path = GameText(kIniDirText) + Ops(s.path).Mid(Length(s.path) - 7);
                --ext;
            }
            s.path = s.path.Left(ext) + GameText(kRegExtText);
        } else {
            s.path = mfc::CString(registryName);
        }
        LoadFigureRegistry(fakeUnit, &s.path, &s.textureLinks, &s.textureNames);
        LinkNodes(fakeUnit, &s.nodes);
    }

    // One figure per mesh.
    SetSizeInline(figures->body, meshes->body.size, figure::kSize, &ConstructFigures, &DestructFigures);
    for (int i = 0; i < meshes->body.size; ++i) {
        char* m = Element(meshes, i, mesh::kSize);
        *At<mfc::CString>(Element(figures, i, figure::kSize), figure::kName) = ei::VCall<const char*>(m, kGetNameSlot);
        SetDword(Element(figures, i, figure::kSize), figure::kValue158, Dword(Element(meshes, i, mesh::kSize), kMeshValue184));
        SetDword(Element(figures, i, figure::kSize), figure::kValue158 + 4, Dword(Element(meshes, i, mesh::kSize), kMeshValue184 + 4));
    }

    if (!BuildSkeleton(meshes, &s.nodes, static_cast<int>(s.infoDword - 1), u + unit::kSkin))
        game::FatalError(GameText(kModelNameText), fileName);
    PlaceMeshes(fakeUnit);

    // Each figure takes the bone links of the nodes of the same name.
    SetSizeInline(boneLinks->body, figures->body.size, 0x14, &ConstructBoneLinks, &DestructBoneLinks);
    for (int f = 0; f < figures->body.size; ++f) {
        for (int n = 0; n < s.nodes.body.size; ++n) {
            const char* name = ei::VCall<const char*>(Element(figures, f, figure::kSize), kGetNameSlot);
            char* nd = Element(&s.nodes, n, node::kSize);
            if (gamecrt::mbscmp(*At<const unsigned char*>(nd, node::kName), reinterpret_cast<const unsigned char*>(name)) != 0)
                continue;
            VtArray* links = reinterpret_cast<VtArray*>(Element(boneLinks, f, 0x14));
            const VtArray* from = At<VtArray>(nd, kNodeBoneLinks);
            SetSizeBoneLinkRecords(links, nullptr, from->body.size, -1);
            std::memcpy(links->body.data, from->body.data, from->body.size * 0x28);
        }
    }

    PrepareMeshes(meshes);
    PrepareMeshes2(meshes);
    for (int i = 0; i < meshes->body.size; ++i) PrepareMesh(Element(meshes, i, mesh::kSize));

    // The figures take the meshes' matrices; their index arrays get four entries per matrix.
    for (int i = 0; i < meshes->body.size; ++i) {
        for (int pair = 0; pair < 2; ++pair) {
            const ArrayBody* from = At<ArrayBody>(Element(meshes, i, mesh::kSize), pair ? kMeshMatrices2 : kMeshMatrices);
            ArrayBody* to = At<ArrayBody>(Element(figures, i, figure::kSize), pair ? figure::kMatrices2 : figure::kMatrices);
            SetSizeMatrices(to, nullptr, from->size, -1);
            std::memcpy(to->data, from->data, static_cast<std::size_t>(from->size) << 6);
        }
        const std::size_t indexArrays[2] = {figure::kIndices, figure::kIndices2};
        for (std::size_t indices : indexArrays) {
            char* f = Element(figures, i, figure::kSize);
            int count = At<ArrayBody>(f, figure::kMatrices2)->size * 4;
            SetSizeAligned(*At<ArrayBody>(f, indices), count, 4, &AllocDwords);
        }
        for (std::size_t v = 0; v < 3; ++v)
            std::memcpy(Element(figures, i, figure::kSize) + figure::kVectors160 + v * 12,
                        Element(meshes, i, mesh::kSize) + kMeshVectors15C + v * 12, 12);
        SetDword(Element(figures, i, figure::kSize), figure::kVectors160 + 0x24,
                 Dword(Element(meshes, i, mesh::kSize), kMeshVectors15C + 0x24));
    }

    // The textures named in the registry file (without ".bmp") and their links.
    for (int t = 0; t < s.textureNames.body.size; ++t) {
        void* owner = *At<void*>(u, unit::kTextureOwner);
        void* texture = FindTexture(owner, nullptr, s.textureNames.Id(t));
        bool bmp = gamecrt::mbsicmp(reinterpret_cast<const unsigned char*>(static_cast<const char*>(Ops(s.textureNames.Name(t)).Mid(4))),
                                    reinterpret_cast<const unsigned char*>(GameText(kBmpExtText))) == 0;
        if (bmp) {
            const mfc::CString& name = s.textureNames.Name(t);
            *At<mfc::CString>(texture, kTextureName) = static_cast<const char*>(name.Left(Length(name) - 4));
        } else {
            *At<mfc::CString>(texture, kTextureName) = static_cast<const char*>(s.textureNames.Name(t));
        }
        for (int l = 0; l < s.textureLinks.body.size; ++l) {
            void* manager = ei::VCall<void*>(ManagerContextOf(fakeUnit), kTextureManagerSlot);
            std::uint32_t id = Dword(s.textureLinks.body.data, l * 8);
            if (id != s.textureNames.Id(t)) continue;
            void* linked = AddTextureLink(owner, nullptr, id, Dword(s.textureLinks.body.data, l * 8 + 4));
            *At<mfc::CString>(linked, kTextureName) = static_cast<const char*>(s.textureNames.Name(t));
            RegisterTexture(manager, nullptr, linked, Dword(s.textureLinks.body.data, l * 8 + 4));
        }
    }

    for (int f = 0; f < figures->body.size; ++f) {
        FinishFigure(Element(figures, f, figure::kSize));
        AddFigure(fakeUnit, nullptr, Element(figures, f, figure::kSize));
    }
    BindSkin(u + unit::kSkin, nullptr, u + unit::kSkinTarget);
    ResetSkin(u + unit::kSkin);
    const std::uint32_t offset[3] = {0, 0, 0x44FA0000};   // (0, 0, 500)
    std::memcpy(u + unit::kOffset1C, offset, sizeof(offset));
    return 1;
}
EI_REPLACE(0x00446C10, LoadFakeUnitScene);

} // namespace scene
