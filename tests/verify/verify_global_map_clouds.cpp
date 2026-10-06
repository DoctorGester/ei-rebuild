// Checks for the global map's cloud layer (global_map_clouds.cpp). Scenarios: ui_camp,
// frames_basic (`screen 4` opens the global map: constructed, ramp set, started, drawn every frame).
#include "verify.h"

#include "../../src/game/global_map_clouds.h"
#include "../../src/game/random.h"

#include <cstring>
#include <vector>

namespace {

// The generator's whole state, to replay the same draws.
struct IsaacSnapshot {
    std::vector<unsigned char> bytes;
    IsaacSnapshot() : bytes(sizeof(IsaacRandom)) { std::memcpy(bytes.data(), &IsaacRandom::Global(), bytes.size()); }
    void Restore() const { std::memcpy(&IsaacRandom::Global(), bytes.data(), bytes.size()); }
    bool Same() const { return std::memcmp(bytes.data(), &IsaacRandom::Global(), bytes.size()) == 0; }
};

} // namespace

// 0049A010: FILD [ESP+8]; MOV EAX,[ESP+8] (8 bytes). The original first; the fields and the ramp
// it made kept aside and the ramp put back, then ours; compared.
static void (__fastcall* s_setRamp)(CCloudNoise*, void*, int, int, float);
static void __fastcall SetRampVerify(CCloudNoise* self, void*, int start, int width, float speed) {
    std::uint16_t before[0x100];
    std::memcpy(before, self->ramp_, sizeof(before));
    s_setRamp(self, nullptr, start, width, speed);
    std::uint16_t expected[0x100];
    std::memcpy(expected, self->ramp_, sizeof(expected));
    std::memcpy(self->ramp_, before, sizeof(before));
    self->SetRamp(start, width, speed);
    int first = -1;
    for (int i = 0; i < 0x100 && first < 0; ++i)
        if (expected[i] != self->ramp_[i]) first = i;
    ei::verify::Check("CloudNoise::SetRamp", -1, first, "first differing entry %d (start %d, width %d)", first, start, width);
}
EI_VERIFY(0x0049A010, SetRampVerify, 8, s_setRamp);

// 0049A090: SUB ESP,0x10; MOV EAX,[007A9D44] (8 bytes). The generator saved; the original into a
// scratch layer (the raw noise kept aside); the generator put back; ours into the real layer;
// layer, raw noise and generator compared.
static void (__fastcall* s_makeLayer)(CCloudNoise*, void*, std::uint8_t*);
static void __fastcall MakeLayerVerify(CCloudNoise* self, void*, std::uint8_t* out) {
    const int cells = (self->size_ >> 3) * (self->size_ >> 3);
    IsaacSnapshot before;
    std::vector<std::uint8_t> expected(0x400);
    s_makeLayer(self, nullptr, expected.data());
    std::vector<std::int32_t> expectedNoise(self->noise_, self->noise_ + cells);
    IsaacSnapshot after;
    before.Restore();
    self->CCloudNoise::MakeLayer(out);
    ei::verify::Check("CloudNoise::MakeLayer", 0, std::memcmp(expected.data(), out, 0x400), "%d");
    ei::verify::Check("CloudNoise::MakeLayer noise", 0,
                      std::memcmp(expectedNoise.data(), self->noise_, cells * sizeof(std::int32_t)), "%d");
    ei::verify::Check("CloudNoise::MakeLayer random", true, after.Same(), "generator state");
}
EI_VERIFY(0x0049A090, MakeLayerVerify, 8, s_makeLayer);

// 0049A220: SUB ESP,0x34; MOV EDX,[ESP+0x3C] (7 bytes). Steps 7-14 (pure) against the original
// into a scratch frame; steps 1-4 (layers, checked by MakeLayer's wrapper) and the rest ours only.
static void (__fastcall* s_generate)(CCloudNoise*, void*, std::uint8_t*, int);
static void __fastcall GenerateVerify(CCloudNoise* self, void*, std::uint8_t* out, int step) {
    if (step < 7 || step > 14 || self->size_ != 0x100) {
        self->CCloudNoise::Generate(out, step);
        return;
    }
    std::vector<std::uint8_t> expected(0x10000);
    s_generate(self, nullptr, expected.data(), step);
    self->CCloudNoise::Generate(out, step);
    const int offset = (step - 7) << 13;
    ei::verify::Check("CloudNoise::Generate", 0, std::memcmp(expected.data() + offset, out + offset, 0x2000),
                      "step %d", step);
}
EI_VERIFY(0x0049A220, GenerateVerify, 7, s_generate);

// The rest ours only.
// 00499DC0: the buffers allocated, the fade at its start.
static CCloudNoise* (__fastcall* s_construct)(CCloudNoise*, void*, int);
static CCloudNoise* __fastcall ConstructVerify(CCloudNoise* self, void*, int size) {
    CCloudNoise* r = self->Construct(size);
    bool ok = r == self && self->size_ == size && self->ramp_ && self->noise_ && self->layers_ && self->frames_[0] &&
              self->frames_[1] && self->frames_[2] && self->phase_ == 0.0f && self->step_ == 0;
    ei::verify::Check("CloudNoise::Construct", true, ok, "size %d", size);
    return r;
}
EI_VERIFY(0x00499DC0, ConstructVerify, 0, s_construct);

static void (__fastcall* s_destructBody)(CCloudNoise*);
static void __fastcall DestructBodyVerify(CCloudNoise* self) {
    self->DestructBody();
    ei::verify::Check("CloudNoise::DestructBody", CCloudNoise::BaseVtable(),
                      *reinterpret_cast<std::uintptr_t*>(self), "%08X");
}
EI_VERIFY(0x00499E90, DestructBodyVerify, 0, s_destructBody);

static CCloudNoise* (__fastcall* s_deletingDestruct)(CCloudNoise*, void*, unsigned);
static CCloudNoise* __fastcall DeletingDestructVerify(CCloudNoise* self, void*, unsigned flags) {
    return CloudNoiseBaseDeletingDestruct(self, nullptr, flags);
}
EI_VERIFY(0x00499E30, DeletingDestructVerify, 0, s_deletingDestruct);

// 00499EE0 / 00499F10: the step stays in 0..15 and the fade below 1.
static void (__fastcall* s_start)(CCloudNoise*);
static void __fastcall StartVerify(CCloudNoise* self) {
    self->Start();
    ei::verify::Check("CloudNoise::Start step", true, self->step_ >= 0 && self->step_ < 16, "%d", self->step_);
}
EI_VERIFY(0x00499EE0, StartVerify, 0, s_start);

static void (__fastcall* s_draw)(CCloudNoise*, void*, std::uint16_t*, float);
static void __fastcall DrawVerify(CCloudNoise* self, void*, std::uint16_t* pixels, float seconds) {
    self->Draw(pixels, seconds);
    ei::verify::Check("CloudNoise::Draw", true, self->step_ >= 0 && self->step_ < 16 && self->phase_ < 1.0f,
                      "step %d", self->step_);
}
EI_VERIFY(0x00499F10, DrawVerify, 0, s_draw);
