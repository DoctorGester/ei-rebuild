// The bones of a fake unit's skeleton (004492F0, called for the scene's root by 00449240 and
// recursively for every child): a bone (0x128 bytes, constructed by the caller with 00506710)
// takes a scene node's name, position keys (bone +0x44), rotation keys with the step to the next
// key (+0x58), a normals-sized array (+0x6C), the two vectors at +0xE4 / +0xF4, and, for nodes
// with morph keys, one morph frame per frame of the animation (+0x84) whose points are the morph
// keys interpolated linearly in between; then a child bone per child node. A node with a pivot
// other than (0, 0, 0) is a fatal error. Names provisional; see docs/notes/objects.md ("Fifth
// round").
#include "scene_loader.h"

#include "../lib/containers.h"
#include "../lib/crt.h"
#include "../lib/mfc.h"
#include "ei/hook.h"
#include "originals_library.h"
#include "originals_render.h"

#include <cstdint>
#include <cstring>

namespace scene {
namespace {

template <typename T>
T& At(const void* base, std::size_t offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(const_cast<void*>(base)) + offset);
}

// --- Layouts -------------------------------------------------------------------------------------

// The bone's arrays: engine aligned arrays (lib::AlignedArray; the inlined SetSize allocates
// through 00452DA0 / 00452D80 / 00452C20 for 32 / 40 / 12-byte elements) and two CArrays
// (mfc::CArray): the normals (12 bytes, zeroed when added) and the morph frames (constructed by
// 00452E20, destroyed by 00452DF0). The keys are filled byte-wise.
template <std::size_t K>
struct KeyBytes {
    char bytes[K];
};
struct Point3 {
    float v[3];
};
using PointArray = lib::AlignedArray<Point3>;
using PositionKeys = lib::AlignedArray<KeyBytes<0x28>>;
using RotationKeys = lib::AlignedArray<KeyBytes<0x20>>;
using BoneChildren = lib::AlignedArray<char*>;
using NormalArray = mfc::CArray<Point3>;
using MorphFrame = KeyBytes<0x18>;
struct MorphFrameElements {
    static void Construct(MorphFrame* frames, int count);
    static void Destruct(MorphFrame* frames, int count);
};
using MorphFrames = mfc::CArray<MorphFrame, MorphFrameElements>;

// The bone (0x128 bytes).
constexpr std::size_t kBoneName = 0x04;        // CString
constexpr std::size_t kBoneParent = 0x1C;
constexpr std::size_t kBoneChildren = 0x20;    // aligned array of bone pointers
constexpr std::size_t kBoneFrames = 0x40;      // the frame count given by the caller (a short)
constexpr std::size_t kBonePositions = 0x44;   // aligned array, 40 bytes each
constexpr std::size_t kBoneRotations = 0x58;   // aligned array, 32 bytes each
constexpr std::size_t kBoneNormals = 0x6C;     // CArray, 12 bytes each (zeroed)
constexpr std::size_t kBoneMorph = 0x84;       // CArray of MorphFrame
constexpr std::size_t kBonePointsB = 0xD4;     // aligned array of 12-byte points, zeroed
constexpr std::size_t kBoneVectorA = 0xE4;     // 4 dwords from node +0x98
constexpr std::size_t kBoneVectorB = 0xF4;     // 3 dwords from node +0x8C
constexpr std::size_t kBonePointsA = 0x100;    // aligned array of 12-byte points, zeroed

// A morph frame (24 bytes): its frame, the next one, and the points.
constexpr std::size_t kMorphFrameSize = 0x18;
constexpr std::size_t kMorphFramePoints = 0x08;   // aligned array of 12-byte points

// The scene node (see scene_loader.h; data / size of each CArray after its vtable).
constexpr std::size_t kNodeRotations = node::kRotKeys + 4;     // 0x14 each: u16 frame, 3 floats at +8
constexpr std::size_t kNodePositions = node::kPosKeys + 4;     // 0x18 each: u16 frame, +8 dword, 3 floats at +0xC
constexpr std::size_t kNodeMorph = node::kMorphKeys + 4;       // 0x14 each: u16 frame, point array at +4
constexpr std::size_t kNodeNormals = node::kNormals + 4;
constexpr std::size_t kNodeChildren = 0x10C + 4;               // node pointers

// --- Forwarders ----------------------------------------------------------------------------------

// 00452C40: SetAtGrow on the skeleton's bone list [this +4; index, &value].
__declspec(naked) void __fastcall BoneListSetAtGrow(void*, void*, int, char* const*) EI_ORIGINAL(0x00452C40)
// Element helpers [pointer, count]: 12-byte zeroing 00452DC0, morph frame constructor 00452E20
// and destructor 00452DF0.
__declspec(naked) void __stdcall ConstructMorphFrames(void*, int) EI_ORIGINAL(0x00452E20)
__declspec(naked) void __stdcall DestroyMorphFrames(void*, int) EI_ORIGINAL(0x00452DF0)
// Out-of-line SetSize [size, growth or -1]: the morph frames 0044F7E0, a frame's points 0044F1D0,
// the children 0044F540; a frame's points copied from a point array 0044F410.
__declspec(naked) void __fastcall MorphFramesSetSize(MorphFrames*, void*, int, int) EI_ORIGINAL(0x0044F7E0)
__declspec(naked) void __fastcall PointsSetSize(PointArray*, void*, int, int) EI_ORIGINAL(0x0044F1D0)
__declspec(naked) void __fastcall PointsCopy(PointArray*, void*, const PointArray*) EI_ORIGINAL(0x0044F410)
__declspec(naked) void __fastcall ChildrenSetSize(BoneChildren*, void*, int, int) EI_ORIGINAL(0x0044F540)
// Vector helpers, __fastcall out in ECX: b - a [00448A10 scales instead: v * t, t as float bits],
// a + b [0040CDC0]. Each component is one rounded operation.
__declspec(naked) float* __fastcall VectorSubtract(float*, const float* /*b*/, const float* /*a*/) EI_ORIGINAL(0x0044A230)
__declspec(naked) float* __fastcall VectorScale(float*, const float*, std::uint32_t /*t*/) EI_ORIGINAL(0x00448A10)
__declspec(naked) float* __fastcall VectorAdd(float*, const float*, const float*) EI_ORIGINAL(0x0040CDC0)
// 00506710: the bone constructor.
// Through its own address, so the verify wrapper sees the children too.
__declspec(naked) void __fastcall BuildBoneAt(char*, char*, const char*, int) EI_ORIGINAL(0x004492F0)

// --- Arrays --------------------------------------------------------------------------------------

void MorphFrameElements::Construct(MorphFrame* frames, int count) { ConstructMorphFrames(frames, count); }
void MorphFrameElements::Destruct(MorphFrame* frames, int count) { DestroyMorphFrames(frames, count); }

// --- x87 -----------------------------------------------------------------------------------------

// A float result of one operation rounded as the original's FSTP does.
float Store(float v) {
    volatile float r = v;
    return r;
}

// (a - b) / (c - b) as FILD (a - b), FIDIV (c - b), FSTP: one rounded division of exact integers.
__declspec(naked) void __fastcall FrameRatio(int /*numerator*/, int /*denominator*/, float* /*out*/) {
    __asm {
        push ecx
        push edx
        fild dword ptr [esp + 4]
        fidiv dword ptr [esp]
        mov eax, dword ptr [esp + 0Ch]
        fstp dword ptr [eax]
        add esp, 8
        ret 4
    }
}

// FCOMP with 0.0 (0073B858) and C3: equal, or unordered.
bool ZeroOrNaN(float v) {
    std::uint32_t bits;
    std::memcpy(&bits, &v, 4);
    return (bits & 0x7FFFFFFFu) == 0 || ((bits & 0x7F800000u) == 0x7F800000u && (bits & 0x007FFFFFu) != 0);
}

// --- The steps -----------------------------------------------------------------------------------

const char* NodeRecord(const char* node, std::size_t array, int index, std::size_t size) {
    return At<const char*>(node, array) + index * size;
}
int NodeCount(const char* node, std::size_t array) { return At<int>(node, array + 4); }

// The position keys (node 0x18 records) into the bone's 40-byte keys: frame, frame + 1, the dword
// at +8 (to +0x24) and the three floats at +0xC (to +0x18).
void CopyPositions(char* bone, const char* node) {
    for (int i = 0; i < NodeCount(node, kNodePositions); ++i) {
        const char* src = NodeRecord(node, kNodePositions, i, 0x18);
        char* dst = At<PositionKeys>(bone, kBonePositions).data[i].bytes;
        int frame = At<short>(src, 0);
        At<int>(dst, 0) = frame;
        At<int>(dst, 4) = frame + 1;
        std::memcpy(dst + 0x18, src + 0xC, 12);
        At<std::uint32_t>(dst, 0x24) = At<std::uint32_t>(src, 8);
    }
}

// The rotation keys (node 0x14 records) into the bone's 32-byte keys: frame, frame + 1, the three
// floats at +8; each key's +0x14 is the step from it to the next key (written when the next one is
// copied); the first key starts with the vector at 0079B9B0.
void CopyRotations(char* bone, const char* node) {
    for (int i = 0; i < NodeCount(node, kNodeRotations); ++i) {
        const char* src = NodeRecord(node, kNodeRotations, i, 0x14);
        char* dst = At<RotationKeys>(bone, kBoneRotations).data[i].bytes;
        int frame = At<short>(src, 0);
        At<int>(dst, 0) = frame;
        At<int>(dst, 4) = frame + 1;
        std::memcpy(dst + 8, src + 8, 12);
        if (i == 0) {
            std::memcpy(dst + 0x14, reinterpret_cast<const void*>(0x0079B9B0), 12);
        } else {
            const float* cur = &At<float>(dst, 8);
            const float* prev = &At<float>(dst - 0x20, 8);
            float step[3] = {Store(cur[0] - prev[0]), Store(cur[1] - prev[1]), Store(cur[2] - prev[2])};
            std::memcpy(dst - 0x20 + 0x14, step, 12);
        }
    }
}

// Morph frame `frame` as the key's points moved towards the next key's by t (from the previous key
// when `fromPrevious`): a + (b - a) * t, each operation rounded; the very first key is copied.
const PointArray& KeyPoints(const char* node, int key) {
    return At<PointArray>(NodeRecord(node, kNodeMorph, key, 0x14), 4);
}

// One morph frame per frame (the bone's frame count): before each key the points between the
// previous key and it (or the key itself before the first), at a key the points from it towards
// the next one (or a copy of the last key).
void BuildMorphFrames(char* bone, const char* node) {
    MorphFrames& frames = At<MorphFrames>(bone, kBoneMorph);
    int key = 0;
    int previousFrame = 0;
    float t = 0.0f;
    for (int frame = 0, offset = 0; frame < frames.size; offset += kMorphFrameSize) {
        int keyFrame = At<short>(NodeRecord(node, kNodeMorph, key, 0x14), 0);
        if (frame < keyFrame) {
            FrameRatio(frame - previousFrame, keyFrame - previousFrame, &t);
            char* out = reinterpret_cast<char*>(frames.data) + offset;
            At<int>(out, 0) = frame;
            ++frame;
            At<int>(out, 4) = frame;
            PointsSetSize(&At<PointArray>(out, kMorphFramePoints), nullptr, KeyPoints(node, 0).size, -1);
            for (int j = 0; j < At<PointArray>(reinterpret_cast<char*>(frames.data) + offset, kMorphFramePoints).size; ++j) {
                float* p = At<PointArray>(reinterpret_cast<char*>(frames.data) + offset, kMorphFramePoints).data[j].v;
                if (key <= 0) {
                    std::memcpy(p, KeyPoints(node, key).data + j * 12, 12);
                    continue;
                }
                const float* a = reinterpret_cast<const float*>(KeyPoints(node, key - 1).data) + j * 3;
                const float* b = reinterpret_cast<const float*>(KeyPoints(node, key).data) + j * 3;
                float d[3] = {Store(b[0] - a[0]), Store(b[1] - a[1]), Store(b[2] - a[2])};
                float s[3] = {Store(d[0] * t), Store(d[1] * t), Store(d[2] * t)};
                float r[3] = {Store(s[0] + a[0]), Store(s[1] + a[1]), Store(s[2] + a[2])};
                std::memcpy(p, r, 12);
            }
            continue;
        }

        ++key;
        previousFrame = keyFrame;
        float toNext = 0.0f;
        if (key < NodeCount(node, kNodeMorph)) {
            FrameRatio(frame - keyFrame, At<short>(NodeRecord(node, kNodeMorph, key, 0x14), 0) - keyFrame, &toNext);
        }
        char* out = reinterpret_cast<char*>(frames.data) + offset;
        At<int>(out, 0) = frame;
        ++frame;
        At<int>(out, 4) = frame;
        PointsSetSize(&At<PointArray>(out, kMorphFramePoints), nullptr, KeyPoints(node, 0).size, -1);
        if (key >= NodeCount(node, kNodeMorph)) {
            PointsCopy(&At<PointArray>(reinterpret_cast<char*>(frames.data) + offset, kMorphFramePoints), nullptr, &KeyPoints(node, key - 1));
            continue;
        }
        std::uint32_t tBits;
        std::memcpy(&tBits, &toNext, 4);
        for (int j = 0; j < At<PointArray>(reinterpret_cast<char*>(frames.data) + offset, kMorphFramePoints).size; ++j) {
            float* p = At<PointArray>(reinterpret_cast<char*>(frames.data) + offset, kMorphFramePoints).data[j].v;
            const float* a = reinterpret_cast<const float*>(KeyPoints(node, key - 1).data) + j * 3;
            const float* b = reinterpret_cast<const float*>(KeyPoints(node, key).data) + j * 3;
            float d[3], s[3], r[3];
            VectorSubtract(d, b, a);
            VectorScale(s, d, tBits);
            const float* sum = VectorAdd(r, a, s);
            std::memcpy(p, sum, 12);
        }
    }
}

} // namespace

// 004492F0 (__fastcall: the skeleton in ECX, the new bone in EDX; RET 8). `node` is the scene node
// (0x120 bytes), the low word of `frames` the animation's frame count.
void __fastcall BuildBone(char* skeleton, char* bone, const char* node, int frames) {
    char* added = bone;
    BoneListSetAtGrow(skeleton + 4, nullptr, At<int>(skeleton, 8), &added);
    bone = added;
    At<mfc::CString>(bone, kBoneName) = At<mfc::CString>(node, node::kName);
    At<int>(bone, kBoneFrames) = static_cast<short>(frames);

    At<RotationKeys>(bone, kBoneRotations).SetSize(NodeCount(node, kNodeRotations));
    At<PositionKeys>(bone, kBonePositions).SetSize(NodeCount(node, kNodePositions));
    At<NormalArray>(bone, kBoneNormals).SetSize(NodeCount(node, kNodeNormals));
    int morphKeys = NodeCount(node, kNodeMorph);
    At<MorphFrames>(bone, kBoneMorph).SetSize(morphKeys != 0 ? morphKeys + 1 : 0);
    std::memcpy(bone + kBoneVectorA, node + 0x98, 16);
    std::memcpy(bone + kBoneVectorB, node + 0x8C, 12);
    if (NodeCount(node, kNodeMorph) != 0) {
        PointArray& a = At<PointArray>(bone, kBonePointsA);
        a.SetSize(KeyPoints(node, 0).size);
        std::memset(a.data, 0, static_cast<std::size_t>(a.size) * 12);
        PointArray& b = At<PointArray>(bone, kBonePointsB);
        b.SetSize(KeyPoints(node, 0).size);
        std::memset(b.data, 0, static_cast<std::size_t>(b.size) * 12);
    }

    CopyPositions(bone, node);
    CopyRotations(bone, node);
    if (NodeCount(node, kNodeMorph) == 0) {
        MorphFramesSetSize(&At<MorphFrames>(bone, kBoneMorph), nullptr, 0, -1);
    } else {
        At<MorphFrames>(bone, kBoneMorph).SetSize(NodeCount(node, kNodePositions));
        if (At<MorphFrames>(bone, kBoneMorph).size > 0) BuildMorphFrames(bone, node);
    }

    ChildrenSetSize(&At<BoneChildren>(bone, kBoneChildren), nullptr, NodeCount(node, kNodeChildren), -1);
    for (int i = 0; i < At<BoneChildren>(bone, kBoneChildren).size; ++i) {
        void* block = gamecrt::operator_new(0x128);
        char* child = block ? ConstructBone(block) : nullptr;
        At<BoneChildren>(bone, kBoneChildren).data[i] = child;
        At<char*>(At<BoneChildren>(bone, kBoneChildren).data[i], kBoneParent) = bone;
        BuildBoneAt(skeleton, At<BoneChildren>(bone, kBoneChildren).data[i],
                    At<const char* const*>(node, kNodeChildren)[i], frames);
    }

    const float* pivot = &At<float>(node, node::kOffset);
    if (!ZeroOrNaN(pivot[0]) || !ZeroOrNaN(pivot[1]) || !ZeroOrNaN(pivot[2])) {
        // "node %s have NONULL pivot (x=%g, y=%g, z=%g)"
        game::FatalError(reinterpret_cast<const char*>(0x0078441C), static_cast<const char*>(At<mfc::CString>(bone, kBoneName)),
                         static_cast<double>(pivot[0]), static_cast<double>(pivot[1]), static_cast<double>(pivot[2]));
    }
}
EI_REPLACE(0x004492F0, BuildBone);

} // namespace scene
