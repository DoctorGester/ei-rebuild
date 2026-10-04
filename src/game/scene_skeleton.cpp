// The fake unit's skeleton from the scene nodes (00448A40, Ghidra: BuildSkeleton; called by
// LoadFakeUnitScene): each node's pivot (its first rotation key's vector, +0x70) and rest rotation
// (its first position key: axis +0x60 and angle +0x6C as a quaternion at +0x7C, unless the axis is
// the zero vector 0079B9B0), the mesh named like a node moved into the node's frame, the bones built
// from the root node (00449240), then each node's frame array (+0xF8, 40-byte records) sized to the
// frame count with the pivot and rotation in its first record. Plain float maths (rule 7). Names
// provisional; see docs/notes/objects.md ("Sixth round").
#include "scene_loader.h"

#include "maths.h"
#include "../lib/crt.h"
#include "ei/hook.h"
#include "ei/vcall.h"

#include <cstdint>
#include <cstring>

namespace scene {
namespace {

template <typename T>
T& At(const void* base, std::size_t offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(const_cast<void*>(base)) + offset);
}

// Node members (0x120 bytes; see scene_loader.h).
constexpr std::size_t kNodeAxis = 0x60;        // Vector3: the first position key's vector
constexpr std::size_t kNodeAngle = 0x6C;       // float: the first position key's +8
constexpr std::size_t kNodePivot = 0x70;       // Vector3: the first rotation key's vector
constexpr std::size_t kNodeRotation = 0x7C;    // Quaternion
constexpr std::size_t kNodeFrames = 0xF8;      // CArray of 40-byte records (vtable at +0xF8)

__declspec(naked) int __cdecl FatalError(const char*, ...) EI_ORIGINAL(0x00480080)
// 00449030 (below), through its address: the index of the root node (-1 when there is none).
__declspec(naked) int __fastcall FindRootNodeAt(VtArray* /*nodes*/) EI_ORIGINAL(0x00449030)
// CArray<node*>::SetSize (00453790, library): the node's children (+0x10C).
__declspec(naked) void __fastcall ChildrenSetSize(VtArray*, void*, int, int) EI_ORIGINAL(0x00453790)
__declspec(naked) void __fastcall CopyRestPoseAt(char*) EI_ORIGINAL(0x004491C0)
constexpr std::size_t kNodeChildren = 0x10C;   // CArray of node pointers
constexpr std::size_t kNodeRestPivot = 0x8C;   // Vector3: the pivot copied
constexpr std::size_t kNodeRestRotation = 0x98;   // Quaternion: the rotation copied

// The node's pivot and rotation copied to their rest copies.
void CopyRest(char* node) {
    std::memcpy(node + kNodeRestRotation, node + kNodeRotation, 16);
    std::memcpy(node + kNodeRestPivot, node + kNodePivot, 12);
}
// 00449240 (below), through its address.
__declspec(naked) void __fastcall BuildRootBoneAt(void* /*skin*/, char* /*node*/, int /*frames*/) EI_ORIGINAL(0x00449240)
// The bone (0x128 bytes): constructor 00506710 (returns this), destructor body 005068C0; BuildBone
// 004492F0 (scene_bones.cpp) and MakeFrameRelative 0044EB60 (scene_bone_frames.cpp), through their
// addresses.
__declspec(naked) char* __fastcall BoneConstruct(void*) EI_ORIGINAL(0x00506710)
__declspec(naked) void __fastcall BoneDestruct(char*) EI_ORIGINAL(0x005068C0)
__declspec(naked) void __fastcall BuildBoneAt(void*, char*, const char*, int) EI_ORIGINAL(0x004492F0)
__declspec(naked) void __fastcall MakeFrameRelativeAt(char*, char*, int) EI_ORIGINAL(0x0044EB60)
// 00453730: constructs n 40-byte frame records [pointer, count].
__declspec(naked) void __stdcall ConstructFrameRecords(void*, int) EI_ORIGINAL(0x00453730)

bool SameAsZero(const Vector3& v) {
    Vector3 zero;
    std::memcpy(&zero, reinterpret_cast<const void*>(0x0079B9B0), sizeof(zero));
    // FCOMP and TEST AH,0x40: equal or unordered counts as equal.
    auto same = [](float a, float b) { return !(a < b) && !(a > b); };
    return same(v.x, zero.x) && same(v.y, zero.y) && same(v.z, zero.z);
}

// The frame array's SetSize as inlined (MFC, new storage, elements constructed, never destroyed).
void SetSizeFrames(VtArray& array, int n) {
    ArrayBody& a = array.body;
    if (n == 0) {
        if (a.data) {
            gamecrt::operator_delete(a.data);
            a.data = nullptr;
        }
        a.max = 0;
        a.size = 0;
    } else if (!a.data) {
        a.data = gamecrt::operator_new(static_cast<std::size_t>(n) * 40);
        ConstructFrameRecords(a.data, n);
        a.max = n;
        a.size = n;
    } else if (n <= a.max) {
        if (a.size < n) ConstructFrameRecords(static_cast<char*>(a.data) + a.size * 40, n - a.size);
        a.size = n;
    } else {
        int grow = a.grow;
        if (grow == 0) {
            grow = a.size / 8;
            if (grow < 4) grow = 4;
            else if (grow > 0x400) grow = 0x400;
        }
        int capacity = n < a.max + grow ? a.max + grow : n;
        void* data = gamecrt::operator_new(static_cast<std::size_t>(capacity) * 40);
        std::memcpy(data, a.data, static_cast<std::size_t>(a.size) * 40);
        ConstructFrameRecords(static_cast<char*>(data) + a.size * 40, n - a.size);
        gamecrt::operator_delete(a.data);
        a.data = data;
        a.size = n;
        a.max = capacity;
    }
}

} // namespace

// 00449240: replaces the skin's root bone (+0x14; the old one destroyed and freed) by a new one
// built from `node`, then makes frames 0 .. (short)frames of the skeleton relative.
void __fastcall BuildRootBone(char* skin, const char* node, int frames) {
    if (char* old = At<char*>(skin, 0x14)) {
        BoneDestruct(old);
        gamecrt::operator_delete(old);
    }
    void* memory = gamecrt::operator_new(0x128);
    char* bone = memory ? BoneConstruct(memory) : nullptr;
    At<char*>(skin, 0x14) = bone;
    BuildBoneAt(skin, bone, node, frames);
    int count = static_cast<std::int16_t>(frames) + 1;
    for (int f = 0; f < count; ++f) MakeFrameRelativeAt(nullptr, At<char*>(skin, 0x14), f);
}
EI_REPLACE(0x00449240, BuildRootBone);

// 004491C0: every child's rest pivot and rotation copied, recursively.
void __fastcall CopyRestPose(char* node) {
    VtArray& children = At<VtArray>(node, kNodeChildren);
    for (int i = 0; i < children.body.size; ++i) {
        CopyRest(static_cast<char**>(children.body.data)[i]);
        CopyRestPoseAt(static_cast<char**>(children.body.data)[i]);
    }
}
EI_REPLACE(0x004491C0, CopyRestPose);

// 00449030: links each node to its children (nodes whose parent name equals its name, _mbscmp),
// finds the root (the last node with an empty parent name, -1 when none) and copies the rest pose
// from it down. Without a root the original reads and writes the element before the array; kept.
int __fastcall FindRootNode(VtArray* nodes) {
    int root = -1;
    for (int i = 0; i < nodes->body.size; ++i) {
        char* parent = static_cast<char*>(nodes->body.data) + i * node::kSize;
        if (reinterpret_cast<const int*>(At<char*>(parent, node::kParent))[-2] == 0) root = i;   // CString length
        for (int j = 0; j < nodes->body.size; ++j) {
            char* base = static_cast<char*>(nodes->body.data);
            char* child = base + j * node::kSize;
            if (gamecrt::mbscmp(At<const unsigned char*>(child, node::kParent), At<const unsigned char*>(base + i * node::kSize, node::kName)) != 0)
                continue;
            VtArray& children = At<VtArray>(base + i * node::kSize, kNodeChildren);
            int n = children.body.size;
            ChildrenSetSize(&children, nullptr, n + 1, -1);
            static_cast<char**>(children.body.data)[n] = child;
        }
    }
    char* rootNode = static_cast<char*>(nodes->body.data) + root * static_cast<int>(node::kSize);
    CopyRest(rootNode);
    VtArray& children = At<VtArray>(rootNode, kNodeChildren);
    for (int i = 0; i < children.body.size; ++i) {
        CopyRest(static_cast<char**>(children.body.data)[i]);
        CopyRestPoseAt(static_cast<char**>(children.body.data)[i]);
    }
    return root;
}
EI_REPLACE(0x00449030, FindRootNode);

// ECX: the unit's meshes (CArray, 0x18C each; slot 7 of a mesh is its name), EDX: the nodes;
// `frames` (its low word counts), `skin`. 1, or 0 without a root node; a first rotation key not
// at frame 0 is a fatal error (FatalError's result is returned if it comes back).
int __fastcall BuildSkeleton(VtArray* meshes, VtArray* nodes, int frames, void* skin) {
    for (int i = 0; i < nodes->body.size; ++i) {
        char* node = static_cast<char*>(nodes->body.data) + i * node::kSize;
        const char* rotationKey = At<char*>(node, node::kRotKeys + 4);
        if (At<std::uint16_t>(rotationKey, 0) != 0)
            return FatalError(reinterpret_cast<const char*>(0x007843AC));   // "ResolveNodesAMF. ..."
        At<Vector3>(node, kNodePivot) = At<Vector3>(rotationKey, 8);
        const char* positionKey = At<char*>(node, node::kPosKeys + 4);
        if (At<std::uint16_t>(positionKey, 0) != 0) continue;
        At<Vector3>(node, kNodeAxis) = At<Vector3>(positionKey, 0xC);
        At<std::uint32_t>(node, kNodeAngle) = At<std::uint32_t>(positionKey, 8);
        if (!SameAsZero(At<Vector3>(node, kNodeAxis))) {
            Quaternion q;
            q.SetAxisAngle(At<Vector3>(node, kNodeAxis), At<float>(node, kNodeAngle));
            At<Quaternion>(node, kNodeRotation) = q;
        }
    }
    int root = FindRootNodeAt(nodes);
    if (root == -1) {
        FatalError(reinterpret_cast<const char*>(0x007843F0));   // "Can't find top-level object in hierarchy"
        return 0;
    }

    for (int m = 0; m < meshes->body.size; ++m) {
        char* mesh = static_cast<char*>(meshes->body.data) + m * mesh::kSize;
        char* node = nullptr;
        for (int j = 0; j < nodes->body.size; ++j) {
            char* candidate = static_cast<char*>(nodes->body.data) + j * node::kSize;
            const char* meshName = ei::VCall<const char*>(mesh, 7);
            if (gamecrt::mbscmp(At<const unsigned char*>(candidate, node::kName),
                                reinterpret_cast<const unsigned char*>(meshName)) == 0) {
                node = candidate;
                break;
            }
        }
        if (!node) continue;
        Quaternion& q = At<Quaternion>(node, kNodeRotation);
        float x2 = q.x + q.x, y2 = q.y + q.y, z2 = q.z + q.z;
        float xw = x2 * q.w, yw = y2 * q.w, zw = z2 * q.w;
        float xx = x2 * q.x, xy = y2 * q.x, xz = z2 * q.x;
        float yy = y2 * q.y, yz = z2 * q.y, zz = z2 * q.z;
        float m00 = 1.0f - (zz + yy), m01 = xy - zw, m02 = xz + yw;
        float m10 = xy + zw, m11 = 1.0f - (zz + xx), m12 = yz - xw;
        float m20 = xz - yw, m21 = yz + xw, m22 = 1.0f - (yy + xx);
        ArrayBody& positions = At<ArrayBody>(mesh, mesh::kPositions);
        const Vector3& pivot = At<Vector3>(node, kNodePivot);
        for (int i = 0; i < positions.size; ++i) {
            Vector3& p = static_cast<Vector3*>(positions.data)[i];
            p.x = p.x - pivot.x;
            p.y = p.y - pivot.y;
            p.z = p.z - pivot.z;
            float rx = (m00 * p.x + m01 * p.y) + m02 * p.z;
            float ry = (m11 * p.y + m10 * p.x) + m12 * p.z;
            float rz = (m21 * p.y + m20 * p.x) + m22 * p.z;
            p = {rx, ry, rz};
        }
        q.x = -q.x;
        q.y = -q.y;
        q.z = -q.z;
    }

    BuildRootBoneAt(skin, static_cast<char*>(nodes->body.data) + root * node::kSize, frames);

    // Repeated once per mesh, as the original does.
    for (int m = 0; m < meshes->body.size; ++m) {
        for (int j = 0; j < nodes->body.size; ++j) {
            char* node = static_cast<char*>(nodes->body.data) + j * node::kSize;
            VtArray& records = At<VtArray>(node, kNodeFrames);
            SetSizeFrames(records, static_cast<std::int16_t>(frames));
            char* first = static_cast<char*>(records.body.data);
            At<Vector3>(first, 0xC) = At<Vector3>(node, kNodePivot);
            At<Quaternion>(first, 0x18) = At<Quaternion>(node, kNodeRotation);
            At<std::uint32_t>(node, 0x10) = 0;
            At<std::uint32_t>(node, 0x14) = 0;
        }
    }
    return 1;
}
EI_REPLACE(0x00448A40, BuildSkeleton);

} // namespace scene
