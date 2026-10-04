// Checks for scene_bone_frames.cpp. The function rewrites the frame of a whole subtree, so: the
// fields it touches in every bone of the subtree are saved, the original runs (its recursive calls
// go to the original too), its results are recorded, the saved fields are put back, ours runs and
// the results are compared within a few ULPs (rule 7: plain float maths, std::acos).
#include "verify.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace scene {
void __fastcall MakeFrameRelative(char* parent, char* bone, int frame);
}

namespace {

struct Field {
    char* at;
    std::size_t size;
};

void Collect(char* bone, int frame, std::vector<Field>& out) {
    char* keys = *reinterpret_cast<char**>(bone + 0x44);
    char* translations = *reinterpret_cast<char**>(bone + 0x58);
    out.push_back({keys + frame * 40, 40});
    if (frame > 0) out.push_back({keys + (frame - 1) * 40, 40});
    out.push_back({translations + frame * 32, 32});
    out.push_back({bone + 0xE4, 0x1C});
    out.push_back({bone + 0x110, 12});
    int children = *reinterpret_cast<int*>(bone + 0x24);
    char** list = *reinterpret_cast<char***>(bone + 0x20);
    for (int i = 0; i < children; ++i) Collect(list[i], frame, out);
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
void(__fastcall* s_frames)(char*, char*, int);
void __fastcall FramesVerify(char* parent, char* bone, int frame) {
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
