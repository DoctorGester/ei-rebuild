// Lightning bolts.
#include "lightning.h"

#include "camera.h"
#include "core_maths.h"
#include "game_singletons.h"
#include "model.h"
#include "random.h"
#include "resource_local.h"
#include "texture.h"
#include "world.h"
#include "world_data.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/nan_audit.h"

#include <bit>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

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
        Vector3 end = AddVectors(main.start, length);
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

// --- Start -------------------------------------------------------------------------------------------

// Lightning's initialiser (called by the lightning client object's setter). Plain float maths (rule 7). See docs/notes/objects.md ("Rewritten").

namespace {

// The noise amplitude of a segment's keys: 1.5 for positive power, 1 for negative, else 2 (zero or NaN).
float KeyAmplitude(float power) {
    if (power > 0.0f) return 1.5f;
    return power < 0.0f ? 1.0f : 2.0f;
}

void FreshKeys(LightningSegment& s, float time, float amplitude) {
    s.noiseTime = time;
    for (int k = 0; k < 3; ++k) {
        s.sideKeys[k].Randomize(amplitude);
        s.upKeys[k].Randomize(amplitude);
    }
}

} // namespace

void Lightning::Start(const Vector3& from, const Vector3& to, float newPower, bool demon) {
    int ticks = g_clientWorld.tick;
    float fraction = g_clientWorld.tickFraction;
    float time = (ticks + fraction) / 4.0f;
    power = newPower;
    const float strength = AbsBits(newPower);
    amplitude = power > 0.0f ? 0.05f : 0.3f;     // 0.05 : 0.3
    // A bolt about 30 units high sways less.
    EI_NAN_AUDIT(to.z, from.z);
    if (AbsBits(AbsBits(to.z - from.z) - 30.0f) < 0.01f) amplitude = 0.05f;

    main.Init(from, to);
    FreshKeys(main, time, KeyAmplitude(power));
    main.width = strength;
    EI_NAN_AUDIT(power);
    if (power < 0.0f) main.width = strength * 1.5f;

    EI_NAN_AUDIT(strength);
    if (strength != 0.0f) {
        const float positions[6] = {0.2f, 0.3f, 0.5f, 0.8f, 0.9f, 0.9f};
        std::memcpy(branchPos, positions, sizeof(positions));
        for (int i = 0; i < 6; ++i) {
            float along = branchPos[i];
            float newAmplitude = KeyAmplitude(power);
            float rest = 1.0f - along;
            Vector3 origin;
            origin.x = along * from.x + rest * to.x;
            origin.y = along * from.y + rest * to.y;
            origin.z = along * from.z + rest * to.z;
            branches[i].Init(origin, to);
            FreshKeys(branches[i], time, newAmplitude);
            branches[i].width = (g_clientRandom.Next() * 1.8626452e-10f + 0.7f) * strength * 0.5f;
        }
    } else {
        main.width = 4.0f;
        for (int i = 0; i < 6; ++i) {
            float dx = from.x - to.x, dy = from.y - to.y, dz = from.z - to.z;
            float reach = std::sqrt(LengthSquaredUnrounded<Sum3::ZYX>(dx, dy, dz)) * 0.05f;
            float low = -reach;
            const float scale = 2.3283064e-10f;
            float offsetY = (reach - low) * g_clientRandom.Next() * scale + low;
            float offsetX = (reach - low) * g_clientRandom.Next() * scale + low;
            float offsetZ = (reach - low) * g_clientRandom.Next() * scale + low;
            float newAmplitude = KeyAmplitude(power);
            Vector3 shiftedTo{offsetX + to.x, offsetY + to.y, offsetZ + to.z};
            Vector3 shiftedFrom{offsetX + from.x, offsetY + from.y, offsetZ + from.z};
            branches[i].Init(shiftedFrom, shiftedTo);
            FreshKeys(branches[i], time, newAmplitude);
            branches[i].width = g_clientRandom.Next() * 1.8626452e-10f * 3.5f + 2.45f;   // 2.45 .. 3.25
        }
    }

    Animate();
    UpdateBounds();
    bounds.boxMin.x -= 1.0f;
    bounds.boxMin.y -= 1.0f;
    bounds.boxMin.z -= 1.0f;
    bounds.boxMax.x += 1.0f;
    bounds.boxMax.y += 1.0f;
    bounds.boxMax.z += 1.0f;
    bounds.radius = bounds.boxMax.Length();
    if (demon) {
        LoadNamedTexture(glowTexture, static_cast<int>(TextureKind::Effects), "DemonLight");
        LoadNamedTexture(texture, static_cast<int>(TextureKind::Effects), "DemonLightning");
    } else {
        LoadNamedTexture(glowTexture, static_cast<int>(TextureKind::Effects), "SmtgLight");
        LoadNamedTexture(texture, static_cast<int>(TextureKind::Effects), "Lightning");
    }
}

LightningSegment::LightningSegment() {
}

LightningSegment::~LightningSegment() {
}

Lightning::Lightning() {
    std::memset(static_cast<void*>(&bounds), 0, sizeof(bounds));
    boundsSpare = {};
    Lightning::Destroy();
}

// The texture references are null after the reset (releasing them again is a no-op).
Lightning::~Lightning() { Lightning::Destroy(); }

const char* Lightning::ClassName() { return "CLightning"; }
ClassId Lightning::GetClassId() { return ClassId::Lightning; }

EngineObject* LightningManager::NewObject(int /*type*/) { return new Lightning(); }

// --- Load --------------------------------------------------------------------------------------------

// The lightning noise table's load. Rebirth patches the function (it redirects the
// file lookup to a mod folder's copy): this is the vanilla function and it
// installs in the vanilla launch only (rule 9). The lightning manager's Create calls it through
// its address (LoadLightningNoiseOriginal). Main thread. See docs/notes/objects.md.

// Opens <text folder>gfx.res and reads lightning.dat into the noise table, row by row through a memory stream
// (a short file leaves the rest zero-filled, as the stream's Read does). Returns 0 when the archive does not
// open (no message), the fatal error's result when the file is missing, else 1.
int LoadLightningNoise() {
    LocalResource resource;
    MemoryStream local;
    bool opened;
    {
        std::string folder = g_textFolder;
        std::string path = folder + "gfx.res";
        // Through the vtable.
        opened = resource.Get()->OpenResource(path, 0) != 0;
    }
    if (!opened) return 0;

    std::uint32_t size;
    void* data = resource.Get()->MappedResource::GetFile("lightning.dat", &size);
    if (!data) return game::FatalError("Unable to open file gfx.res\\lightning.dat");

    FillStream(local, data, size);
    ByteStream* stream = &local;
    float* row = g_lightningNoise[0];
    for (int i = 0; i < kLightningNoiseRows; ++i, row += kLightningNoiseRowLength)
        stream->Read(row, kLightningNoiseRowLength * sizeof(float));
    resource.Get()->MappedResource::CloseFile(data);
    return 1;
}

// --- Draw --------------------------------------------------------------------------------------------

// Drawing a lightning bolt (Lightning's renderer virtual; called by DrawAll) and its
// glow: the segments as textured strips through the render device, then a
// glow: the segments as textured strips through the render device, then a
// the world's frame counter. Plain float maths (rule 7). See docs/notes/objects.md ("Rewritten").

namespace {

// The glow's work arrays: the sprite records (zero filled) and the 16-byte points.

struct SpriteRecord {   // 8 bytes: colour, texture rectangle
    std::uint32_t colour;
    const float* rect;
};
std::vector<SpriteRecord> g_sprites;
std::vector<LightningPoint> g_glowPoints;

void SizeSprites() { g_sprites.resize(8); }
void SizePoints() { g_glowPoints.resize(8); }

// A random texture offset: 0, 0.25, 0.5 or 0.75.
float RandomOffset() {
    return static_cast<int>(g_clientRandom.Next() & 3) * 0.25f;
}

} // namespace

// `position` the glow's centre, `size` its radius; the frame argument the caller computes is not
// used (the frame comes from the position and the world's counter).
void Lightning::DrawGlow(SceneRenderer* device, const Vector3& position, float size) {
    EnsureTextureLoaded(glowTexture);
    if (!device->SetTexture(0, glowTexture)) return;

    // The 15 frames of the 4 x 4 glow texture: (u0, v0, u1, v1).
    float frames[15][4];
    const float quarter = 0.25f;
    for (int i = 0; i < 15; ++i) {
        frames[i][0] = (i & 3) * quarter;
        frames[i][1] = 0.75f - ((i >> 2) & 3) * quarter;
        frames[i][2] = frames[i][0] + quarter;
        frames[i][3] = frames[i][1] + quarter;
    }
    SizeSprites();
    SizePoints();

    // Towards the camera, normalised unless zero.
    const Vector3& eye = camera::FrameView().position;
    Vector3 towards = eye - position;
    float length = LengthZYX(towards);
    EI_NAN_AUDIT(length);
    if (length != 0.0f) towards /= length;
    float scaled = position.x * 15.0f;
    int shift = std::lround(scaled);
    int frame = (OwningWorld()->tick + shift) % 15;
    for (int j = 0; j < 8; ++j) {
        float t = j * 0.125f;   // 1/8
        Vector3 along{towards.x * t * size, towards.y * t * size, towards.z * t * size};
        LightningPoint& p = g_glowPoints[j];
        p.x = position.x + along.x;
        p.y = position.y + along.y;
        p.z = position.z + along.z;
        p.w = std::sqrt(1.0f - t * t) * size;
        SpriteRecord& s = g_sprites[j];
        s.colour = 0xA0FFFFFFu;
        s.rect = frames[frame % 15];
        frame += 2;
    }
    device->DrawSprites(0, &g_glowPoints, &g_sprites);
}

// 0 when the texture cannot be set; 1 otherwise (also when the bolt is not visible).
int Lightning::Draw(SceneRenderer* device) {
    EnsureTextureLoaded(texture);
    SceneRenderer* renderer = device;
    if (!renderer->SetTexture(0, texture)) return 0;
    int visibility = renderer->pipeline->ClipTest(&bounds);
    if (visibility == 2) return 1;
    const float zero = 0.0f;
    if (power >= zero) Animate();
    const float slant = 0.25f;
    EI_NAN_AUDIT(power);
    if (power < zero) {    // negative: untextured lengthwise
        renderer->DrawStrip(&main.points, main.width, visibility, 0.0f, 0.0f, slant);
        for (LightningSegment& b : branches) renderer->DrawStrip(&b.points, b.width, visibility, 0.0f, 0.0f, slant);
    } else {
        float offset = RandomOffset();
        float span = LengthZXY(main.step) * std::ssize(main.points) * 0.6f;
        renderer->DrawStrip(&main.points, main.width, visibility, span, offset, slant);
        for (LightningSegment& b : branches) {
            offset = RandomOffset();
            span = LengthZXY(b.step) * std::ssize(b.points) * 0.6f;
            renderer->DrawStrip(&b.points, b.width, visibility, span, offset, slant);
        }
    }

    float count = static_cast<float>(std::ssize(main.points));
    Vector3 start = main.start;
    Vector3 run{count * main.step.x, count * main.step.y, count * main.step.z};
    Vector3 end{start.x + run.x, start.y + run.y, start.z + run.z};
    float dx = start.x - end.x, dy = start.y - end.y, dz = start.z - end.z;
    float size = std::sqrt(LengthSquaredUnrounded<Sum3::ZYX>(dx, dy, dz)) * 0.11f;
    if (power > zero) {
        // Clamped to [0.5, 1.2] with selects (NaN keeps the size).
        float low = 0.5f, high = 1.2f;
        size = MaxKeepB(low, size);
        size = MinKeepA(size, high);
    }
    if (power >= zero) DrawGlow(device, end, size);
    if (power == zero) DrawGlow(device, start, size);
    return 1;
}
