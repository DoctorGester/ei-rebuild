// Checks for scene_skeleton.cpp: ours only (both build the skeleton, allocate bones and move the
// meshes' positions). BuildSkeleton: with a root it returns 1 and every node's frame array holds
// the frame count with the node's pivot and rotation first; BuildRootBone: the skin has a root
// bone afterwards.
#include "verify.h"

#include "../../src/game/scene_loader.h"

#include <cstdint>
#include <cstring>

namespace scene {
int __fastcall BuildSkeleton(VtArray* meshes, VtArray* nodes, int frames, SceneSkin* skin);
void __fastcall BuildRootBone(SceneSkin* skin, const SceneNode* node, int frames);
}

namespace {
void* s_unused;
using namespace scene;

int __fastcall SkeletonVerify(VtArray* meshes, VtArray* nodes, int frames, SceneSkin* skin) {
    int result = BuildSkeleton(meshes, nodes, frames, skin);
    bool ok = true;
    if (result == 1 && meshes->size > 0) {
        NodeArray& typed = Typed<NodeArray>(*nodes);
        for (int j = 0; ok && j < typed.size; ++j) {
            const SceneNode& node = typed.data[j];
            const auto* records = reinterpret_cast<const NodeFrame*>(node.frames.data);
            ok = node.frames.size == static_cast<std::int16_t>(frames) &&
                 (node.frames.size == 0 ||
                  (std::memcmp(&records[0].pivot, &node.pivot, sizeof(Vector3)) == 0 &&
                   std::memcmp(&records[0].rotation, &node.rotation, sizeof(Quaternion)) == 0));
        }
    }
    ei::verify::Check("BuildSkeleton", true, ok, "result %d", result);
    return result;
}

void __fastcall RootBoneVerify(SceneSkin* skin, const SceneNode* node, int frames) {
    BuildRootBone(skin, node, frames);
    ei::verify::Check("BuildRootBone", true, skin->root != nullptr, "");
}
} // namespace

EI_VERIFY(0x00448A40, SkeletonVerify, 0, s_unused);
EI_VERIFY(0x00449240, RootBoneVerify, 0, s_unused);
