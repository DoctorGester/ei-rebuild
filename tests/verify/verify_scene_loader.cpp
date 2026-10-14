// Differential check for the CFakeUnit scene loader (scene_loader.cpp, 00471B10).
//
// The loader reads an open CFile, so it cannot simply run twice. Ours runs first on the real
// arrays, reading through a proxy file (only slot 13, Read, is used) that forwards to the real file
// and records every read (size, bytes). The original then runs on fresh empty copies of the three
// arrays, reading through a proxy that replays the recording: it must ask for the same sizes in the
// same order. The arrays are then compared in depth: every byte of every element except the heap
// pointers, whose contents are compared instead (names, vertex, face, group and key arrays).
// The copies are freed with the arrays' own element destructors.
//
// The comparison needs the arrays empty on entry (the caller, 00446C10, empties the helpers and
// gives a new node array; the mesh array is empty the first time a figure loads). Otherwise ours
// runs alone and only the array sizes are checked.
#include "verify.h"

#include "../../src/game/scene_loader.h"

#include "../../src/lib/crt.h"
#include "ei/vcall.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>

#pragma warning(disable : 4740)

namespace {

using namespace scene;

// --- Recorded file -------------------------------------------------------------------------------

struct ReadOp {
    std::uint32_t size;
    std::uint32_t result;
    std::size_t dataAt;
    std::uint32_t dataSize;
};

struct ProxyFile {
    void* const* vtable;
    void* real;                     // recording: the real file; replaying: null
    std::vector<ReadOp>* ops;
    std::vector<std::uint8_t>* data;
    std::size_t next;
    bool mismatch;
};

std::uint32_t __fastcall ProxyRead(ProxyFile* f, void*, void* p, std::uint32_t n) {
    if (f->real) {
        std::uint32_t r = ei::VCall<std::uint32_t>(f->real, 13, p, n);
        std::uint32_t got = r <= n ? r : n;
        f->ops->push_back({n, r, f->data->size(), got});
        f->data->insert(f->data->end(), static_cast<std::uint8_t*>(p), static_cast<std::uint8_t*>(p) + got);
        return r;
    }
    if (f->next >= f->ops->size() || (*f->ops)[f->next].size != n) {
        f->mismatch = true;
        return 0;
    }
    const ReadOp& op = (*f->ops)[f->next++];
    if (op.dataSize) std::memcpy(p, f->data->data() + op.dataAt, op.dataSize);
    return op.result;
}

// Any other slot is a mismatch (the loader only reads).
std::uint32_t __fastcall ProxyOther(ProxyFile* f) {
    f->mismatch = true;
    return 0;
}

void* const* ProxyVtable() {
    static void* vtable[32];
    for (void*& slot : vtable) slot = reinterpret_cast<void*>(&ProxyOther);
    vtable[13] = reinterpret_cast<void*>(&ProxyRead);
    return vtable;
}

// --- Freeing the copies ------------------------------------------------------------------------------

__declspec(naked) void __stdcall DestructMeshes(void*, int) EI_ORIGINAL(0x00476F50)
__declspec(naked) void __stdcall DestructNodes(void*, int) EI_ORIGINAL(0x00477100)
__declspec(naked) void __fastcall SetSizeHelpers(VtArray*, void*, int, int) EI_ORIGINAL(0x00453550)

void FreeArray(VtArray& a, void(__stdcall* destruct)(void*, int)) {
    if (!a.data) return;
    destruct(a.data, a.size);
    gamecrt::operator_delete(a.data);
    a.data = nullptr;
    a.size = a.maxSize = a.growBy = 0;
}

// --- Deep comparison -----------------------------------------------------------------------------------

struct Diff {
    char where[160] = "";
    bool failed = false;
    void Fail(const char* format, ...) {
        if (failed) return;
        failed = true;
        va_list ap;
        va_start(ap, format);
        std::vsnprintf(where, sizeof(where), format, ap);
        va_end(ap);
    }
};

template <typename T = std::uint8_t>
const T* At(const void* base, std::size_t offset) { return reinterpret_cast<const T*>(static_cast<const char*>(base) + offset); }

// Bytes [0, size) except the dwords at `skip`.
void CompareBytes(Diff& d, const char* what, int index, const void* a, const void* b, std::size_t size,
                  std::initializer_list<std::size_t> skip) {
    for (std::size_t i = 0; i < size; ++i) {
        bool skipped = false;
        for (std::size_t s : skip) skipped |= i >= s && i < s + 4;
        if (!skipped && At(a, i)[0] != At(b, i)[0]) {
            d.Fail("%s %d +0x%X: %02X vs %02X", what, index, unsigned(i), At(a, i)[0], At(b, i)[0]);
            return;
        }
    }
}

void CompareString(Diff& d, const char* what, int index, const void* a, const void* b) {
    const char* x = *At<const char*>(a, 0);
    const char* y = *At<const char*>(b, 0);
    if (std::strcmp(x, y) != 0) d.Fail("%s %d: \"%.40s\" vs \"%.40s\"", what, index, x, y);
}

// The layout of an array without its vtable (the aligned arrays; an MFC array's tail at +4).
struct ArrayBody {
    void* data;
    int size;
    int max;
    int grow;
};

// An array body: size, capacity, grow step, then the first `compared` bytes of each element.
void CompareBody(Diff& d, const char* what, int index, const ArrayBody& a, const ArrayBody& b, std::size_t elementSize,
                 std::size_t compared) {
    if (a.size != b.size || a.max != b.max || a.grow != b.grow) {
        d.Fail("%s %d: size %d/%d max %d/%d grow %d/%d", what, index, a.size, b.size, a.max, b.max, a.grow, b.grow);
        return;
    }
    if ((a.data == nullptr) != (b.data == nullptr)) {
        d.Fail("%s %d: data %p vs %p", what, index, a.data, b.data);
        return;
    }
    for (int i = 0; i < a.size; ++i) {
        if (std::memcmp(At(a.data, i * elementSize), At(b.data, i * elementSize), compared) != 0) {
            d.Fail("%s %d: element %d", what, index, i);
            return;
        }
    }
}

const ArrayBody& BodyAt(const void* object, std::size_t offset) { return *At<ArrayBody>(object, offset); }

void CompareMesh(Diff& d, int i, const void* a, const void* b) {
    CompareBytes(d, "mesh", i, a, b, mesh::kSize, {mesh::kName, mesh::kPositions, mesh::kTexCoords, mesh::kFaces, mesh::kGroups + 4});
    CompareString(d, "mesh name", i, At(a, mesh::kName), At(b, mesh::kName));
    CompareBody(d, "mesh positions", i, BodyAt(a, mesh::kPositions), BodyAt(b, mesh::kPositions), 12, 12);
    CompareBody(d, "mesh texcoords", i, BodyAt(a, mesh::kTexCoords), BodyAt(b, mesh::kTexCoords), 8, 8);
    // +0x24..+0x33 of a face are never written (the aligned block is not initialised).
    CompareBody(d, "mesh faces", i, BodyAt(a, mesh::kFaces), BodyAt(b, mesh::kFaces), sizeof(MeshFace), 0x24);
    const ArrayBody& ga = BodyAt(a, mesh::kGroups + 4);
    const ArrayBody& gb = BodyAt(b, mesh::kGroups + 4);
    CompareBody(d, "mesh groups", i, ga, gb, sizeof(FaceGroup), 0);
    if (d.failed) return;
    for (int g = 0; g < ga.size; ++g) {
        const void* x = At(ga.data, g * sizeof(FaceGroup));
        const void* y = At(gb.data, g * sizeof(FaceGroup));
        CompareBytes(d, "mesh group", g, x, y, sizeof(FaceGroup), {0, 8});
        CompareString(d, "mesh group name", g, x, y);
        CompareBody(d, "mesh group faces", g, BodyAt(x, 8), BodyAt(y, 8), 4, 4);
    }
}

void CompareNode(Diff& d, int i, const void* a, const void* b) {
    CompareBytes(d, "node", i, a, b, node::kSize,
                 {node::kName, node::kParent, node::kRotKeys + 4, node::kPosKeys + 4, node::kMorphKeys + 4, node::kNormals + 4});
    CompareString(d, "node name", i, At(a, node::kName), At(b, node::kName));
    CompareString(d, "node parent", i, At(a, node::kParent), At(b, node::kParent));
    CompareBody(d, "node rot keys", i, BodyAt(a, node::kRotKeys + 4), BodyAt(b, node::kRotKeys + 4), 0x14, 0x14);
    CompareBody(d, "node pos keys", i, BodyAt(a, node::kPosKeys + 4), BodyAt(b, node::kPosKeys + 4), 0x18, 0x18);
    CompareBody(d, "node normals", i, BodyAt(a, node::kNormals + 4), BodyAt(b, node::kNormals + 4), 12, 12);
    const ArrayBody& ka = BodyAt(a, node::kMorphKeys + 4);
    const ArrayBody& kb = BodyAt(b, node::kMorphKeys + 4);
    CompareBody(d, "node morph keys", i, ka, kb, 0x14, 0);
    if (d.failed) return;
    for (int k = 0; k < ka.size; ++k) {
        const void* x = At(ka.data, k * 0x14);
        const void* y = At(kb.data, k * 0x14);
        CompareBytes(d, "node morph key", k, x, y, 0x14, {4});
        CompareBody(d, "node morph points", k, BodyAt(x, 4), BodyAt(y, 4), 12, 12);
    }
}

// +0x64..+0x8B of a helper is a container its constructor (0068DBC0) builds on the heap; the loader
// does not touch it, so it is left out.
void CompareHelper(Diff& d, int i, const void* a, const void* b) {
    CompareBytes(d, "helper", i, a, b, 0x64, {offsetof(CMaterial, name)});
    CompareBytes(d, "helper", i, At(a, 0x8C), At(b, 0x8C), sizeof(CMaterial) - 0x8C, {offsetof(CMaterial, name2) - 0x8C, offsetof(CMaterial, name3) - 0x8C});
    CompareString(d, "helper name", i, At(a, offsetof(CMaterial, name)), At(b, offsetof(CMaterial, name)));
    CompareString(d, "helper name2", i, At(a, offsetof(CMaterial, name2)), At(b, offsetof(CMaterial, name2)));
    CompareString(d, "helper name3", i, At(a, offsetof(CMaterial, name3)), At(b, offsetof(CMaterial, name3)));
}

void CompareArray(Diff& d, const char* what, const VtArray& a, const VtArray& b, std::size_t elementSize,
                  void (*compare)(Diff&, int, const void*, const void*)) {
    CompareBody(d, what, 0, BodyAt(&a, 4), BodyAt(&b, 4), elementSize, 0);
    for (int i = 0; i < a.size && !d.failed; ++i)
        compare(d, i, At(a.data, i * elementSize), At(b.data, i * elementSize));
}

bool Empty(const VtArray* a) { return !a->data && a->size == 0; }

// --- The wrapper -------------------------------------------------------------------------------------------

// 00471B10: PUSH -1 (2), PUSH imm32 (5).
mfc::BOOL(__fastcall* s_load)(SceneInfo*, void*, void*, VtArray*, VtArray*, VtArray*);
mfc::BOOL __fastcall LoadVerify(SceneInfo* self, void*, void* file, VtArray* meshes, VtArray* helpers, VtArray* nodes) {
    bool comparable = Empty(meshes) && Empty(helpers) && Empty(nodes);
    std::vector<ReadOp> ops;
    std::vector<std::uint8_t> data;
    ProxyFile recorder{ProxyVtable(), file, &ops, &data, 0, false};
    // The copies start as the real arrays did (vtable and grow step).
    VtArray meshCopy = *meshes, helperCopy = *helpers, nodeCopy = *nodes;
    mfc::BOOL actual = self->Load(reinterpret_cast<mfc::CFileVirtuals*>(&recorder), meshes, helpers, nodes);
    if (!comparable) {
        ei::verify::Check("SceneInfo::Load (ours only)", true, meshes->size == nodes->size, "%d meshes, %d nodes",
                          meshes->size, nodes->size);
        return actual;
    }

    ProxyFile replay{ProxyVtable(), nullptr, &ops, &data, 0, false};
    SceneInfo info = *self;
    info.flags_ = static_cast<std::uint16_t>(~self->flags_);
    mfc::BOOL expected = s_load(&info, nullptr, &replay, &meshCopy, &helperCopy, &nodeCopy);

    Diff d;
    if (replay.mismatch || replay.next != ops.size())
        d.Fail("reads: %u of %u replayed%s", unsigned(replay.next), unsigned(ops.size()), replay.mismatch ? ", mismatch" : "");
    if (expected != actual || info.flags_ != self->flags_) d.Fail("result %d/%d flags %04X/%04X", expected, actual, info.flags_, self->flags_);
    if (!d.failed) CompareArray(d, "meshes", meshCopy, *meshes, mesh::kSize, &CompareMesh);
    if (!d.failed) CompareArray(d, "nodes", nodeCopy, *nodes, node::kSize, &CompareNode);
    if (!d.failed) CompareArray(d, "helpers", helperCopy, *helpers, sizeof(CMaterial), &CompareHelper);
    ei::verify::Check("SceneInfo::Load", true, !d.failed, "%s (%u reads, %d meshes, %d helpers)", d.where, unsigned(ops.size()),
                      meshes->size, helpers->size);

    FreeArray(meshCopy, &DestructMeshes);
    FreeArray(nodeCopy, &DestructNodes);
    SetSizeHelpers(&helperCopy, nullptr, 0, -1);
    return actual;
}

// 00446C10 (LoadFakeUnitScene): opens files and builds the unit's figures through code that is not
// rewritten, so ours runs alone. Afterwards: one figure and one bone-link array per mesh, each
// figure named like its mesh, and the unit's +0x1C vector set to (0, 0, 500).
void* s_unused;
mfc::BOOL __stdcall LoadFakeUnitSceneVerify(const char* fileName, const char* registryName, void* fakeUnit) {
    mfc::BOOL result = LoadFakeUnitScene(fileName, registryName, static_cast<FakeUnitScene*>(fakeUnit));
    const char* u = static_cast<const char*>(fakeUnit);
    Diff d;
    if (result) {
        const VtArray& meshes = *At<VtArray>(u, 0x544);
        const VtArray& figures = *At<VtArray>(u, 0x530);
        const VtArray& boneLinks = *At<VtArray>(u, 0x558);
        if (figures.size != meshes.size || boneLinks.size != figures.size)
            d.Fail("%d meshes, %d figures, %d bone link arrays", meshes.size, figures.size, boneLinks.size);
        for (int i = 0; i < figures.size && !d.failed; ++i) {
            const char* figureName = *At<const char*>(figures.data, i * 0x220 + 0x120);
            const char* meshName = *At<const char*>(meshes.data, i * mesh::kSize + mesh::kName);
            if (std::strcmp(figureName, meshName) != 0) d.Fail("figure %d \"%.40s\", mesh \"%.40s\"", i, figureName, meshName);
        }
        const std::uint32_t expected[3] = {0, 0, 0x44FA0000};
        if (std::memcmp(u + 0x1C, expected, sizeof(expected)) != 0) d.Fail("unit +0x1C not (0, 0, 500)");
    }
    ei::verify::Check("LoadFakeUnitScene", true, !d.failed, "%s: %s", fileName ? fileName : "(null)", d.where);
    return result;
}

} // namespace

EI_VERIFY(0x00471B10, LoadVerify, 7, s_load);
EI_VERIFY(0x00446C10, LoadFakeUnitSceneVerify, 0, s_unused);
