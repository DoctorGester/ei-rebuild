// Lightning bolts (lightning.h). Names are provisional.
//
// Floating point follows the original's x87 code: `double` for values the original keeps on the FPU
// stack, static_cast<float> where it stores to a float.
#include "lightning.h"

#include "core_maths.h"
#include "random.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "game_data.h"
#include "lib/nan_audit.h"

#include <concepts>
#include <cstring>
#include <initializer_list>

// --- Forwarders (not rewritten) ----------------------------------------------------------------


namespace {

// A zero-extended dword loaded with FILD qword (exact).
double Unsigned(std::uint32_t value) {
    volatile std::int64_t wide = value;
    return static_cast<double>(wide);
}

IsaacRandom* Rng() { return ei::GamePointer<IsaacRandom>(0x007A9D40); }

// The noise table: 32 rows of 400 floats (rows 0..15 for the stored keys, 16..31 for the key drawn
// for each sample), read from gfx.res\lightning.dat by LoadLightningNoise (00468A00).
const float* NoiseTable() { return ei::GamePointer<const float>(0x0079CE78); }
constexpr int kNoiseRowLength = 400;

// Scratch curves written by LightningSegment::Animate (one float per point, at most 400).
float* NoiseSide() { return ei::GamePointer<float>(0x0079C1F8); }
float* NoiseUp() { return ei::GamePointer<float>(0x0079C838); }

// Branch sway frequencies: six pairs (side, up) at 00787C8C.
const float* BranchFrequencies() { return ei::GamePointer<const float>(0x00787C8C); }

// World clock: whole ticks (007AF61C) and the fraction of the current one (007AFCA8).
int GameTicks() { return ei::GameData<const volatile int>(0x007AF61C); }
float GameTickFraction() { return ei::GameData<const volatile float>(0x007AFCA8); }

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
    int count = segment->points.size;
    const float* table = NoiseTable();
    for (int p = 0; p < count; ++p) {
        double sum = 0.0;
        for (int j = 0; j < 8; ++j) sum = sum + static_cast<double>(table[rows[j] * kNoiseRowLength + p]) * weights[j];
        out[p] = static_cast<float>(sum);
    }
}

// The noise amplitude a segment animates with: 1.5 for positive power, 1 for negative, 2 for 0
// (and NaN).
float NoiseAmplitude(float power) {
    if (power > 0.0f) return 1.5f;
    return power < 0.0f ? 1.0f : 2.0f;
}


} // namespace

// --- NoiseKey / LightningSegment ----------------------------------------------------------------

void NoiseKey::Randomize(float amplitude) { RandomizeKey(*this, amplitude, false); }
EI_REPLACE_METHOD(0x00466190, NoiseKey::Randomize);

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
EI_REPLACE_METHOD(0x004662C0, LightningSegment::SampleNoise);

void LightningSegment::Animate(float time, float amplitude) {
    int count = points.size;
    float* sideNoise = NoiseSide();
    float* upNoise = NoiseUp();
    SampleNoise(time, sideNoise, upNoise, amplitude);
    float countF = static_cast<float>(count);
    double sideAt = sideNoise[0];
    float sideSlope = static_cast<float>((static_cast<double>(sideNoise[count - 1]) - sideNoise[0]) / countF);
    double upAt = upNoise[0];
    float upSlope = static_cast<float>((static_cast<double>(upNoise[count - 1]) - upNoise[0]) / countF);
    for (int i = 0; i < points.size; ++i) {
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
        LightningPoint& point = points.data[i];
        point.x = static_cast<float>(rx);
        point.y = ry;
        point.z = rz;
        sideAt = sideAt + sideSlope;
        upAt = upAt + upSlope;
    }
}
EI_REPLACE_METHOD(0x00466CE0, LightningSegment::Animate);

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
        count = 48;             // FISTP(50.0) & ~3
    } else if (length > 15.0f) {
        scale = 15.0f;
        count = 72;             // FISTP(75.0) & ~3
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
    points.SetSize(count, -1);   // 00468C50
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
        boundsMin_.x = p.x >= boundsMin_.x ? boundsMin_.x : p.x;
        boundsMin_.y = p.y >= boundsMin_.y ? boundsMin_.y : p.y;
        boundsMin_.z = p.z >= boundsMin_.z ? boundsMin_.z : p.z;
        boundsMax_.x = boundsMax_.x >= p.x ? boundsMax_.x : p.x;
        boundsMax_.y = boundsMax_.y >= p.y ? boundsMax_.y : p.y;
        boundsMax_.z = boundsMax_.z >= p.z ? boundsMax_.z : p.z;
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
    float sumY = static_cast<float>(static_cast<double>(boundsMin_.y) + boundsMax_.y);
    float sumZ = static_cast<float>(static_cast<double>(boundsMin_.z) + boundsMax_.z);
    center_.x = static_cast<float>(sumX * 0.5);
    center_.y = static_cast<float>(static_cast<double>(sumY) * 0.5);
    center_.z = static_cast<float>(static_cast<double>(sumZ) * 0.5);
    boundsSpare_.x = 0.0f;
    boundsSpare_.y = 0.0f;
    boundsSpare_.z = 0.0f;
    boundsMin_.x = static_cast<float>(static_cast<double>(boundsMin_.x) - center_.x);
    boundsMin_.y = static_cast<float>(static_cast<double>(boundsMin_.y) - center_.y);
    boundsMin_.z = static_cast<float>(static_cast<double>(boundsMin_.z) - center_.z);
    boundsMax_.x = static_cast<float>(static_cast<double>(boundsMax_.x) - center_.x);
    boundsMax_.y = static_cast<float>(static_cast<double>(boundsMax_.y) - center_.y);
    boundsMax_.z = static_cast<float>(static_cast<double>(boundsMax_.z) - center_.z);
    boundsKind_ = 3;
    radius_ = static_cast<float>(std::sqrt(LengthSquaredUnrounded<Sum3::ZXY, double>(boundsMax_)));
}
EI_REPLACE_METHOD(0x00467150, CLightning::UpdateBounds);

void CLightning::Animate() {
    float sway = amplitude_;
    float time = static_cast<float>((static_cast<double>(GameTicks()) + GameTickFraction()) / 4.0);
    main_.Animate(time, NoiseAmplitude(power_));
    EI_NAN_AUDIT(power_);
    if (power_ != 0.0f) {
        // The branches follow the main segment: each starts at its point of the bolt and ends near
        // the bolt's end, swaying on two sines.
        double n = main_.points.size;
        Vector3 length;
        length.x = static_cast<float>(n * main_.step.x);
        length.y = static_cast<float>(n * main_.step.y);
        length.z = static_cast<float>(n * main_.step.z);
        Vector3 end;
        AddVectors(&end, main_.start, length);
        const float* frequency = BranchFrequencies();
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

            int index = x87::fistp(static_cast<float>((1.0 - branchPos_[i]) * main_.points.size));
            int last = main_.points.size - 1;
            if (index >= last) index = last;
            const LightningPoint& origin = main_.points.data[index];
            branch.start.x = origin.x;
            branch.start.y = origin.y;
            branch.start.z = origin.z;
            float inverse = static_cast<float>(1.0 / branch.points.size);
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
EI_REPLACE_METHOD(0x004672E0, CLightning::Animate);

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
            std::uint32_t r1 = Rng()->Next();
            std::uint32_t r2 = Rng()->Next();
            std::uint32_t r3 = Rng()->Next();
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
EI_REPLACE_METHOD(0x00467E10, CLightning::SetEndpoints);

// --- CLightningManager --------------------------------------------------------------------------

int CLightningManager::DrawAll(void* device) {
    // The manager's object list (the bolts, in registration order).
    ObjectList::Node* node = objectList_.head;
    while (node) {
        ObjectList::Node* current = node;
        node = node->next;
        static_cast<CLightning*>(current->data)->DrawImpl(device);
    }
    for (auto* n = newBolts_.head; n; n = n->next) {
        if (n->data) n->data->Release();
    }
    newBolts_.RemoveAll();
    return 1;
}
EI_REPLACE_METHOD(0x00468940, CLightningManager::DrawAll);
