// Lightning bolts.
#include "lightning.h"

#include "core_maths.h"
#include "game_singletons.h"
#include "random.h"
#include "texture.h"
#include "world.h"

#include "lib/containers.h"
#include "lib/float_select.h"
#include "lib/nan_audit.h"

#include <bit>
#include <cmath>
#include <concepts>
#include <cstring>
#include <initializer_list>

// --- Helpers -----------------------------------------------------------------------------------

namespace {

// The noise table: 32 rows of 400 floats (rows 0..15 for the stored keys, 16..31 for the key drawn
// for each sample), read from gfx.res\lightning.dat by LoadLightningNoise.
constexpr int kNoiseRowLength = 400;

// Scratch curves written by LightningSegment::Animate (one float per point, at most 400).
float g_noiseSide[400] = /*0079C1F8*/ {};
float g_noiseUp[400] = /*0079C838*/ {};

constexpr float kBranchFrequencies[12] = /*00787C8C*/ {2.0f, 3.0f, 2.5f, 1.8f, 0.3f, 0.8f, 1.2f, 1.3f, 3.0f, 5.1f, 2.1f, 1.76f};

constexpr float kNoiseScale = 4.656613e-10f;   // about 2 / 2^32
constexpr float kRandomScale = 2.3283064e-10f;  // about 1 / 2^32
constexpr float kSideA = -0.0666f;
constexpr float kSideB = 0.013f;
constexpr float kOffsetFactor = 0.05f;

float FromBits(std::uint32_t bits) {
    float f;
    f = std::bit_cast<float>(bits);
    return f;
}
// A float argument would convert to an integer first: callers pass raw bits only.
template <std::floating_point T> float FromBits(T) = delete;

// Randomize a key. The up-noise keys of SampleNoise multiply the
// normaliser by the amplitude before the weight; everything else multiplies the weight first.
void RandomizeKey(NoiseKey& key, float amplitude, bool amplitudeFirst) {
    float sum = 0.0f;
    for (int i = 0; i < 2; ++i) {
        key.index[i] = g_clientRandom.Next() & 0xF;
        float v = WordToFloat(g_clientRandom.Next()) * kNoiseScale - 1.0f;
        key.weight[i] = v;
        sum = v * v + sum;
    }
    float scale = 1.0f / std::sqrt(sum);
    for (int i = 0; i < 2; ++i) {
        if (amplitudeFirst)
            key.weight[i] = scale * amplitude * key.weight[i];
        else
            key.weight[i] = scale * key.weight[i] * amplitude;
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
    for (int j = 0; j < 8; ++j) weights[j] = blend[j] * weights[j];
    int count = lib::Count(segment->points);
    const float* table = g_lightningNoise[0];
    for (int p = 0; p < count; ++p) {
        float sum = 0.0f;
        for (int j = 0; j < 8; ++j) sum = sum + table[rows[j] * kNoiseRowLength + p] * weights[j];
        out[p] = sum;
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
    float elapsed = time - noiseTime;
    float left = elapsed;
    bool advance;
    if (elapsed > 100.0f) {
        left = 100.0f;
        advance = true;
    } else {
        advance = left > 1.0f;
    }
    if (advance) {
        float next;
        do {
            sideKeys[2] = sideKeys[1];
            sideKeys[1] = sideKeys[0];
            RandomizeKey(sideKeys[0], amplitude, false);
            upKeys[2] = upKeys[1];
            upKeys[1] = upKeys[0];
            RandomizeKey(upKeys[0], amplitude, true);
            noiseTime = noiseTime + 1.0f;
            next = left - 1.0f;
            left = next;
        } while (next > 1.0f);
    }
    // Uniform B-spline blend of the keys at the fraction `left`.
    float squared = left * left;
    float half = squared * 0.5f;
    float blend[8];
    blend[0] = blend[1] = half;
    blend[2] = blend[3] = (left + 0.5f) - squared;
    blend[4] = blend[5] = (0.5f - left) + half;
    blend[6] = blend[7] = 1.0f;
    SampleCurve(this, sideKeys, blend, outSide, amplitude);
    SampleCurve(this, upKeys, blend, outUp, amplitude);
}

void LightningSegment::Animate(float time, float amplitude) {
    int count = lib::Count(points);
    float* sideNoise = g_noiseSide;
    float* upNoise = g_noiseUp;
    SampleNoise(time, sideNoise, upNoise, amplitude);
    float countF = static_cast<float>(count);
    float sideAt = sideNoise[0];
    float sideSlope = (sideNoise[count - 1] - sideNoise[0]) / countF;
    float upAt = upNoise[0];
    float upSlope = (upNoise[count - 1] - upNoise[0]) / countF;
    for (int i = 0; i < std::ssize(points); ++i) {
        float s = sideNoise[i] - sideAt;   // the curve minus its straight line from the first value
        float u = upNoise[i] - upAt;
        float along = static_cast<float>(i);
        LightningPoint& point = points[i];
        point.x = along * step.x + start.x + s * side.x + u * up.x;
        point.y = along * step.y + start.y + s * side.y + u * up.y;
        point.z = along * step.z + start.z + s * side.z + u * up.z;
        sideAt = sideAt + sideSlope;
        upAt = upAt + upSlope;
    }
}

void LightningSegment::Init(const Vector3& from, const Vector3& to) {
    start = from;
    step.x = to.x - from.x;
    step.y = to.y - from.y;
    step.z = to.z - from.z;
    float length = std::sqrt(LengthSquaredUnrounded<Sum3::XYZ>(step));

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
        count = std::lround(length * 5.0f) & ~3;
        if (count > 400) count = 400;
        else if (count < 4) count = 4;
    }

    side.x = step.y - step.z * kSideA;
    side.y = step.z * kSideB - step.x;
    side.z = step.x * kSideA - step.y * kSideB;
    // up = cross(step, side), through the Vector3 constructor's float arguments.
    float upZ = step.y * side.x - step.x * side.y;
    float upY = step.x * side.z - side.x * step.z;
    float upX = side.y * step.z - step.y * side.z;
    up.x = upX;
    up.y = upY;
    up.z = upZ;

    for (Vector3* axis : {&side, &up}) {
        float norm = std::sqrt(LengthSquaredUnrounded<Sum3::XYZ>(*axis));
        if (norm > 0.0f) {
            float factor = scale / norm;
            axis->x = factor * axis->x;
            axis->y = factor * axis->y;
            axis->z = factor * axis->z;
        } else {
            axis->x = 0.0f;
            axis->y = 0.0f;
            axis->z = 0.0f;
        }
    }

    step /= static_cast<float>(count);
    points.resize(count);
}

// --- Lightning --------------------------------------------------------------------------------

void Lightning::Destroy() {
    power = 1.0f;
    amplitude = FromBits(0x3DCCCCCD);    // 0.1f
    texture.Reset();
    glowTexture.Reset();
}

void Lightning::Create() {
    Destroy();
    auto* manager = static_cast<LightningManager*>(owner);
    manager->newBolts.push_back(this);
    ++refCount;
}

void Lightning::ExpandBounds(const LightningSegment* segment) {
    for (int i = 0; i < std::ssize(segment->points); ++i) {
        const LightningPoint& p = segment->points[i];
        // Minimum: the point when it is smaller or unordered; maximum likewise.
        bounds.boxMin.x = MinKeepA(p.x, bounds.boxMin.x);
        bounds.boxMin.y = MinKeepA(p.y, bounds.boxMin.y);
        bounds.boxMin.z = MinKeepA(p.z, bounds.boxMin.z);
        bounds.boxMax.x = MaxKeepB(bounds.boxMax.x, p.x);
        bounds.boxMax.y = MaxKeepB(bounds.boxMax.y, p.y);
        bounds.boxMax.z = MaxKeepB(bounds.boxMax.z, p.z);
    }
}

void Lightning::UpdateBounds() {
    const float largest = FromBits(0x7149F2CA);      // 1e30
    const float lowest = FromBits(0xF149F2CA);    // -1e30
    bounds.boxMin.x = largest;
    bounds.boxMin.y = largest;
    bounds.boxMin.z = largest;
    bounds.boxMax.x = lowest;
    bounds.boxMax.y = lowest;
    bounds.boxMax.z = lowest;
    ExpandBounds(&main);
    for (int i = 0; i < 6; ++i) ExpandBounds(&branches[i]);

    float sumX = bounds.boxMin.x + bounds.boxMax.x;
    float sumY = bounds.boxMin.y + bounds.boxMax.y;
    float sumZ = bounds.boxMin.z + bounds.boxMax.z;
    bounds.center.x = sumX * 0.5f;
    bounds.center.y = sumY * 0.5f;
    bounds.center.z = sumZ * 0.5f;
    boundsSpare.x = 0.0f;
    boundsSpare.y = 0.0f;
    boundsSpare.z = 0.0f;
    bounds.boxMin.x = bounds.boxMin.x - bounds.center.x;
    bounds.boxMin.y = bounds.boxMin.y - bounds.center.y;
    bounds.boxMin.z = bounds.boxMin.z - bounds.center.z;
    bounds.boxMax.x = bounds.boxMax.x - bounds.center.x;
    bounds.boxMax.y = bounds.boxMax.y - bounds.center.y;
    bounds.boxMax.z = bounds.boxMax.z - bounds.center.z;
    bounds.flags = lib::Bits(BoundsFlag::Sphere | BoundsFlag::Box);
    bounds.radius = std::sqrt(LengthSquaredUnrounded<Sum3::ZXY>(bounds.boxMax));
}

void Lightning::Animate() {
    float sway = amplitude;
    float time = (g_clientWorld.tick + g_clientWorld.tickFraction) / 4.0f;
    main.Animate(time, NoiseAmplitude(power));
    EI_NAN_AUDIT(power);
    if (power != 0.0f) {
        // The branches follow the main segment: each starts at its point of the bolt and ends near
        // the bolt's end, swaying on two sines.
        Vector3 length = main.step * static_cast<float>(std::ssize(main.points));
        Vector3 end;
        AddVectors(&end, main.start, length);
        const float* frequency = kBranchFrequencies;
        for (int i = 0; i < 6; ++i) {
            LightningSegment& branch = branches[i];
            float reach = sway * branchPos[i];
            float sideSway = std::sin(time * frequency[2 * i] * 0.25f) * reach;
            float upSway = std::sin(time * frequency[2 * i + 1] * 0.25f) * reach;
            float ux = upSway * main.up.x;
            float uy = upSway * main.up.y;
            float uz = upSway * main.up.z;
            float sx = sideSway * main.side.x;
            float sy = sideSway * main.side.y;
            float sz = sideSway * main.side.z;
            float wx = sx + end.x;
            float wy = end.y + sy;
            float wz = end.z + sz;
            float targetX = wx + ux;
            float targetY = wy + uy;
            float targetZ = wz + uz;

            int index = std::lround((1.0f - branchPos[i]) * std::ssize(main.points));
            int last = lib::Count(main.points) - 1;
            if (index >= last) index = last;
            const LightningPoint& origin = main.points[index];
            branch.start.x = origin.x;
            branch.start.y = origin.y;
            branch.start.z = origin.z;
            float inverse = 1.0f / std::ssize(branch.points);
            float dz = targetZ - branch.start.z;
            float dy = targetY - branch.start.y;
            float dx = targetX - branch.start.x;
            float stepZ = dz * inverse;
            float stepY = dy * inverse;
            float stepX = dx * inverse;
            branch.step.x = stepX;
            branch.step.y = stepY;
            branch.step.z = stepZ;
        }
    }
    for (int i = 0; i < 6; ++i) branches[i].Animate(time, NoiseAmplitude(power));
}

void Lightning::SetEndpoints(const Vector3& from, const Vector3& to) {
    main.Init(from, to);
    EI_NAN_AUDIT(power);
    if (power != 0.0f) {
        // Each branch runs from its point of the bolt to the end.
        for (int i = 0; i < 6; ++i) {
            float along = branchPos[i];
            float rest = 1.0f - along;
            float ax = rest * to.x;
            float ay = rest * to.y;
            float az = rest * to.z;
            float bx = along * from.x;
            float by = along * from.y;
            float bz = along * from.z;
            Vector3 origin;
            origin.x = bx + ax;
            origin.y = by + ay;
            origin.z = bz + az;
            branches[i].Init(origin, to);
        }
    } else {
        // Power 0: six more bolts parallel to the main one, shifted by a random offset of up to
        // 5% of its length on each axis.
        for (int i = 0; i < 6; ++i) {
            float dx = from.x - to.x;
            float dy = from.y - to.y;
            float dz = from.z - to.z;
            float reachExt = std::sqrt(LengthSquaredUnrounded<Sum3::ZYX>(dx, dy, dz)) * kOffsetFactor;
            float reach = reachExt;
            float low = -reachExt;
            std::uint32_t r1 = g_clientRandom.Next();
            std::uint32_t r2 = g_clientRandom.Next();
            std::uint32_t r3 = g_clientRandom.Next();
            float ox = (reach - low) * WordToFloat(r3) * kRandomScale + low;
            float oy = (reach - low) * WordToFloat(r2) * kRandomScale + low;
            float ozExt = (reach - low) * WordToFloat(r1) * kRandomScale + low;
            float oz = ozExt;
            Vector3 shiftedTo;
            shiftedTo.z = ozExt + to.z;
            shiftedTo.y = oy + to.y;
            shiftedTo.x = ox + to.x;
            Vector3 shiftedFrom;
            shiftedFrom.z = oz + from.z;
            shiftedFrom.y = oy + from.y;
            shiftedFrom.x = ox + from.x;
            branches[i].Init(shiftedFrom, shiftedTo);
        }
    }
    EI_NAN_AUDIT(power);
    if (power < 0.0f) Animate();
    UpdateBounds();
}

// --- LightningManager --------------------------------------------------------------------------

int LightningManager::DrawAll(SceneRenderer* device) {
    // The manager's object list (the bolts, in creation order).
    auto node = objectList.begin();
    while (node != objectList.end()) {
        auto current = node;
        ++node;
        static_cast<Lightning*>(*current)->Draw(device);
    }
    for (auto& n : newBolts) {
        if (n) n->Release();
    }
    newBolts.clear();
    return 1;
}

// --- The manager's Create: the noise table loaded once --------------------------------------------

namespace {
int g_lightningNoiseLoaded = /*007A9694*/ {};
} // namespace

void LightningManager::Create() {
    if (!g_lightningNoiseLoaded) {
        g_lightningNoiseLoaded = 1;
        LoadLightningNoise();
    }
    BaseManager::Create();
}

// --- Class name and id ------------------------------------------------------------------------------

namespace {
constexpr const char* kLightningManagerClassName = "CLightningManager";
} // namespace

const char* LightningManager::ClassName() { return kLightningManagerClassName; }
ClassId LightningManager::GetClassId() { return ClassId::LightningManager; }
