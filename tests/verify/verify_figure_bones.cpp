// Checks for the skeleton's pose (figure_bones.cpp).
#include "verify.h"

#include "../../src/game/figure_mesh.h"
#include "../../src/game/map_object_figures.h"

#include <cstring>
#include <vector>

namespace {

// What Place writes per bone: its position and rotation and its mesh's.
struct BoneState {
    FigureBone* bone;
    Vector3 position;
    Quaternion rotation;
    Vector3 meshPosition;
    Quaternion meshRotation;
    int offsetCount;
};

void Collect(FigureBone* bone, std::vector<BoneState>& out) {
    BoneState s{};
    s.bone = bone;
    out.push_back(s);
    for (auto* node = bone->children.head; node; node = node->next) Collect(node->data, out);
}

void Save(std::vector<BoneState>& states) {
    for (BoneState& s : states) {
        s.position = s.bone->position;
        s.rotation = s.bone->rotation;
        if (CFigureMesh* mesh = s.bone->mesh) {
            s.meshPosition = mesh->localPosition_;
            s.meshRotation = mesh->localRotation_;
            s.offsetCount = mesh->offsets_.size;
        }
    }
}

void Restore(const std::vector<BoneState>& states) {
    for (const BoneState& s : states) {
        s.bone->position = s.position;
        s.bone->rotation = s.rotation;
        if (CFigureMesh* mesh = s.bone->mesh) {
            mesh->localPosition_ = s.meshPosition;
            mesh->localRotation_ = s.meshRotation;
        }
    }
}

bool Near(const float* a, const float* b, int n) {
    for (int i = 0; i < n; ++i)
        if (!ei::verify::NearlyEqual(a[i], b[i], 4, 1e-6f)) return false;
    return true;
}

int s_depth;

} // namespace

// 0050C230 recurses through its game address, so a call made while the wrapper runs is the
// original's own recursion and goes straight to it. At the top: the subtree's bones and meshes are
// snapshotted, the original runs, its results are kept, the snapshot is restored and ours runs;
// positions and rotations (bones and meshes) are compared within 4 ULPs, the meshes' offset counts
// exactly. The offsets (005A0B10) are written by both runs from the same vertices. Reached whenever
// a unit's animation changes (every scenario with units).
static void* s_place;
static void __fastcall PlaceVerify(FigureBone* bone, void*, const Vector3* origin) {
    using Original = void(__fastcall*)(FigureBone*, void*, const Vector3*);
    if (s_depth > 0) {
        reinterpret_cast<Original>(s_place)(bone, nullptr, origin);
        return;
    }
    std::vector<BoneState> before, expected, actual;
    Collect(bone, before);
    Save(before);
    ++s_depth;
    reinterpret_cast<Original>(s_place)(bone, nullptr, origin);
    --s_depth;
    expected = before;
    Save(expected);
    Restore(before);
    bone->Place(origin);
    actual = before;
    Save(actual);

    bool ok = true;
    std::size_t at = 0;
    for (; ok && at < expected.size(); ++at) {
        const BoneState& e = expected[at];
        const BoneState& a = actual[at];
        ok = Near(&e.position.x, &a.position.x, 3) && Near(&e.rotation.w, &a.rotation.w, 4) &&
             Near(&e.meshPosition.x, &a.meshPosition.x, 3) && Near(&e.meshRotation.w, &a.meshRotation.w, 4) &&
             e.offsetCount == a.offsetCount;
    }
    ei::verify::Check("FigureBone::Place", true, ok, "%d bones, mismatch at bone %d", static_cast<int>(expected.size()),
                      ok ? -1 : static_cast<int>(at) - 1);
}
EI_VERIFY(0x0050C230, PlaceVerify, 6, s_place);
