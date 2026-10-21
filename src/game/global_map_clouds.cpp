// The global map's cloud layer (A-S4): see global_map_clouds.h. Integer noise; the alpha ramp
// and the fade are plain float maths (rule 7).
#include "global_map_clouds.h"

#include "random.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "ei/vcall.h"
#include "lib/nan_audit.h"

#include <cmath>
#include <cstring>

namespace {

constexpr int kLayerSide = 32;
constexpr int kLayerCells = kLayerSide * kLayerSide;   // 0x400
constexpr int kSteps = 16;

// The cell to the right of `cell` in its row of 32, wrapping to the row's start.
int RightOf(int cell) { return ((cell + 1) & 0x1F) ? cell + 1 : cell - 0x1F; }

// One octave: layer cells `scale` pixels wide, bilinear weights out of `scale` each way, for the
// pixel (x, y) of the 256-wide frame. Returns the weighted sum (weights total scale * scale).
int Octave(const std::uint8_t* layer, int x, int y, int scale, int shift) {
    const int cell = ((y >> shift) & 0x1F) * kLayerSide + ((x >> shift) & 0x1F);
    const int right = RightOf(cell);
    const int fx = x & (scale - 1);
    const int fy = y & (scale - 1);
    const int below = layer[(cell + kLayerSide) & 0x3FF];
    const int belowRight = layer[(right + kLayerSide) & 0x3FF];
    const int lower = (belowRight * fx + below * (scale - fx)) * fy;
    const int upper = (layer[right & 0x3FF] * fx + layer[cell] * (scale - fx)) * (scale - fy);
    return lower + upper;
}

} // namespace

CCloudNoise* CCloudNoise::Construct(int size) {
    ei::SetVtable(this, BaseVtable());
    size_ = size;
    const auto layerBytes = static_cast<std::size_t>((size >> 3) * (size >> 3) * 4);
    noise_ = static_cast<std::int32_t*>(gamecrt::operator_new(layerBytes));
    layers_ = static_cast<std::uint8_t*>(gamecrt::operator_new(layerBytes));
    const auto frameBytes = static_cast<std::size_t>(size * size);
    for (std::uint8_t*& frame : frames_) frame = static_cast<std::uint8_t*>(gamecrt::operator_new(frameBytes));
    ramp_ = static_cast<std::uint16_t*>(gamecrt::operator_new(0x200));
    phase_ = 0.0f;
    step_ = 0;
    return this;
}
EI_REPLACE_METHOD(0x00499DC0, CCloudNoise::Construct);

void CCloudNoise::DestructBody() {
    ei::SetVtable(this, BaseVtable());
    gamecrt::operator_delete(noise_);
    gamecrt::operator_delete(layers_);
    for (std::uint8_t* frame : frames_) gamecrt::operator_delete(frame);
    gamecrt::operator_delete(ramp_);
}
EI_REPLACE_METHOD(0x00499E90, CCloudNoise::DestructBody);
// The compiler's jump thunks to it (functions.csv "thunk" rows; S16a: the standalone image has no
// code at them): the same replacement, so their calls skip its check.
EI_REPLACE_METHOD(0x0062EDC0, CCloudNoise::DestructBody);

// 00499E30 (base slot 2, RET 4): DestructBody, freed when bit 0 of `flags` is set. Returns this.
CCloudNoise* CCloudNoise::DeletingDestructImpl(unsigned flags) {
    DestructBody();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x00499E30, CCloudNoise::DeletingDestructImpl);

void CCloudNoise::Start() {
    for (int step = 0; step < kSteps; ++step) Generate(frames_[0], step);
    for (int step = 0; step < kSteps; ++step) Generate(frames_[1], step);
}
EI_REPLACE_METHOD(0x00499EE0, CCloudNoise::Start);

void CCloudNoise::Draw(std::uint16_t* pixels, float seconds) {
    phase_ = seconds * speed_ + phase_;
    EI_NAN_AUDIT(phase_);
    while (phase_ >= 1.0f) {   // a NaN phase ends it (the original looped for ever)
        phase_ = phase_ - 1.0f;
        Generate(frames_[2], step_);
        if (step_ == kSteps - 1) {
            std::uint8_t* from = frames_[0];
            frames_[0] = frames_[1];
            frames_[1] = frames_[2];
            frames_[2] = from;
            step_ = 0;
        } else {
            ++step_;
        }
    }
    const int blend = static_cast<int>((static_cast<float>(step_) + phase_) * 0.0625f * 254.0f + 1.0f);
    const int count = size_ * size_;
    const std::uint8_t* from = frames_[0];
    const std::uint8_t* to = frames_[1];
    for (int i = 0; i < count; ++i) pixels[i] = ramp_[(to[i] * blend + from[i] * (256 - blend)) >> 8];
}
EI_REPLACE_METHOD(0x00499F10, CCloudNoise::Draw);

void CCloudNoise::SetRamp(int start, int width, float speed) {
    float base = static_cast<float>(width) * (1.0f / 255.0f);   // FSTP to a float
    rampStart_ = start;
    rampWidth_ = width;
    speed_ = speed;
    for (int i = 0; i < 0x100; ++i) {
        const int exponent = i - start < 0 ? 0 : i - start;
        const double value = 255.0 - std::pow(static_cast<double>(base), static_cast<double>(exponent)) * 255.0;
        const auto alpha = static_cast<std::uint8_t>(static_cast<int>(value));
        ramp_[i] = static_cast<std::uint16_t>(((alpha >> 4) << 12) | 0xFFF);
    }
}
EI_REPLACE_METHOD(0x0049A010, CCloudNoise::SetRamp);

void CCloudNoise::MakeLayerImpl(std::uint8_t* out) {
    IsaacRandom& random = IsaacRandom::Global();
    const std::uint32_t a = random.Next();
    const std::uint32_t b = random.Next();
    const std::uint32_t c = random.Next();
    std::int32_t* n = noise_;
    for (int row = 0; row < kLayerSide; ++row) {
        std::uint32_t seed = static_cast<std::uint32_t>(row);
        for (int x = 0; x < kLayerSide; ++x, seed += 0x39) {
            const std::uint32_t m = (seed << 13) ^ seed;
            *n++ = static_cast<std::int32_t>(((m * m * a + b) * m + c) & 0x1FFFFFFF);
        }
    }
    const std::int32_t* v = noise_;
    for (int i = 0; i < kLayerCells; ++i) {
        auto at = [v, i](int d) { return v[(i + d) & 0x3FF]; };
        const int edges = (at(-0x20) + at(-1) + at(1) + at(0x20)) >> 3;
        const int corners = (at(-0x21) + at(-0x1F) + at(0x1F) + at(0x21)) >> 4;
        out[i] = static_cast<std::uint8_t>((edges + corners + (v[i] >> 2)) >> 21);
    }
}

void CCloudNoise::GenerateImpl(std::uint8_t* out, int step) {
    if (step >= 1 && step <= 4) {
        MakeLayer(layers_ + (step - 1) * kLayerCells);
        return;
    }
    if (step <= 6 || step >= 15) return;
    const int slice = step - 7;
    std::uint8_t* pixel = out + (slice << 13);
    for (int y = slice * 32; y < (slice + 1) * 32; ++y) {
        for (int x = 0; x < 0x100; ++x) {
            int value = Octave(layers_, x, y, 8, 3) - 0x1C00;
            value += Octave(layers_ + 0x400, x, y, 4, 2) * 2;
            const int fine = Octave(layers_ + 0x800, x, y, 2, 1) + layers_[0xC00 + (y & 0x1F) * kLayerSide + (x & 0x1F)] * 2;
            value = (value + fine * 4) >> 6;
            if (value > 0xFF) value = 0xFF;
            *pixel++ = static_cast<std::uint8_t>(value);
        }
    }
}

namespace {
// The virtual slots are registered through shims (rewrite-guide rule 4).
} // namespace
EI_REPLACE_METHOD(0x0049A090, CCloudNoise::MakeLayerImpl);
EI_REPLACE_METHOD(0x0049A220, CCloudNoise::GenerateImpl);
