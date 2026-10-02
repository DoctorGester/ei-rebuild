// Differential check for CAnimationNode::SetMorph: run the original, keep its outputs, restore the
// fields it writes, run ours, compare.
#include "verify.h"

#include "../../src/game/figure.h"

#include <cstddef>
#include <cstring>

// 005064C0 starts with SUB ESP,0x3C; MOV EAX,[ESP+0x44] (7 bytes).
static void(__fastcall* s_setMorph)(CAnimationNode*, void*, const BoneMorphBox*, float, float, float);

static void __fastcall SetMorphVerify(CAnimationNode* node, void*, const BoneMorphBox* box, float s, float t, float u) {
    constexpr std::size_t kOffset = offsetof(CAnimationNode, position_), kSize = 0x18;
    unsigned char before[kSize], expected[kSize];
    std::memcpy(before, reinterpret_cast<char*>(node) + kOffset, kSize);
    s_setMorph(node, nullptr, box, s, t, u);
    std::memcpy(expected, reinterpret_cast<char*>(node) + kOffset, kSize);
    std::memcpy(reinterpret_cast<char*>(node) + kOffset, before, kSize);
    node->SetMorph(box, s, t, u);
    bool same = std::memcmp(expected, reinterpret_cast<char*>(node) + kOffset, kSize) == 0;
    const Vector3& e = *reinterpret_cast<const Vector3*>(expected);
    ei::verify::Check("CAnimationNode::SetMorph", true, same, "s %g t %g u %g -> (%g %g %g) vs (%g %g %g)", s, t, u, e.x,
                      e.y, e.z, node->position_.x, node->position_.y, node->position_.z);
    std::memcpy(reinterpret_cast<char*>(node) + kOffset, expected, kSize);
}
EI_VERIFY(0x005064C0, SetMorphVerify, 7, s_setMorph);
