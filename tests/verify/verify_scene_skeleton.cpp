// Checks for scene_skeleton.cpp: ours only (both build the skeleton, allocate bones and move the
// meshes' positions). BuildSkeleton: with a root it returns 1 and every node's frame array holds
// the frame count with the node's pivot and rotation first; BuildRootBone: the skin has a root
// bone afterwards.
#include "verify.h"

#include "../../src/game/scene_loader.h"

#include <cstdint>
#include <cstring>

namespace scene {
int __fastcall BuildSkeleton(VtArray* meshes, VtArray* nodes, int frames, void* skin);
void __fastcall BuildRootBone(char* skin, const char* node, int frames);
}

namespace {
void* s_unused;
using namespace scene;

int __fastcall SkeletonVerify(VtArray* meshes, VtArray* nodes, int frames, void* skin) {
    int result = BuildSkeleton(meshes, nodes, frames, skin);
    bool ok = true;
    if (result == 1 && meshes->size > 0) {
        for (int j = 0; ok && j < nodes->size; ++j) {
            char* node = reinterpret_cast<char*>(nodes->data) + j * node::kSize;
            auto& records = *reinterpret_cast<VtArray*>(node + 0xF8);
            ok = records.size == static_cast<std::int16_t>(frames) &&
                 (records.size == 0 ||
                  (std::memcmp(reinterpret_cast<char*>(records.data) + 0xC, node + 0x70, 12) == 0 &&
                   std::memcmp(reinterpret_cast<char*>(records.data) + 0x18, node + 0x7C, 16) == 0));
        }
    }
    ei::verify::Check("BuildSkeleton", true, ok, "result %d", result);
    return result;
}

void __fastcall RootBoneVerify(char* skin, const char* node, int frames) {
    BuildRootBone(skin, node, frames);
    ei::verify::Check("BuildRootBone", true, *reinterpret_cast<char**>(skin + 0x14) != nullptr, "");
}
} // namespace

EI_VERIFY(0x00448A40, SkeletonVerify, 0, s_unused);
EI_VERIFY(0x00449240, RootBoneVerify, 0, s_unused);
