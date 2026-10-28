// The fake unit's skeleton from the scene nodes (BuildSkeleton, called by LoadFakeUnitScene): each node's
// pivot (its first rotation key's vector, +0x70) and rest rotation (its first position key: axis +0x60 and
// angle +0x6C as a quaternion, unless the axis is the zero vector), the mesh named like a node
// moved into the node's frame, the bones built from the root node, then each node's frame array (+0xF8,
// 40-byte records) sized to the frame count with the pivot and rotation in its first record. Plain float
// maths (rule 7). Names provisional; see docs/notes/objects.md ("Rewritten").
#include "scene_loader.h"

#include "figure.h"
#include "maths.h"
#include "../lib/crt.h"
#include "lib/mfc_array.h"
#include "lib/nan_audit.h"
#include "lib/str.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace scene {
void MakeFrameRelative(CAnimationNode*, CAnimationNode*, int);

void ConstructNodeFrames(void* records, int count) {
    mfc::impl::KeyFrames::ConstructElements(static_cast<mfc::impl::KeyFrame40*>(records), count);
}

namespace {

// The node's children (+0x10C; CArray<node*>).
using ChildArray = std::vector<SceneNode*>;

// The node's pivot and rotation copied to their rest copies.
void CopyRest(SceneNode* node) {
    std::memcpy(&node->restRotation, &node->rotation, sizeof(Quaternion));
    std::memcpy(&node->restPivot, &node->pivot, sizeof(Vector3));
}

bool SameAsZero(const Vector3& v) {
    Vector3 zero;
    std::memcpy(&zero, &kZeroVector, sizeof(zero));
    EI_NAN_AUDIT(v.x, v.y, v.z);
    return v.x == zero.x && v.y == zero.y && v.z == zero.z;
}

// The node's children as the CArray<node*> instantiation resizes them.
ChildArray& Children(SceneNode* node) { return node->children; }

} // namespace

// Replaces the skin's root bone (the old one destroyed and freed) by a new one built
// from `node`, then makes frames 0 .. (short)frames of the skeleton relative.
void BuildRootBone(SceneSkin* skin, const SceneNode* node, int frames) {
    if (CAnimationNode* old = skin->root) {
        old->DestructBody();
        gamecrt::operator_delete(old);
    }
    CAnimationNode* memory = gamecrt::operator_new_as<CAnimationNode>();
    CAnimationNode* bone = memory ? memory->Construct() : nullptr;
    skin->root = bone;
    BuildBone(skin, bone, node, frames);
    int count = static_cast<std::int16_t>(frames) + 1;
    for (int f = 0; f < count; ++f) MakeFrameRelative(nullptr, skin->root, f);
}

// Every child's rest pivot and rotation copied, recursively.
void CopyRestPose(SceneNode* node) {
    ChildArray& children = Children(node);
    for (int i = 0; i < std::ssize(children); ++i) {
        CopyRest(children[i]);
        CopyRestPose(children[i]);
    }
}

// Links each node to its children (nodes whose parent name equals its name, _mbscmp), finds the root (the
// last node with an empty parent name, -1 when none) and copies the rest pose from it down. Without a root
// the element before the array is read and written; kept.
int FindRootNode(NodeArray* nodeArray) {
    NodeArray& nodes = *nodeArray;
    int root = -1;
    for (int i = 0; i < nodes.size; ++i) {
        if (nodes.data[i].parent.size() == 0) root = i;
        for (int j = 0; j < nodes.size; ++j) {
            SceneNode* child = &nodes.data[j];
            if (str::Compare(child->parent,
                                nodes.data[i].name) != 0)
                continue;
            ChildArray& children = Children(&nodes.data[i]);
            int n = std::ssize(children);
            children.resize(n + 1);
            children[n] = child;
        }
    }
    SceneNode* rootNode = &nodes.data[root];
    CopyRest(rootNode);
    ChildArray& children = Children(rootNode);
    for (int i = 0; i < std::ssize(children); ++i) {
        CopyRest(children[i]);
        CopyRestPose(children[i]);
    }
    return root;
}

// Takes the unit's meshes (a CArray; a mesh's virtual 7 is its name) and the nodes; `frames` (its
// low word counts), `skin`. 1, or 0 without a root node; a first rotation key not at frame 0 is a fatal
// error (game::FatalError's result is returned if it comes back).
int BuildSkeleton(MeshArray* meshArray, NodeArray* nodeArray, int frames, SceneSkin* skin) {
    MeshArray& meshes = *meshArray;
    NodeArray& nodes = *nodeArray;
    for (int i = 0; i < nodes.size; ++i) {
        SceneNode* node = &nodes.data[i];
        const NodeRotationKey& rotationKey = node->rotationKeys[0];
        if (static_cast<std::uint16_t>(rotationKey.frame) != 0) return game::FatalError(FirstKeyNotAtZeroText());
        node->pivot = rotationKey.value;
        const NodePositionKey& positionKey = node->positionKeys[0];
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
            if (str::Compare(candidate->name,
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
        for (int i = 0; i < std::ssize(positions); ++i) {
            Vector3& p = positions[i].point;   // the file's bits as floats
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

    // Repeated once per mesh.
    for (int m = 0; m < meshes.size; ++m) {
        for (int j = 0; j < nodes.size; ++j) {
            SceneNode* node = &nodes.data[j];
            NodeFrameArray& records = node->frames;
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

} // namespace scene
