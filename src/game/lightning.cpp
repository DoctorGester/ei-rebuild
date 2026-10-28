// Lightning bolts. Floating point follows the original's x87 code: `double` for values it keeps on the FPU
// stack, static_cast<float> where it stores to a float.
#include "lightning.h"

#include "core_maths.h"
#include "random.h"
#include "texture.h"

#include "ei/x87.h"
#include "game_data.h"
#include "game_singletons.h"
#include "lib/nan_audit.h"

#include <concepts>
#include <cstring>
#include <initializer_list>

// --- Helpers -----------------------------------------------------------------------------------

namespace {

double Unsigned(std::uint32_t value) {
    volatile std::int64_t wide = value;
    return static_cast<double>(wide);
}

// The noise table: 32 rows of 400 floats (rows 0..15 for the stored keys, 16..31 for the key drawn
// for each sample), read from gfx.res\lightning.dat by LoadLightningNoise.
const float* NoiseTable() { return LightningNoiseTable(); }
constexpr int kNoiseRowLength = 400;

// Scratch curves written by LightningSegment::Animate (one float per point, at most 400).
float g_noiseSide[400] = /*0079C1F8*/ {};
float g_noiseUp[400] = /*0079C838*/ {};

constexpr float kBranchFrequencies[12] = /*00787C8C*/ {2.0f, 3.0f, 2.5f, 1.8f, 0.3f, 0.8f, 1.2f, 1.3f, 3.0f, 5.1f, 2.1f, 1.76f};

constexpr double kNoiseScale = 0x1.0000000100000p-31;   // about 2 / 2^32
constexpr double kRandomScale = 0x1.0000000100000p-32;  // about 1 / 2^32
constexpr float kSideA = -0.0666f;
constexpr float kSideB = 0.013f;
constexpr float kOffsetFactor = 0.05f;

float FromBits(std::uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}
// A float argument would convert to an integer first: callers pass raw bits only.
template <std::floating_point T> float FromBits(T) = delete;

// Randomize a key. The up-noise keys of SampleNoise multiply the
// normaliser by the amplitude before the weight; everything else multiplies the weight first.
void RandomizeKey(NoiseKey& key, float amplitude, bool amplitudeFirst) {
    float sum = 0.0f;
    for (int i = 0; i < 2; ++i) {
        key.index[i] = IsaacRandom::Global().Next() & 0xF;
        double v = Unsigned(IsaacRandom::Global().Next()) * kNoiseScale - 1.0;
        key.weight[i] = static_cast<float>(v);
        sum = static_cast<float>(v * v + sum);
    }
    double scale = 1.0 / std::sqrt(sum);
    for (int i = 0; i < 2; ++i) {
        if (amplitudeFirst)
            key.weight[i] = static_cast<float>(scale * amplitude * key.weight[i]);
        else
            key.weight[i] = static_cast<float>(scale * key.weight[i] * amplitude);
    }
}

// One noise curve: the three stored keys and a fresh one, weighted by the B-spline blend.
void SampleCurve(LightningSegment* segment, const NoiseKey* keys, const float* blend, float* out, float amplitude) {
    NoiseKey fresh;
    RandomizeKey(fresh, amplitude, false);
    std::uint32_t rows[8];
    float weights[8];
    for (int k = 0; k < 3; ++k) {
        rows[2 * k] = keys[k].index[0];
        rows[2 * k + 1] = keys[k].index[1];
        weights[2 * k] = keys[k].weight[0];
        weights[2 * k + 1] = keys[k].weight[1];
    }
    weights[6] = fresh.weight[0];
    weights[7] = fresh.weight[1];
    rows[7] = fresh.index[1] + 16;
    rows[6] = fresh.index[0] + 16;
    for (int j = 0; j < 8; ++j) weights[j] = static_cast<float>(static_cast<double>(blend[j]) * weights[j]);
    int count = std::ssize(segment->points);
    const float* table = NoiseTable();
    for (int p = 0; p < count; ++p) {
        double sum = 0.0;
        for (int j = 0; j < 8; ++j) sum = sum + static_cast<double>(table[rows[j] * kNoiseRowLength + p]) * weights[j];
        out[p] = static_cast<float>(sum);
    }
}

// The noise amplitude a segment animates with: 1.5 for positive power, 1 for negative, 2 for 0 (and NaN).
float NoiseAmplitude(float power) {
    if (power > 0.0f) return 1.5f;
    return power < 0.0f ? 1.0f : 2.0f;
}

} // namespace

// --- NoiseKey / LightningSegment ----------------------------------------------------------------

void NoiseKey::Randomize(float amplitude) { RandomizeKey(*this, amplitude, false); }

void LightningSegment::SampleNoise(float time, float* outSide, float* outUp, float amplitude) {
    double elapsed = static_cast<double>(time) - noiseTime;
    float left = static_cast<float>(elapsed);
    bool advance;
    if (elapsed > 100.0) {
        left = 100.0f;
        advance = true;
    } else {
        advance = left > 1.0f;
    }
    if (advance) {
        double next;
        do {
            sideKeys[2] = sideKeys[1];
            sideKeys[1] = sideKeys[0];
            RandomizeKey(sideKeys[0], amplitude, false);
            upKeys[2] = upKeys[1];
            upKeys[1] = upKeys[0];
            RandomizeKey(upKeys[0], amplitude, true);
            noiseTime = static_cast<float>(static_cast<double>(noiseTime) + 1.0);
            next = static_cast<double>(left) - 1.0;
            left = static_cast<float>(next);
        } while (next > 1.0);
    }
    // Uniform B-spline blend of the keys at the fraction `left`.
    double squared = static_cast<double>(left) * left;
    float half = static_cast<float>(squared * 0.5);
    float blend[8];
    blend[0] = blend[1] = half;
    blend[2] = blend[3] = static_cast<float>((static_cast<double>(left) + 0.5) - squared);
    blend[4] = blend[5] = static_cast<float>((0.5 - static_cast<double>(left)) + half);
    blend[6] = blend[7] = 1.0f;
    SampleCurve(this, sideKeys, blend, outSide, amplitude);
    SampleCurve(this, upKeys, blend, outUp, amplitude);
}

void LightningSegment::Animate(float time, float amplitude) {
    int count = std::ssize(points);
    float* sideNoise = g_noiseSide;
    float* upNoise = g_noiseUp;
    SampleNoise(time, sideNoise, upNoise, amplitude);
    float countF = static_cast<float>(count);
    double sideAt = sideNoise[0];
    float sideSlope = static_cast<float>((static_cast<double>(sideNoise[count - 1]) - sideNoise[0]) / countF);
    double upAt = upNoise[0];
    float upSlope = static_cast<float>((static_cast<double>(upNoise[count - 1]) - upNoise[0]) / countF);
    for (int i = 0; i < std::ssize(points); ++i) {
        double s = sideNoise[i] - sideAt;   // the curve minus its straight line from the first value
        double u = upNoise[i] - upAt;
        float ux = static_cast<float>(u * up.x);
        float uy = static_cast<float>(u * up.y);
        float uz = static_cast<float>(u * up.z);
        float sx = static_cast<float>(s * side.x);
        float sy = static_cast<float>(s * side.y);
        double sz = s * side.z;
        double fi = i;
        float lx = static_cast<float>(fi * step.x);
        float ly = static_cast<float>(fi * step.y);
        double lz = fi * step.z;
        float px = static_cast<float>(static_cast<double>(lx) + start.x);
        float py = static_cast<float>(static_cast<double>(ly) + start.y);
        double pz = lz + start.z;
        float qx = static_cast<float>(static_cast<double>(px) + sx);
        float qy = static_cast<float>(static_cast<double>(py) + sy);
        double qz = sz + pz;
        double rx = static_cast<double>(qx) + ux;
        float ry = static_cast<float>(static_cast<double>(qy) + uy);
        float rz = static_cast<float>(qz + uz);
        LightningPoint& point = points[i];
        point.x = static_cast<float>(rx);
        point.y = ry;
        point.z = rz;
        sideAt = sideAt + sideSlope;
        upAt = upAt + upSlope;
    }
}

void LightningSegment::Init(const Vector3* from, const Vector3* to) {
    std::memcpy(&start, from, sizeof(start));
    step.x = static_cast<float>(static_cast<double>(to->x) - from->x);
    step.y = static_cast<float>(static_cast<double>(to->y) - from->y);
    step.z = static_cast<float>(static_cast<double>(to->z) - from->z);
    float length = static_cast<float>(std::sqrt(LengthSquaredUnrounded<Sum3::XYZ, double>(step)));

    // Point count and the length the displacement axes are scaled to.
    int count;
    float scale = length;
    EI_NAN_AUDIT(length);
    if (length < 10.0f) {
        count = 48;             // 50.0 rounded, & ~3
    } else if (length > 15.0f) {
        scale = 15.0f;
        count = 72;             // 75.0 rounded, & ~3
    } else {
        count = x87::fistp(static_cast<float>(static_cast<double>(length) * 5.0)) & ~3;
        if (count > 400) count = 400;
        else if (count < 4) count = 4;
    }

    side.x = static_cast<float>(static_cast<double>(step.y) - static_cast<double>(step.z) * kSideA);
    side.y = static_cast<float>(static_cast<double>(step.z) * kSideB - step.x);
    side.z = static_cast<float>(static_cast<double>(step.x) * kSideA - static_cast<double>(step.y) * kSideB);
    // up = cross(step, side), through the Vector3 constructor's float arguments.
    float upZ = static_cast<float>(static_cast<double>(step.y) * side.x - static_cast<double>(step.x) * side.y);
    float upY = static_cast<float>(static_cast<double>(step.x) * side.z - static_cast<double>(side.x) * step.z);
    float upX = static_cast<float>(static_cast<double>(side.y) * step.z - static_cast<double>(step.y) * side.z);
    up.x = upX;
    up.y = upY;
    up.z = upZ;

    for (Vector3* axis : {&side, &up}) {
        double norm = std::sqrt(LengthSquaredUnrounded<Sum3::XYZ, double>(*axis));
        if (norm > 0.0) {
            double factor = scale / norm;
            axis->x = static_cast<float>(factor * axis->x);
            axis->y = static_cast<float>(factor * axis->y);
            axis->z = static_cast<float>(factor * axis->z);
        } else {
            axis->x = 0.0f;
            axis->y = 0.0f;
            axis->z = 0.0f;
        }
    }

    double n = count;
    step.x = static_cast<float>(step.x / n);
    step.y = static_cast<float>(step.y / n);
    step.z = static_cast<float>(step.z / n);
    points.resize(count);
}

// --- CLightning --------------------------------------------------------------------------------

void CLightning::Destroy() {
    power_ = 1.0f;
    amplitude_ = FromBits(0x3DCCCCCD);   // 0.1f
    CTexture* texture = texture_;
    texture_ = nullptr;
    if (texture) texture->Release();
    CTexture* glow = glowTexture_;
    glowTexture_ = nullptr;
    if (glow) glow->Release();
}

int CLightning::Create() {
    Destroy();
    auto* manager = static_cast<CLightningManager*>(owner_);
    manager->newBolts_.push_back(this);
    ++refCount_;
    return 1;
}

void CLightning::ExpandBounds(const LightningSegment* segment) {
    for (int i = 0; i < std::ssize(segment->points); ++i) {
        const LightningPoint& p = segment->points[i];
        // Minimum: the point when it is smaller or unordered; maximum likewise.
        bounds_.boxMin.x = p.x >= bounds_.boxMin.x ? bounds_.boxMin.x : p.x;
        bounds_.boxMin.y = p.y >= bounds_.boxMin.y ? bounds_.boxMin.y : p.y;
        bounds_.boxMin.z = p.z >= bounds_.boxMin.z ? bounds_.boxMin.z : p.z;
        bounds_.boxMax.x = bounds_.boxMax.x >= p.x ? bounds_.boxMax.x : p.x;
        bounds_.boxMax.y = bounds_.boxMax.y >= p.y ? bounds_.boxMax.y : p.y;
        bounds_.boxMax.z = bounds_.boxMax.z >= p.z ? bounds_.boxMax.z : p.z;
    }
}

void CLightning::UpdateBounds() {
    const float largest = FromBits(0x7149F2CA);      // 1e30
    const float lowest = FromBits(0xF149F2CA);    // -1e30
    bounds_.boxMin.x = largest;
    bounds_.boxMin.y = largest;
    bounds_.boxMin.z = largest;
    bounds_.boxMax.x = lowest;
    bounds_.boxMax.y = lowest;
    bounds_.boxMax.z = lowest;
    ExpandBounds(&main_);
    for (int i = 0; i < 6; ++i) ExpandBounds(&branches_[i]);

    double sumX = static_cast<double>(bounds_.boxMin.x) + bounds_.boxMax.x;
    float sumY = static_cast<float>(static_cast<double>(bounds_.boxMin.y) + bounds_.boxMax.y);
    float sumZ = static_cast<float>(static_cast<double>(bounds_.boxMin.z) + bounds_.boxMax.z);
    bounds_.center.x = static_cast<float>(sumX * 0.5);
    bounds_.center.y = static_cast<float>(static_cast<double>(sumY) * 0.5);
    bounds_.center.z = static_cast<float>(static_cast<double>(sumZ) * 0.5);
    boundsSpare_.x = 0.0f;
    boundsSpare_.y = 0.0f;
    boundsSpare_.z = 0.0f;
    bounds_.boxMin.x = static_cast<float>(static_cast<double>(bounds_.boxMin.x) - bounds_.center.x);
    bounds_.boxMin.y = static_cast<float>(static_cast<double>(bounds_.boxMin.y) - bounds_.center.y);
    bounds_.boxMin.z = static_cast<float>(static_cast<double>(bounds_.boxMin.z) - bounds_.center.z);
    bounds_.boxMax.x = static_cast<float>(static_cast<double>(bounds_.boxMax.x) - bounds_.center.x);
    bounds_.boxMax.y = static_cast<float>(static_cast<double>(bounds_.boxMax.y) - bounds_.center.y);
    bounds_.boxMax.z = static_cast<float>(static_cast<double>(bounds_.boxMax.z) - bounds_.center.z);
    bounds_.flags = 3;
    bounds_.radius = static_cast<float>(std::sqrt(LengthSquaredUnrounded<Sum3::ZXY, double>(bounds_.boxMax)));
}

void CLightning::Animate() {
    float sway = amplitude_;
    float time = static_cast<float>((static_cast<double>(CurrentClientTick()) + ClientTickFraction()) / 4.0);
    main_.Animate(time, NoiseAmplitude(power_));
    EI_NAN_AUDIT(power_);
    if (power_ != 0.0f) {
        // The branches follow the main segment: each starts at its point of the bolt and ends near
        // the bolt's end, swaying on two sines.
        double n = std::ssize(main_.points);
        Vector3 length;
        length.x = static_cast<float>(n * main_.step.x);
        length.y = static_cast<float>(n * main_.step.y);
        length.z = static_cast<float>(n * main_.step.z);
        Vector3 end;
        AddVectors(&end, main_.start, length);
        const float* frequency = kBranchFrequencies;
        for (int i = 0; i < 6; ++i) {
            LightningSegment& branch = branches_[i];
            double reach = static_cast<double>(sway) * branchPos_[i];
            float sideSway = static_cast<float>(x87::fsin(static_cast<double>(time) * frequency[2 * i] * 0.25) * reach);
            double upSway = x87::fsin(static_cast<double>(time) * frequency[2 * i + 1] * 0.25) * reach;
            float ux = static_cast<float>(upSway * main_.up.x);
            float uy = static_cast<float>(upSway * main_.up.y);
            double uz = upSway * main_.up.z;
            double sx = static_cast<double>(sideSway) * main_.side.x;
            float sy = static_cast<float>(static_cast<double>(sideSway) * main_.side.y);
            float sz = static_cast<float>(static_cast<double>(sideSway) * main_.side.z);
            double wx = sx + end.x;
            float wy = static_cast<float>(static_cast<double>(end.y) + sy);
            float wz = static_cast<float>(static_cast<double>(end.z) + sz);
            float targetX = static_cast<float>(wx + ux);
            float targetY = static_cast<float>(static_cast<double>(wy) + uy);
            float targetZ = static_cast<float>(static_cast<double>(wz) + uz);

            int index = x87::fistp(static_cast<float>((1.0 - branchPos_[i]) * std::ssize(main_.points)));
            int last = std::ssize(main_.points) - 1;
            if (index >= last) index = last;
            const LightningPoint& origin = main_.points[index];
            branch.start.x = origin.x;
            branch.start.y = origin.y;
            branch.start.z = origin.z;
            float inverse = static_cast<float>(1.0 / std::ssize(branch.points));
            double dz = static_cast<double>(targetZ) - branch.start.z;
            double dy = static_cast<double>(targetY) - branch.start.y;
            double dx = static_cast<double>(targetX) - branch.start.x;
            float stepZ = static_cast<float>(dz * inverse);
            float stepY = static_cast<float>(dy * inverse);
            float stepX = static_cast<float>(dx * inverse);
            branch.step.x = stepX;
            branch.step.y = stepY;
            branch.step.z = stepZ;
        }
    }
    for (int i = 0; i < 6; ++i) branches_[i].Animate(time, NoiseAmplitude(power_));
}

void CLightning::SetEndpoints(const Vector3* from, const Vector3* to) {
    main_.Init(from, to);
    EI_NAN_AUDIT(power_);
    if (power_ != 0.0f) {
        // Each branch runs from its point of the bolt to the end.
        for (int i = 0; i < 6; ++i) {
            double along = branchPos_[i];
            double rest = 1.0 - along;
            float ax = static_cast<float>(rest * to->x);
            float ay = static_cast<float>(rest * to->y);
            float az = static_cast<float>(rest * to->z);
            float bx = static_cast<float>(along * from->x);
            float by = static_cast<float>(along * from->y);
            double bz = along * from->z;
            Vector3 origin;
            origin.x = static_cast<float>(static_cast<double>(bx) + ax);
            origin.y = static_cast<float>(static_cast<double>(by) + ay);
            origin.z = static_cast<float>(bz + az);
            branches_[i].Init(&origin, to);
        }
    } else {
        // Power 0: six more bolts parallel to the main one, shifted by a random offset of up to
        // 5% of its length on each axis.
        for (int i = 0; i < 6; ++i) {
            double dx = static_cast<double>(from->x) - to->x;
            double dy = static_cast<double>(from->y) - to->y;
            double dz = static_cast<double>(from->z) - to->z;
            double reachExt = std::sqrt(LengthSquaredUnrounded<Sum3::ZYX>(dx, dy, dz)) * kOffsetFactor;
            float reach = static_cast<float>(reachExt);
            float low = static_cast<float>(-reachExt);
            std::uint32_t r1 = IsaacRandom::Global().Next();
            std::uint32_t r2 = IsaacRandom::Global().Next();
            std::uint32_t r3 = IsaacRandom::Global().Next();
            float ox = static_cast<float>((static_cast<double>(reach) - low) * Unsigned(r3) * kRandomScale + low);
            float oy = static_cast<float>((static_cast<double>(reach) - low) * Unsigned(r2) * kRandomScale + low);
            double ozExt = (static_cast<double>(reach) - low) * Unsigned(r1) * kRandomScale + low;
            float oz = static_cast<float>(ozExt);
            Vector3 shiftedTo;
            shiftedTo.z = static_cast<float>(ozExt + to->z);
            shiftedTo.y = static_cast<float>(static_cast<double>(oy) + to->y);
            shiftedTo.x = static_cast<float>(static_cast<double>(ox) + to->x);
            Vector3 shiftedFrom;
            shiftedFrom.z = static_cast<float>(static_cast<double>(oz) + from->z);
            shiftedFrom.y = static_cast<float>(static_cast<double>(oy) + from->y);
            shiftedFrom.x = static_cast<float>(static_cast<double>(ox) + from->x);
            branches_[i].Init(&shiftedFrom, &shiftedTo);
        }
    }
    EI_NAN_AUDIT(power_);
    if (power_ < 0.0f) Animate();
    UpdateBounds();
}

// --- CLightningManager --------------------------------------------------------------------------

int CLightningManager::DrawAll(CSceneRenderer* device) {
    // The manager's object list (the bolts, in creation order).
    auto node = objectList_.begin();
    while (node != objectList_.end()) {
        ObjectList::iterator  current = node;
        ++node;
        static_cast<CLightning*>(*current)->Draw(device);
    }
    for (auto n = newBolts_.begin(); n != newBolts_.end(); ++n) {
        if (*n) (*n)->Release();
    }
    newBolts_.clear();
    return 1;
}
