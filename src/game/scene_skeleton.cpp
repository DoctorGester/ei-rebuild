// The fake unit's skeleton from the scene nodes (00448A40, Ghidra: BuildSkeleton; called by
// LoadFakeUnitScene): each node's pivot (its first rotation key's vector, +0x70) and rest rotation
// (its first position key: axis +0x60 and angle +0x6C as a quaternion at +0x7C, unless the axis is
// the zero vector 0079B9B0), the mesh named like a node moved into the node's frame, the bones built
// from the root node (00449240), then each node's frame array (+0xF8, 40-byte records) sized to the
// frame count with the pivot and rotation in its first record. Plain float maths (rule 7). Names
// provisional; see docs/notes/objects.md ("Rewritten").
#include "scene_loader.h"

#include "figure.h"
#include "maths.h"
#include "../lib/crt.h"
#include "lib/mfc_array.h"
#include "ei/hook.h"
#include "originals_render.h"
#include "originals_engine.h"
#include "lib/nan_audit.h"

#include <cstdint>
#include <cstring>

namespace scene {
void __fastcall MakeFrameRelative(CAnimationNode*, CAnimationNode*, int);
namespace {

// The node's children (+0x10C; CArray<node*>, SetSize 00453790).
using ChildArray = mfc::CArray<SceneNode*>;

// The node's pivot and rotation copied to their rest copies.
void CopyRest(SceneNode* node) {
    std::memcpy(&node->restRotation, &node->rotation, sizeof(Quaternion));
    std::memcpy(&node->restPivot, &node->pivot, sizeof(Vector3));
}
// 00453730: constructs n 40-byte frame records [pointer, count] (mfc::impl::KeyFrames').
void __stdcall ConstructFrameRecords(void* records, int count) {
    mfc::impl::KeyFrames::ConstructElements(static_cast<mfc::impl::KeyFrame40*>(records), count);
}

bool SameAsZero(const Vector3& v) {
    Vector3 zero;
    std::memcpy(&zero, &ZeroVector(), sizeof(zero));
    EI_NAN_AUDIT(v.x, v.y, v.z);
    return v.x == zero.x && v.y == zero.y && v.z == zero.z;
}

// The frame array (MFC CArray of NodeFrame; elements constructed by 00453730, never destroyed).
using FrameArray = mfc::CArray<NodeFrame, OriginalElements<NodeFrame, ConstructFrameRecords>>;
FrameArray& Frames(SceneNode* node) { return Typed<FrameArray>(node->frames); }

// The node's children as the CArray<node*> instantiation resizes them.
ChildArray& Children(SceneNode* node) { return node->children; }

} // namespace

// 00449240: replaces the skin's root bone (the old one destroyed and freed) by a new one built
// from `node`, then makes frames 0 .. (short)frames of the skeleton relative.
void __fastcall BuildRootBone(SceneSkin* skin, const SceneNode* node, int frames) {
    if (CAnimationNode* old = skin->root) {
        old->DestructBody();
        gamecrt::operator_delete(old);
    }
    void* memory = gamecrt::operator_new(sizeof(CAnimationNode));
    CAnimationNode* bone = memory ? static_cast<CAnimationNode*>(memory)->Construct() : nullptr;
    skin->root = bone;
    BuildBone(skin, bone, node, frames);
    int count = static_cast<std::int16_t>(frames) + 1;
    for (int f = 0; f < count; ++f) MakeFrameRelative(nullptr, skin->root, f);
}
EI_REPLACE(0x00449240, BuildRootBone);

// 004491C0: every child's rest pivot and rotation copied, recursively.
void __fastcall CopyRestPose(SceneNode* node) {
    ChildArray& children = Children(node);
    for (int i = 0; i < children.size; ++i) {
        CopyRest(children.data[i]);
        CopyRestPose(children.data[i]);
    }
}

// 00449030: links each node to its children (nodes whose parent name equals its name, _mbscmp),
// finds the root (the last node with an empty parent name, -1 when none) and copies the rest pose
// from it down. Without a root the original reads and writes the element before the array; kept.
int __fastcall FindRootNode(VtArray* nodeArray) {
    NodeArray& nodes = Typed<NodeArray>(*nodeArray);
    int root = -1;
    for (int i = 0; i < nodes.size; ++i) {
        if (nodes.data[i].parent.GetLength() == 0) root = i;
        for (int j = 0; j < nodes.size; ++j) {
            SceneNode* child = &nodes.data[j];
            if (gamecrt::mbscmp(static_cast<const char*>(child->parent),
                                static_cast<const char*>(nodes.data[i].name)) != 0)
                continue;
            ChildArray& children = Children(&nodes.data[i]);
            int n = children.size;
            children.SetSize(n + 1, -1);
            children.data[n] = child;
        }
    }
    SceneNode* rootNode = &nodes.data[root];
    CopyRest(rootNode);
    ChildArray& children = Children(rootNode);
    for (int i = 0; i < children.size; ++i) {
        CopyRest(children.data[i]);
        CopyRestPose(children.data[i]);
    }
    return root;
}
EI_REPLACE(0x00449030, FindRootNode);

// ECX: the unit's meshes (CArray, 0x18C each; slot 7 of a mesh is its name), EDX: the nodes;
// `frames` (its low word counts), `skin`. 1, or 0 without a root node; a first rotation key not
// at frame 0 is a fatal error (game::FatalError's result is returned if it comes back).
int __fastcall BuildSkeleton(VtArray* meshArray, VtArray* nodeArray, int frames, SceneSkin* skin) {
    MeshArray& meshes = Typed<MeshArray>(*meshArray);
    NodeArray& nodes = Typed<NodeArray>(*nodeArray);
    for (int i = 0; i < nodes.size; ++i) {
        SceneNode* node = &nodes.data[i];
        const NodeRotationKey& rotationKey = node->rotationKeys.data[0];
        if (static_cast<std::uint16_t>(rotationKey.frame) != 0) return game::FatalError(FirstKeyNotAtZeroText());
        node->pivot = rotationKey.value;
        const NodePositionKey& positionKey = node->positionKeys.data[0];
        if (static_cast<std::uint16_t>(positionKey.frame) != 0) continue;
        node->axis = positionKey.axis;
        std::memcpy(&node->angle, &positionKey.angle, sizeof(float));
        if (!SameAsZero(node->axis)) {
            Quaternion q;
            q.SetAxisAngle(node->axis, node->angle);
            node->rotation = q;
        }
    }
    int root = FindRootNode(nodeArray);
    if (root == -1) {
        game::FatalError(NoRootNodeText());
        return 0;
    }

    for (int m = 0; m < meshes.size; ++m) {
        SceneMesh* mesh = &meshes.data[m];
        SceneNode* node = nullptr;
        for (int j = 0; j < nodes.size; ++j) {
            SceneNode* candidate = &nodes.data[j];
            const char* meshName = mesh->GetName();
            if (gamecrt::mbscmp(static_cast<const char*>(candidate->name),
                                meshName) == 0) {
                node = candidate;
                break;
            }
        }
        if (!node) continue;
        Quaternion& q = node->rotation;
        float x2 = q.x + q.x, y2 = q.y + q.y, z2 = q.z + q.z;
        float xw = x2 * q.w, yw = y2 * q.w, zw = z2 * q.w;
        float xx = x2 * q.x, xy = y2 * q.x, xz = z2 * q.x;
        float yy = y2 * q.y, yz = z2 * q.y, zz = z2 * q.z;
        float m00 = 1.0f - (zz + yy), m01 = xy - zw, m02 = xz + yw;
        float m10 = xy + zw, m11 = 1.0f - (zz + xx), m12 = yz - xw;
        float m20 = xz - yw, m21 = yz + xw, m22 = 1.0f - (yy + xx);
        PointBuffer& positions = mesh->positions;
        const Vector3& pivot = node->pivot;
        for (int i = 0; i < positions.size; ++i) {
            Vector3& p = reinterpret_cast<Vector3&>(positions.data[i]);   // the file's bits as floats
            p = p - pivot;
            float rx = DotUnrounded<Sum3::XYZ>(m00, m01, m02, p.x, p.y, p.z);
            float ry = DotUnrounded<Sum3::XYZ>(m10, m11, m12, p.x, p.y, p.z);
            float rz = DotUnrounded<Sum3::XYZ>(m20, m21, m22, p.x, p.y, p.z);
            p = {rx, ry, rz};
        }
        q.x = -q.x;
        q.y = -q.y;
        q.z = -q.z;
    }

    BuildRootBone(skin, &nodes.data[root], frames);

    // Repeated once per mesh, as the original does.
    for (int m = 0; m < meshes.size; ++m) {
        for (int j = 0; j < nodes.size; ++j) {
            SceneNode* node = &nodes.data[j];
            FrameArray& records = Frames(node);
            records.SetSize(static_cast<std::int16_t>(frames));
            NodeFrame* first = records.data;
            first->pivot = node->pivot;
            first->rotation = node->rotation;
            node->spare010 = 0;
            node->spare014 = 0;
        }
    }
    return 1;
}
EI_REPLACE(0x00448A40, BuildSkeleton);

} // namespace scene
