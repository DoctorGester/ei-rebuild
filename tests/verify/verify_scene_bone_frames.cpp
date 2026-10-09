// Checks for scene_bone_frames.cpp. The function rewrites the frame of a whole subtree, so: the
// fields it touches in every bone of the subtree are saved, the original runs (its recursive calls
// go to the original too), its results are recorded, the saved fields are put back, ours runs and
// the results are compared within a few ULPs (rule 7: plain float maths, std::acos).
#include "verify.h"

#include "../../src/game/figure.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace scene {
void __fastcall MakeFrameRelative(CAnimationNode* parent, CAnimationNode* bone, int frame);
}

namespace {

struct Field {
    char* at;
    std::size_t size;
};

char* Bytes(void* p) { return static_cast<char*>(p); }

void Collect(CAnimationNode* bone, int frame, std::vector<Field>& out) {
    AnimationRotationKey* keys = bone->rotationKeys_.data;
    out.push_back({Bytes(&keys[frame]), sizeof(AnimationRotationKey)});
    if (frame > 0) out.push_back({Bytes(&keys[frame - 1]), sizeof(AnimationRotationKey)});
    out.push_back({Bytes(&bone->translationKeys_.data[frame]), sizeof(AnimationTranslationKey)});
    out.push_back({Bytes(&bone->restRotation_), sizeof(Quaternion) + sizeof(Vector3)});   // and restPosition_
    out.push_back({Bytes(&bone->position_), sizeof(Vector3)});
    for (int i = 0; i < bone->children_.size; ++i) Collect(bone->children_.data[i], frame, out);
}

std::vector<std::uint8_t> Save(const std::vector<Field>& fields) {
    std::vector<std::uint8_t> bytes;
    for (const Field& f : fields) bytes.insert(bytes.end(), f.at, f.at + f.size);
    return bytes;
}
void Restore(const std::vector<Field>& fields, const std::vector<std::uint8_t>& bytes) {
    std::size_t i = 0;
    for (const Field& f : fields) {
        std::memcpy(f.at, bytes.data() + i, f.size);
        i += f.size;
    }
}

bool g_busy;

// 0044EB60: SUB ESP,0xBC (6).
void(__fastcall* s_frames)(CAnimationNode*, CAnimationNode*, int);
void __fastcall FramesVerify(CAnimationNode* parent, CAnimationNode* bone, int frame) {
    if (g_busy) {
        s_frames(parent, bone, frame);
        return;
    }
    std::vector<Field> fields;
    Collect(bone, frame, fields);
    std::vector<std::uint8_t> before = Save(fields);
    g_busy = true;
    s_frames(parent, bone, frame);
    g_busy = false;
    std::vector<std::uint8_t> expected = Save(fields);
    Restore(fields, before);
    scene::MakeFrameRelative(parent, bone, frame);
    std::vector<std::uint8_t> actual = Save(fields);
    bool ok = expected.size() == actual.size();
    for (std::size_t i = 0; ok && i + 4 <= expected.size(); i += 4) {
        float e, a;
        std::memcpy(&e, expected.data() + i, 4);
        std::memcpy(&a, actual.data() + i, 4);
        if (!ei::verify::NearlyEqual(e, a, 16, 1e-5f)) ok = false;
    }
    ei::verify::Check("MakeFrameRelative", true, ok, "frame %d", frame);
}
} // namespace

EI_VERIFY(0x0044EB60, FramesVerify, 6, s_frames);
