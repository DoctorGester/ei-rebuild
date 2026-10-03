// Differential checks for the lightning bolts (lightning.cpp).
//
// Most of these functions draw random numbers: the generator's state (IsaacRandom at 007A9D40) is
// saved, the original runs on a scratch copy, the state after it is kept, the saved state is put
// back and ours runs on the real object; the objects, the generator states and any global output
// must then match. Scratch segments get their own point buffers (capacity 400, the most Init ever
// asks for), so the original's SetSize never reallocates them.
#include "verify.h"

#include "../../src/game/lightning.h"
#include "../../src/game/random.h"

#include <cstring>

namespace {

IsaacRandom* Rng() { return reinterpret_cast<IsaacRandom*>(0x007A9D40); }
float* NoiseSide() { return reinterpret_cast<float*>(0x0079C1F8); }
float* NoiseUp() { return reinterpret_cast<float*>(0x0079C838); }
constexpr int kMaxPoints = 400;

struct RandomState {
    IsaacRandom state;
    void Save() { std::memcpy(&state, Rng(), sizeof(state)); }
    void Restore() const { std::memcpy(Rng(), &state, sizeof(state)); }
    bool Same() const { return std::memcmp(&state, Rng(), sizeof(state)) == 0; }
};

// A segment copy with its own point buffer.
struct SegmentScratch {
    alignas(16) LightningPoint points[kMaxPoints];
    LightningSegment segment;

    // False when the segment cannot be copied (more points than the buffer holds).
    bool From(const LightningSegment& source) {
        if (source.points.size < 0 || source.points.size > kMaxPoints) return false;
        std::memcpy(&segment, &source, sizeof(segment));
        int keep = source.points.capacity < kMaxPoints ? source.points.capacity : kMaxPoints;
        if (source.points.data && keep > 0) std::memcpy(points, source.points.data, keep * sizeof(LightningPoint));
        segment.points.data = points;
        segment.points.capacity = kMaxPoints;
        return true;
    }
};

// Same segment, apart from the buffer and its capacity; `withPoints` also compares the points.
bool SameSegment(const LightningSegment& expected, const LightningSegment& actual, bool withPoints) {
    if (std::memcmp(&expected, &actual, offsetof(LightningSegment, points)) != 0) return false;
    if (expected.points.size != actual.points.size || expected.points.growBy != actual.points.growBy) return false;
    if (std::memcmp(&expected.width, &actual.width, sizeof(float)) != 0) return false;
    if (!withPoints || expected.points.size <= 0) return true;
    return std::memcmp(expected.points.data, actual.points.data, expected.points.size * sizeof(LightningPoint)) == 0;
}

// A deep copy of a bolt (all seven segments with their own buffers).
struct LightningScratch {
    alignas(16) LightningPoint points[7][kMaxPoints];
    alignas(4) unsigned char bytes[sizeof(CLightning)];

    CLightning* Object() { return reinterpret_cast<CLightning*>(bytes); }
    static LightningSegment& Segment(CLightning* bolt, int k) { return k == 0 ? bolt->main_ : bolt->branches_[k - 1]; }

    bool From(CLightning* source) {
        for (int k = 0; k < 7; ++k) {
            int size = Segment(source, k).points.size;
            if (size < 0 || size > kMaxPoints) return false;
        }
        std::memcpy(bytes, source, sizeof(bytes));
        for (int k = 0; k < 7; ++k) {
            const LightningSegment& from = Segment(source, k);
            LightningSegment& to = Segment(Object(), k);
            int keep = from.points.capacity < kMaxPoints ? from.points.capacity : kMaxPoints;
            if (from.points.data && keep > 0) std::memcpy(points[k], from.points.data, keep * sizeof(LightningPoint));
            to.points.data = points[k];
            to.points.capacity = kMaxPoints;
        }
        return true;
    }
};

// Same bolt: the fields outside the segments, every segment (points only when `withPoints`).
bool SameLightning(CLightning* expected, CLightning* actual, bool withPoints) {
    if (std::memcmp(expected, actual, offsetof(CLightning, main_)) != 0) return false;
    if (std::memcmp(expected->branchPos_, actual->branchPos_, sizeof(CLightning) - offsetof(CLightning, branchPos_)) != 0)
        return false;
    for (int k = 0; k < 7; ++k) {
        if (!SameSegment(LightningScratch::Segment(expected, k), LightningScratch::Segment(actual, k), withPoints))
            return false;
    }
    return true;
}

struct NoiseCopy {
    float side[kMaxPoints];
    float up[kMaxPoints];
    void Save() {
        std::memcpy(side, NoiseSide(), sizeof(side));
        std::memcpy(up, NoiseUp(), sizeof(up));
    }
    bool Same() const {
        return std::memcmp(side, NoiseSide(), sizeof(side)) == 0 && std::memcmp(up, NoiseUp(), sizeof(up)) == 0;
    }
};

void* s_unused;

// --- NoiseKey / LightningSegment ----------------------------------------------------------------

// 00466190: SUB ESP,0xC (3), PUSH EBP (1), PUSH ESI (1).
void(__fastcall* s_randomize)(NoiseKey*, void*, float);
void __fastcall RandomizeVerify(NoiseKey* self, void*, float amplitude) {
    static RandomState before, after;
    before.Save();
    NoiseKey expected = *self;
    s_randomize(&expected, nullptr, amplitude);
    after.Save();
    before.Restore();
    self->Randomize(amplitude);
    bool same = std::memcmp(&expected, self, sizeof(expected)) == 0 && after.Same();
    ei::verify::Check("NoiseKey::Randomize", true, same, "amplitude %g", amplitude);
}

// 004662C0: SUB ESP,0x84 (6).
void(__fastcall* s_sampleNoise)(LightningSegment*, void*, float, float*, float*, float);
void __fastcall SampleNoiseVerify(LightningSegment* self, void*, float time, float* outSide, float* outUp, float amplitude) {
    int count = self->points.size;
    if (count < 0 || count > kMaxPoints) {
        self->SampleNoise(time, outSide, outUp, amplitude);
        return;
    }
    static RandomState before, after;
    static float side[kMaxPoints], up[kMaxPoints];
    static unsigned char copy[sizeof(LightningSegment)];
    before.Save();
    std::memcpy(copy, self, sizeof(copy));
    auto* expected = reinterpret_cast<LightningSegment*>(copy);
    s_sampleNoise(expected, nullptr, time, side, up, amplitude);
    after.Save();
    before.Restore();
    self->SampleNoise(time, outSide, outUp, amplitude);
    bool same = std::memcmp(copy, self, sizeof(copy)) == 0 && after.Same() &&
                std::memcmp(side, outSide, count * sizeof(float)) == 0 && std::memcmp(up, outUp, count * sizeof(float)) == 0;
    ei::verify::Check("LightningSegment::SampleNoise", true, same, "time %g noise time %g points %d", time,
                      expected->noiseTime, count);
}

// 00466CE0: SUB ESP,0x4C (3), MOV EAX,[ESP+0x54] (4).
void(__fastcall* s_segmentAnimate)(LightningSegment*, void*, float, float);
void __fastcall SegmentAnimateVerify(LightningSegment* self, void*, float time, float amplitude) {
    static SegmentScratch scratch;
    static RandomState before, after;
    static NoiseCopy noise;
    if (!scratch.From(*self)) {
        self->Animate(time, amplitude);
        return;
    }
    before.Save();
    s_segmentAnimate(&scratch.segment, nullptr, time, amplitude);
    after.Save();
    noise.Save();
    before.Restore();
    self->Animate(time, amplitude);
    bool same = SameSegment(scratch.segment, *self, true) && after.Same() && noise.Same();
    ei::verify::Check("LightningSegment::Animate", true, same, "time %g amplitude %g points %d", time, amplitude,
                      self->points.size);
}

// 00466820: PUSH EBP (1), MOV EBP,ESP (2), SUB ESP,0x14 (3). The points are not compared: a grown
// array is left uninitialised.
void(__fastcall* s_segmentInit)(LightningSegment*, void*, const Vector3*, const Vector3*);
void __fastcall SegmentInitVerify(LightningSegment* self, void*, const Vector3* from, const Vector3* to) {
    static SegmentScratch scratch;
    if (!scratch.From(*self)) {
        self->Init(from, to);
        return;
    }
    Vector3 a = *from, b = *to;   // in case they point into the segment
    s_segmentInit(&scratch.segment, nullptr, &a, &b);
    self->Init(from, to);
    ei::verify::Check("LightningSegment::Init", true, SameSegment(scratch.segment, *self, false),
                      "(%g, %g, %g) -> (%g, %g, %g)", a.x, a.y, a.z, b.x, b.y, b.z);
}

// --- CLightning --------------------------------------------------------------------------------

// 00466FD0: PUSH EBP (1), MOV EBP,ESP (2), SUB ESP,0x30 (3). Only the box (+0x38..+0x50) changes.
void(__fastcall* s_expandBounds)(CLightning*, void*, const LightningSegment*);
void __fastcall ExpandBoundsVerify(CLightning* self, void*, const LightningSegment* segment) {
    alignas(4) unsigned char copy[offsetof(CLightning, unknown_50)];
    std::memcpy(copy, self, sizeof(copy));
    s_expandBounds(reinterpret_cast<CLightning*>(copy), nullptr, segment);
    self->ExpandBounds(segment);
    ei::verify::Check("CLightning::ExpandBounds", true, std::memcmp(copy, self, sizeof(copy)) == 0, "points %d",
                      segment->points.size);
}

// 00467150: SUB ESP,0x1C (3), PUSH EBX (1), PUSH EBP (1). Reads the points (shared), writes
// +0x24..+0x5C.
void(__fastcall* s_updateBounds)(CLightning*);
void __fastcall UpdateBoundsVerify(CLightning* self) {
    static unsigned char copy[sizeof(CLightning)];
    std::memcpy(copy, self, sizeof(copy));
    s_updateBounds(reinterpret_cast<CLightning*>(copy));
    self->UpdateBounds();
    ei::verify::Check("CLightning::UpdateBounds", true, std::memcmp(copy, self, sizeof(copy)) == 0, "radius %g",
                      self->radius_);
}

// 004672E0: PUSH EBP (1), MOV EBP,ESP (2), SUB ESP,0x6C (3).
void(__fastcall* s_animate)(CLightning*);
void __fastcall AnimateVerify(CLightning* self) {
    static LightningScratch scratch;
    static RandomState before, after;
    static NoiseCopy noise;
    if (!scratch.From(self)) {
        self->Animate();
        return;
    }
    before.Save();
    s_animate(scratch.Object());
    after.Save();
    noise.Save();
    before.Restore();
    self->Animate();
    bool same = SameLightning(scratch.Object(), self, true) && after.Same() && noise.Same();
    ei::verify::Check("CLightning::Animate", true, same, "power %g", self->power_);
}

// 00467E10: SUB ESP,0x5C (3), PUSH EBX (1), PUSH EBP (1). The points (and the bounds computed
// from them) are compared only when they cannot hold uninitialised elements: the bolt animated
// (negative power) or no real array had to grow past its capacity.
void(__fastcall* s_setEndpoints)(CLightning*, void*, const Vector3*, const Vector3*);
void __fastcall SetEndpointsVerify(CLightning* self, void*, const Vector3* from, const Vector3* to) {
    static LightningScratch scratch;
    static RandomState before, after;
    static NoiseCopy noise;
    if (!scratch.From(self)) {
        self->SetEndpoints(from, to);
        return;
    }
    int capacity[7];
    for (int k = 0; k < 7; ++k) capacity[k] = LightningScratch::Segment(self, k).points.capacity;
    Vector3 a = *from, b = *to;
    before.Save();
    s_setEndpoints(scratch.Object(), nullptr, &a, &b);
    after.Save();
    noise.Save();
    before.Restore();
    self->SetEndpoints(from, to);

    CLightning* expected = scratch.Object();
    bool animated = !(self->power_ >= 0.0f);
    bool grown = false;
    for (int k = 0; k < 7; ++k) grown = grown || LightningScratch::Segment(expected, k).points.size > capacity[k];
    bool same;
    if (animated || !grown) {
        same = SameLightning(expected, self, true) && noise.Same();
    } else {
        same = std::memcmp(expected, self, offsetof(CLightning, boundsKind_)) == 0 &&
               std::memcmp(expected->branchPos_, self->branchPos_,
                           sizeof(CLightning) - offsetof(CLightning, branchPos_)) == 0;
        for (int k = 0; k < 7; ++k)
            same = same && SameSegment(LightningScratch::Segment(expected, k), LightningScratch::Segment(self, k), false);
    }
    same = same && after.Same();
    ei::verify::Check("CLightning::SetEndpoints", true, same, "power %g (%g, %g, %g) -> (%g, %g, %g)%s", self->power_,
                      a.x, a.y, a.z, b.x, b.y, b.z, grown ? " grown" : "");
}

// 00466EA0 (slot 2), ours only: releases textures and adds a reference. Afterwards the bolt is the
// tail of the manager's new-bolt list, the list and the reference count grew by one, and the bolt
// is reset.
int __fastcall RegisterVerify(CLightning* self) {
    auto* manager = static_cast<CLightningManager*>(self->owner_);
    int count = manager->newBolts_.count;
    int refs = self->refCount_;
    int result = self->Register();
    bool same = result == 1 && manager->newBolts_.tail && manager->newBolts_.tail->data == self &&
                manager->newBolts_.tail->next == nullptr && manager->newBolts_.count == count + 1 &&
                self->refCount_ == refs + 1 && !self->texture_ && !self->glowTexture_ && self->power_ == 1.0f;
    ei::verify::Check("CLightning::Register", true, same, "count %d", count);
    return result;
}

// 00468790 (slot 3), ours only: textures released and cleared, power 1, amplitude 0.1.
void __fastcall ResetVerify(CLightning* self) {
    self->Reset();
    std::uint32_t amplitude;
    std::memcpy(&amplitude, &self->amplitude_, sizeof(amplitude));
    bool same = !self->texture_ && !self->glowTexture_ && self->power_ == 1.0f && amplitude == 0x3DCCCCCD;
    ei::verify::Check("CLightning::Reset", true, same, "");
}

// 00468940, ours only: draws through the original 00467AD0; afterwards the new-bolt list is empty.
int __fastcall DrawAllVerify(CLightningManager* self, void*, void* device) {
    int result = self->DrawAll(device);
    const auto& list = self->newBolts_;
    bool same = result == 1 && !list.head && !list.tail && list.count == 0 && !list.free && !list.blocks;
    ei::verify::Check("CLightningManager::DrawAll", true, same, "");
    return result;
}

} // namespace

EI_VERIFY(0x00466190, RandomizeVerify, 5, s_randomize);
EI_VERIFY(0x004662C0, SampleNoiseVerify, 6, s_sampleNoise);
EI_VERIFY(0x00466CE0, SegmentAnimateVerify, 7, s_segmentAnimate);
EI_VERIFY(0x00466820, SegmentInitVerify, 6, s_segmentInit);
EI_VERIFY(0x00466FD0, ExpandBoundsVerify, 6, s_expandBounds);
EI_VERIFY(0x00467150, UpdateBoundsVerify, 5, s_updateBounds);
EI_VERIFY(0x004672E0, AnimateVerify, 6, s_animate);
EI_VERIFY(0x00467E10, SetEndpointsVerify, 5, s_setEndpoints);
EI_VERIFY(0x00466EA0, RegisterVerify, 0, s_unused);
EI_VERIFY(0x00468790, ResetVerify, 0, s_unused);
EI_VERIFY(0x00468940, DrawAllVerify, 0, s_unused);
