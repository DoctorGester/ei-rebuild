// Checks for the animated node's lifetime (animation_node_lifetime.cpp, A-U10).
// - Construct (00506710) allocates nothing (the names take the shared empty string): the original
//   builds into a raw copy of the node's memory, ours into the node; every byte compared except
//   the pose / rest positions and the morph, which neither writes.
// - DestructBody (005068C0): ours only; no mesh, no children, the array storage gone.
// Reached by the fake units' skeletons (scene loading: 00449240 / 0044A163; the skin code's
// 005087B0-00508CD0 destroy them) - units, unit_create and the models scenarios.
#include "verify.h"

#include "../../src/game/figure.h"

#include <cstddef>
#include <cstring>

namespace {

bool Written(std::size_t at) {
    constexpr std::size_t kPose = offsetof(CAnimationNode, posePosition_);
    constexpr std::size_t kRest = offsetof(CAnimationNode, restPosition_);
    constexpr std::size_t kMorph = offsetof(CAnimationNode, morphS_);
    return !(at >= kPose && at < kPose + sizeof(Vector3)) && !(at >= kRest && at < kRest + sizeof(Vector3)) &&
           at < kMorph;
}

} // namespace

// 00506710: MOV EAX,ECX; PUSH ESI; MOV ECX,[0x00796E48] (9 bytes).
static void* s_construct;
static CAnimationNode* __fastcall ConstructVerify(CAnimationNode* node, void*) {
    alignas(8) unsigned char scratch[sizeof(CAnimationNode)];
    std::memcpy(scratch, static_cast<void*>(node), sizeof(scratch));
    auto* copy = reinterpret_cast<CAnimationNode*>(scratch);
    CAnimationNode* expected = reinterpret_cast<CAnimationNode*(__fastcall*)(CAnimationNode*)>(s_construct)(copy);
    CAnimationNode* result = node->Construct();
    const auto* ours = reinterpret_cast<const unsigned char*>(node);
    int differs = -1;
    for (std::size_t i = 0; i < sizeof(CAnimationNode) && differs < 0; ++i)
        if (Written(i) && scratch[i] != ours[i]) differs = static_cast<int>(i);
    ei::verify::Check("CAnimationNode::Construct", true, expected == copy && result == node && differs < 0,
                      "first difference at +0x%X", differs);
    return result;
}
EI_VERIFY(0x00506710, ConstructVerify, 9, s_construct);

static void* s_unusedDestruct;
static void __fastcall DestructBodyVerify(CAnimationNode* node, void*) {
    node->DestructBody();
    bool ok = !node->mesh_ && node->children_.size == 0 && !node->children_.data && node->spare000_ == 0;
    ei::verify::Check("CAnimationNode::DestructBody", true, ok, "node %p", static_cast<void*>(node));
}
EI_VERIFY(0x005068C0, DestructBodyVerify, 0, s_unusedDestruct);
