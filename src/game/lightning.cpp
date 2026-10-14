// Lightning bolts (lightning.h). Names are provisional.
//
// Floating point follows the original's x87 code: `double` for values the original keeps on the
// FPU stack, Store() where it stores to a float. Compiled with precise semantics so that the
// operation order is kept and NaN compares stay as written.
#include "lightning.h"

#include "core_maths.h"
#include "random.h"

#include "ei/hook.h"
#include "ei/x87.h"

#include <concepts>
#include <cstring>
#include <initializer_list>

#pragma float_control(precise, on)

// --- Forwarders (not rewritten) ----------------------------------------------------------------

__declspec(naked) void __fastcall LightningPointsSetSize(LightningPointArray*, void*, int, int) EI_ORIGINAL(0x00468C50)
__declspec(naked) int CLightning::Draw(void*) EI_ORIGINAL(0x00467AD0)

namespace {

// Rounds an x87 intermediate to float, as the original's FSTP to a float does.
float Store(double v) {
    volatile float r = static_cast<float>(v);
    return r;
}

// A zero-extended dword loaded with FILD qword (exact).
double Unsigned(std::uint32_t value) {
    volatile std::int64_t wide = value;
    return static_cast<double>(wide);
}

IsaacRandom* Rng() { return reinterpret_cast<IsaacRandom*>(0x007A9D40); }

// The noise table: 32 rows of 400 floats (rows 0..15 for the stored keys, 16..31 for the key drawn
// for each sample), read from gfx.res\lightning.dat by LoadLightningNoise (00468A00).
const float* NoiseTable() { return reinterpret_cast<const float*>(0x0079CE78); }
constexpr int kNoiseRowLength = 400;

// Scratch curves written by LightningSegment::Animate (one float per point, at most 400).
float* NoiseSide() { return reinterpret_cast<float*>(0x0079C1F8); }
float* NoiseUp() { return reinterpret_cast<float*>(0x0079C838); }

// Branch sway frequencies: six pairs (side, up) at 00787C8C.
const float* BranchFrequencies() { return reinterpret_cast<const float*>(0x00787C8C); }

// World clock: whole ticks (007AF61C) and the fraction of the current one (007AFCA8).
int GameTicks() { return *reinterpret_cast<const volatile int*>(0x007AF61C); }
float GameTickFraction() { return *reinterpret_cast<const volatile float*>(0x007AFCA8); }

constexpr double kNoiseScale = 0x1.0000000100000p-31;   // 0073C508: about 2 / 2^32
constexpr double kRandomScale = 0x1.0000000100000p-32;  // 0073C468: about 1 / 2^32
constexpr float kSideA = -0.0666f;                       // 0073C514
constexpr float kSideB = 0.013f;                         // 0073C510
constexpr float kOffsetFactor = 0.05f;                   // 0073C52C

float FromBits(std::uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}
// A float argument would convert to an integer first: callers pass raw bits only.
template <std::floating_point T> float FromBits(T) = delete;

// Randomize a key as the original does inline. The up-noise keys of SampleNoise multiply the
// normaliser by the amplitude before the weight; everything else multiplies the weight first.
void RandomizeKey(NoiseKey& key, float amplitude, bool amplitudeFirst) {
    float sum = 0.0f;
    for (int i = 0; i < 2; ++i) {
        key.index[i] = Rng()->Next() & 0xF;
        double v = Unsigned(Rng()->Next()) * kNoiseScale - 1.0;
        key.weight[i] = Store(v);
        sum = Store(v * v + sum);
    }
    double scale = 1.0 / x87::fsqrt(sum);
    for (int i = 0; i < 2; ++i) {
        if (amplitudeFirst)
            key.weight[i] = Store(scale * amplitude * key.weight[i]);
        else
            key.weight[i] = Store(scale * key.weight[i] * amplitude);
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
    for (int j = 0; j < 8; ++j) weights[j] = Store(static_cast<double>(blend[j]) * weights[j]);
    int count = segment->points.size;
    const float* table = NoiseTable();
    for (int p = 0; p < count; ++p) {
        double sum = 0.0;
        for (int j = 0; j < 8; ++j) sum = sum + static_cast<double>(table[rows[j] * kNoiseRowLength + p]) * weights[j];
        out[p] = Store(sum);
    }
}

// The noise amplitude a segment animates with: 1.5 for positive power, 1 for negative, 2 for 0
// (and NaN).
float NoiseAmplitude(float power) {
    if (power > 0.0f) return 1.5f;
    return power < 0.0f ? 1.0f : 2.0f;
}

bool IsNonZero(float f) { return f > 0.0f || f < 0.0f; }   // false for 0 and NaN

} // namespace

// --- NoiseKey / LightningSegment ----------------------------------------------------------------

void NoiseKey::Randomize(float amplitude) { RandomizeKey(*this, amplitude, false); }
EI_REPLACE_METHOD(0x00466190, NoiseKey::Randomize);

void LightningSegment::SampleNoise(float time, float* outSide, float* outUp, float amplitude) {
    double elapsed = static_cast<double>(time) - noiseTime;
    float left = Store(elapsed);
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
            noiseTime = Store(static_cast<double>(noiseTime) + 1.0);
            next = static_cast<double>(left) - 1.0;
            left = Store(next);
        } while (next > 1.0);
    }
    // Uniform B-spline blend of the keys at the fraction `left`.
    double squared = static_cast<double>(left) * left;
    float half = Store(squared * 0.5);
    float blend[8];
    blend[0] = blend[1] = half;
    blend[2] = blend[3] = Store((static_cast<double>(left) + 0.5) - squared);
    blend[4] = blend[5] = Store((0.5 - static_cast<double>(left)) + half);
    blend[6] = blend[7] = 1.0f;
    SampleCurve(this, sideKeys, blend, outSide, amplitude);
    SampleCurve(this, upKeys, blend, outUp, amplitude);
}
EI_REPLACE_METHOD(0x004662C0, LightningSegment::SampleNoise);

void LightningSegment::Animate(float time, float amplitude) {
    int count = points.size;
    float* sideNoise = NoiseSide();
    float* upNoise = NoiseUp();
    SampleNoise(time, sideNoise, upNoise, amplitude);
    float countF = Store(count);
    double sideAt = sideNoise[0];
    float sideSlope = Store((static_cast<double>(sideNoise[count - 1]) - sideNoise[0]) / countF);
    double upAt = upNoise[0];
    float upSlope = Store((static_cast<double>(upNoise[count - 1]) - upNoise[0]) / countF);
    for (int i = 0; i < points.size; ++i) {
        double s = sideNoise[i] - sideAt;   // the curve minus its straight line from the first value
        double u = upNoise[i] - upAt;
        float ux = Store(u * up.x);
        float uy = Store(u * up.y);
        float uz = Store(u * up.z);
        float sx = Store(s * side.x);
        float sy = Store(s * side.y);
        double sz = s * side.z;
        double fi = i;
        float lx = Store(fi * step.x);
        float ly = Store(fi * step.y);
        double lz = fi * step.z;
        float px = Store(static_cast<double>(lx) + start.x);
        float py = Store(static_cast<double>(ly) + start.y);
        double pz = lz + start.z;
        float qx = Store(static_cast<double>(px) + sx);
        float qy = Store(static_cast<double>(py) + sy);
        double qz = sz + pz;
        double rx = static_cast<double>(qx) + ux;
        float ry = Store(static_cast<double>(qy) + uy);
        float rz = Store(qz + uz);
        LightningPoint& point = points.data[i];
        point.x = Store(rx);
        point.y = ry;
        point.z = rz;
        sideAt = sideAt + sideSlope;
        upAt = upAt + upSlope;
    }
}
EI_REPLACE_METHOD(0x00466CE0, LightningSegment::Animate);

void LightningSegment::Init(const Vector3* from, const Vector3* to) {
    std::memcpy(&start, from, sizeof(start));
    step.x = Store(static_cast<double>(to->x) - from->x);
    step.y = Store(static_cast<double>(to->y) - from->y);
    step.z = Store(static_cast<double>(to->z) - from->z);
    float length = Store(x87::fsqrt(LengthSquaredUnrounded<Sum3::XYZ, double>(step)));

    // Point count and the length the displacement axes are scaled to.
    int count;
    float scale = length;
    if (!(length >= 10.0f)) {   // also NaN
        count = 48;             // FISTP(50.0) & ~3
    } else if (length > 15.0f) {
        scale = 15.0f;
        count = 72;             // FISTP(75.0) & ~3
    } else {
        count = x87::fistp(Store(static_cast<double>(length) * 5.0)) & ~3;
        if (count > 400) count = 400;
        else if (count < 4) count = 4;
    }

    side.x = Store(static_cast<double>(step.y) - static_cast<double>(step.z) * kSideA);
    side.y = Store(static_cast<double>(step.z) * kSideB - step.x);
    side.z = Store(static_cast<double>(step.x) * kSideA - static_cast<double>(step.y) * kSideB);
    // up = cross(step, side), through the Vector3 constructor's float arguments.
    float upZ = Store(static_cast<double>(step.y) * side.x - static_cast<double>(step.x) * side.y);
    float upY = Store(static_cast<double>(step.x) * side.z - static_cast<double>(side.x) * step.z);
    float upX = Store(static_cast<double>(side.y) * step.z - static_cast<double>(step.y) * side.z);
    up.x = upX;
    up.y = upY;
    up.z = upZ;

    for (Vector3* axis : {&side, &up}) {
        double norm = x87::fsqrt(LengthSquaredUnrounded<Sum3::XYZ, double>(*axis));
        if (norm > 0.0) {
            double factor = scale / norm;
            axis->x = Store(factor * axis->x);
            axis->y = Store(factor * axis->y);
            axis->z = Store(factor * axis->z);
        } else {
            axis->x = 0.0f;
            axis->y = 0.0f;
            axis->z = 0.0f;
        }
    }

    double n = count;
    step.x = Store(step.x / n);
    step.y = Store(step.y / n);
    step.z = Store(step.z / n);
    LightningPointsSetSize(&points, nullptr, count, -1);
}
EI_REPLACE_METHOD(0x00466820, LightningSegment::Init);

// --- CLightning --------------------------------------------------------------------------------

void CLightning::Reset() {
    power_ = 1.0f;
    amplitude_ = FromBits(0x3DCCCCCD);   // 0.1f
    CEngineObject* texture = texture_;
    texture_ = nullptr;
    if (texture) texture->Release();
    CEngineObject* glow = glowTexture_;
    glowTexture_ = nullptr;
    if (glow) glow->Release();
}
EI_REPLACE_METHOD(0x00468790, CLightning::Reset);

int CLightning::Register() {
    Reset();
    auto* manager = static_cast<CLightningManager*>(owner_);
    manager->newBolts_.AddTail(this);
    ++refCount_;
    return 1;
}
EI_REPLACE_METHOD(0x00466EA0, CLightning::Register);

void CLightning::ExpandBounds(const LightningSegment* segment) {
    for (int i = 0; i < segment->points.size; ++i) {
        const LightningPoint& p = segment->points.data[i];
        // Minimum: the point when it is smaller or unordered; maximum likewise.
        if (!(p.x >= boundsMin_.x)) boundsMin_.x = p.x;
        if (!(p.y >= boundsMin_.y)) boundsMin_.y = p.y;
        if (!(p.z >= boundsMin_.z)) boundsMin_.z = p.z;
        if (!(boundsMax_.x >= p.x)) boundsMax_.x = p.x;
        if (!(boundsMax_.y >= p.y)) boundsMax_.y = p.y;
        if (!(boundsMax_.z >= p.z)) boundsMax_.z = p.z;
    }
}
EI_REPLACE_METHOD(0x00466FD0, CLightning::ExpandBounds);

void CLightning::UpdateBounds() {
    const float big = FromBits(0x7149F2CA);      // 1e30
    const float small = FromBits(0xF149F2CA);    // -1e30
    boundsMin_.x = big;
    boundsMin_.y = big;
    boundsMin_.z = big;
    boundsMax_.x = small;
    boundsMax_.y = small;
    boundsMax_.z = small;
    ExpandBounds(&main_);
    for (int i = 0; i < 6; ++i) ExpandBounds(&branches_[i]);

    double sumX = static_cast<double>(boundsMin_.x) + boundsMax_.x;
    float sumY = Store(static_cast<double>(boundsMin_.y) + boundsMax_.y);
    float sumZ = Store(static_cast<double>(boundsMin_.z) + boundsMax_.z);
    center_.x = Store(sumX * 0.5);
    center_.y = Store(static_cast<double>(sumY) * 0.5);
    center_.z = Store(static_cast<double>(sumZ) * 0.5);
    boundsSpare_.x = 0.0f;
    boundsSpare_.y = 0.0f;
    boundsSpare_.z = 0.0f;
    boundsMin_.x = Store(static_cast<double>(boundsMin_.x) - center_.x);
    boundsMin_.y = Store(static_cast<double>(boundsMin_.y) - center_.y);
    boundsMin_.z = Store(static_cast<double>(boundsMin_.z) - center_.z);
    boundsMax_.x = Store(static_cast<double>(boundsMax_.x) - center_.x);
    boundsMax_.y = Store(static_cast<double>(boundsMax_.y) - center_.y);
    boundsMax_.z = Store(static_cast<double>(boundsMax_.z) - center_.z);
    boundsKind_ = 3;
    radius_ = Store(x87::fsqrt(LengthSquaredUnrounded<Sum3::ZXY, double>(boundsMax_)));
}
EI_REPLACE_METHOD(0x00467150, CLightning::UpdateBounds);

void CLightning::Animate() {
    float sway = amplitude_;
    float time = Store((static_cast<double>(GameTicks()) + GameTickFraction()) / 4.0);
    main_.Animate(time, NoiseAmplitude(power_));
    if (IsNonZero(power_)) {
        // The branches follow the main segment: each starts at its point of the bolt and ends near
        // the bolt's end, swaying on two sines.
        double n = main_.points.size;
        Vector3 length;
        length.x = Store(n * main_.step.x);
        length.y = Store(n * main_.step.y);
        length.z = Store(n * main_.step.z);
        Vector3 end;
        AddVectors(&end, main_.start, length);
        const float* frequency = BranchFrequencies();
        for (int i = 0; i < 6; ++i) {
            LightningSegment& branch = branches_[i];
            double reach = static_cast<double>(sway) * branchPos_[i];
            float sideSway = Store(x87::fsin(static_cast<double>(time) * frequency[2 * i] * 0.25) * reach);
            double upSway = x87::fsin(static_cast<double>(time) * frequency[2 * i + 1] * 0.25) * reach;
            float ux = Store(upSway * main_.up.x);
            float uy = Store(upSway * main_.up.y);
            double uz = upSway * main_.up.z;
            double sx = static_cast<double>(sideSway) * main_.side.x;
            float sy = Store(static_cast<double>(sideSway) * main_.side.y);
            float sz = Store(static_cast<double>(sideSway) * main_.side.z);
            double wx = sx + end.x;
            float wy = Store(static_cast<double>(end.y) + sy);
            float wz = Store(static_cast<double>(end.z) + sz);
            float targetX = Store(wx + ux);
            float targetY = Store(static_cast<double>(wy) + uy);
            float targetZ = Store(static_cast<double>(wz) + uz);

            int index = x87::fistp(Store((1.0 - branchPos_[i]) * main_.points.size));
            int last = main_.points.size - 1;
            if (index >= last) index = last;
            const LightningPoint& origin = main_.points.data[index];
            branch.start.x = origin.x;
            branch.start.y = origin.y;
            branch.start.z = origin.z;
            float inverse = Store(1.0 / branch.points.size);
            double dz = static_cast<double>(targetZ) - branch.start.z;
            double dy = static_cast<double>(targetY) - branch.start.y;
            double dx = static_cast<double>(targetX) - branch.start.x;
            float stepZ = Store(dz * inverse);
            float stepY = Store(dy * inverse);
            float stepX = Store(dx * inverse);
            branch.step.x = stepX;
            branch.step.y = stepY;
            branch.step.z = stepZ;
        }
    }
    for (int i = 0; i < 6; ++i) branches_[i].Animate(time, NoiseAmplitude(power_));
}
EI_REPLACE_METHOD(0x004672E0, CLightning::Animate);

void CLightning::SetEndpoints(const Vector3* from, const Vector3* to) {
    main_.Init(from, to);
    if (IsNonZero(power_)) {
        // Each branch runs from its point of the bolt to the end.
        for (int i = 0; i < 6; ++i) {
            double along = branchPos_[i];
            double rest = 1.0 - along;
            float ax = Store(rest * to->x);
            float ay = Store(rest * to->y);
            float az = Store(rest * to->z);
            float bx = Store(along * from->x);
            float by = Store(along * from->y);
            double bz = along * from->z;
            Vector3 origin;
            origin.x = Store(static_cast<double>(bx) + ax);
            origin.y = Store(static_cast<double>(by) + ay);
            origin.z = Store(bz + az);
            branches_[i].Init(&origin, to);
        }
    } else {
        // Power 0: six more bolts parallel to the main one, shifted by a random offset of up to
        // 5% of its length on each axis.
        for (int i = 0; i < 6; ++i) {
            double dx = static_cast<double>(from->x) - to->x;
            double dy = static_cast<double>(from->y) - to->y;
            double dz = static_cast<double>(from->z) - to->z;
            double reachExt = x87::fsqrt(LengthSquaredUnrounded<Sum3::ZYX>(dx, dy, dz)) * kOffsetFactor;
            float reach = Store(reachExt);
            float low = Store(-reachExt);
            std::uint32_t r1 = Rng()->Next();
            std::uint32_t r2 = Rng()->Next();
            std::uint32_t r3 = Rng()->Next();
            float ox = Store((static_cast<double>(reach) - low) * Unsigned(r3) * kRandomScale + low);
            float oy = Store((static_cast<double>(reach) - low) * Unsigned(r2) * kRandomScale + low);
            double ozExt = (static_cast<double>(reach) - low) * Unsigned(r1) * kRandomScale + low;
            float oz = Store(ozExt);
            Vector3 shiftedTo;
            shiftedTo.z = Store(ozExt + to->z);
            shiftedTo.y = Store(static_cast<double>(oy) + to->y);
            shiftedTo.x = Store(static_cast<double>(ox) + to->x);
            Vector3 shiftedFrom;
            shiftedFrom.z = Store(static_cast<double>(oz) + from->z);
            shiftedFrom.y = Store(static_cast<double>(oy) + from->y);
            shiftedFrom.x = Store(static_cast<double>(ox) + from->x);
            branches_[i].Init(&shiftedFrom, &shiftedTo);
        }
    }
    if (!(power_ >= 0.0f)) Animate();   // negative (or NaN)
    UpdateBounds();
}
EI_REPLACE_METHOD(0x00467E10, CLightning::SetEndpoints);

// --- CLightningManager --------------------------------------------------------------------------

int CLightningManager::DrawAll(void* device) {
    // The manager's object list (the bolts, in registration order).
    ObjectList::Node* node = objectList_.head;
    while (node) {
        ObjectList::Node* current = node;
        node = node->next;
        static_cast<CLightning*>(current->data)->Draw(device);
    }
    for (auto* n = newBolts_.head; n; n = n->next) {
        if (n->data) n->data->Release();
    }
    newBolts_.RemoveAll();
    return 1;
}
EI_REPLACE_METHOD(0x00468940, CLightningManager::DrawAll);
