// Lightning bolts.
#include "game/effects/lightning.h"

#include "engine/scene/camera.h"
#include "engine/core/core_maths.h"
#include "game/app/singletons.h"
#include "engine/scene/scene_renderer.h"
#include "engine/scene/pipeline.h"
#include "engine/models/texture.h"
#include "engine/core/random.h"
#include "engine/data/resource.h"
#include "engine/core/core_streams.h"
#include "game/world/world.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/maths.h"

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

// The bolt's side is across it and this axis (nearly straight up, so no bolt runs along it).
constexpr Vector3 kSideAxis = {0.013f, -0.0666f, 1.0f};
constexpr float kOffsetFactor = 0.05f;


// Randomize a key. The up-noise keys of SampleNoise multiply the
// normaliser by the amplitude before the weight; everything else multiplies the weight first.
void RandomizeKey(NoiseKey& key, float amplitude, bool amplitudeFirst) {
    float sum = 0.0f;
    for (int i = 0; i < 2; ++i) {
        key.index[i] = g_clientRandom.Next() & 0xF;
        float v = g_clientRandom.Below(2.0f) - 1.0f;
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
            noiseTime += 1.0f;
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
        points[i] = along * step + start + s * side + u * up;
        sideAt += sideSlope;
        upAt += upSlope;
    }
}

void LightningSegment::Init(const Vector3& from, const Vector3& to) {
    start = from;
    step = to - from;
    float length = std::sqrt(LengthSquared(step));

    // Point count and the length the displacement axes are scaled to.
    int count;
    float scale = length;
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

    side = Cross(step, kSideAxis);
    up = Cross(side, step);

    for (Vector3* axis : {&side, &up}) {
        float norm = std::sqrt(LengthSquared(*axis));
        if (norm > 0.0f) {
            *axis *= scale / norm;
        } else {
            *axis = {};
        }
    }

    step /= static_cast<float>(count);
    points.resize(count);
}

// --- Lightning --------------------------------------------------------------------------------

void Lightning::Destroy() {
    power = 1.0f;
    amplitude = 0.1f;
    texture.Reset();
    glowTexture.Reset();
}

void Lightning::Create() {
    Destroy();
    ClientWorld()->listManager->newBolts.push_back(this);
    ++refCount;
}

void Lightning::ExpandBounds(const LightningSegment* segment) {
    for (int i = 0; i < std::ssize(segment->points); ++i) {
        // Minimum: the point when it is smaller or unordered; maximum likewise.
        bounds.boxMin = MinKeepA(segment->points[i], bounds.boxMin);
        bounds.boxMax = MaxKeepB(bounds.boxMax, segment->points[i]);
    }
}

void Lightning::UpdateBounds() {
    bounds.boxMin = {1e30f, 1e30f, 1e30f};
    bounds.boxMax = {-1e30f, -1e30f, -1e30f};
    ExpandBounds(&main);
    for (int i = 0; i < 6; ++i) ExpandBounds(&branches[i]);

    bounds.center = (bounds.boxMin + bounds.boxMax) * 0.5f;
    bounds.boxMin -= bounds.center;
    bounds.boxMax -= bounds.center;
    bounds.flags = lib::Bits(BoundsFlag::Sphere | BoundsFlag::Box);
    bounds.radius = std::sqrt(LengthSquared(bounds.boxMax));
}

void Lightning::Animate() {
    float sway = amplitude;
    float time = (g_clientWorld.tick + g_clientWorld.tickFraction) / 4.0f;
    main.Animate(time, NoiseAmplitude(power));
    if (power != 0.0f) {
        // The branches follow the main segment: each starts at its point of the bolt and ends near
        // the bolt's end, swaying on two sines.
        Vector3 length = main.step * static_cast<float>(std::ssize(main.points));
        Vector3 end = main.start + length;
        const float* frequency = kBranchFrequencies;
        for (int i = 0; i < 6; ++i) {
            LightningSegment& branch = branches[i];
            float reach = sway * branchPos[i];
            float sideSway = lib::Sin(time * frequency[2 * i] * 0.25f) * reach;
            float upSway = lib::Sin(time * frequency[2 * i + 1] * 0.25f) * reach;
            Vector3 target = sideSway * main.side + end + upSway * main.up;

            int index = std::lround((1.0f - branchPos[i]) * std::ssize(main.points));
            int last = lib::Count(main.points) - 1;
            if (index >= last) index = last;
            branch.start = main.points[index];
            float inverse = 1.0f / std::ssize(branch.points);
            branch.step = (target - branch.start) * inverse;
        }
    }
    for (int i = 0; i < 6; ++i) branches[i].Animate(time, NoiseAmplitude(power));
}

void Lightning::SetEndpoints(const Vector3& from, const Vector3& to) {
    main.Init(from, to);
    if (power != 0.0f) {
        // Each branch runs from its point of the bolt to the end.
        for (int i = 0; i < 6; ++i) {
            float along = branchPos[i];
            float rest = 1.0f - along;
            branches[i].Init(along * from + rest * to, to);
        }
    } else {
        // Power 0: six more bolts parallel to the main one, shifted by a random offset of up to
        // 5% of its length on each axis.
        for (int i = 0; i < 6; ++i) {
            float reach = std::sqrt(LengthSquared(from - to)) * kOffsetFactor;
            float low = -reach;
            std::uint32_t r1 = g_clientRandom.Next();
            std::uint32_t r2 = g_clientRandom.Next();
            std::uint32_t r3 = g_clientRandom.Next();
            Vector3 offset = {RangeOfWord(r3, low, reach), RangeOfWord(r2, low, reach), RangeOfWord(r1, low, reach)};
            branches[i].Init(offset + from, offset + to);
        }
    }
    if (power < 0.0f) Animate();
    UpdateBounds();
}

// --- LightningManager --------------------------------------------------------------------------

int LightningManager::DrawAll(SceneRenderer* device) {
    // The manager's object list: the bolts (the manager makes no other class), in creation order.
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

// --- The manager's Create: the noise table loaded (the client world's one manager, made at each start) -----------

void LightningManager::Create() {
    LoadLightningNoise();
    BaseManager::Create();
}

// --- Class name and id ------------------------------------------------------------------------------

namespace {
constexpr const char* kLightningManagerClassName = "CLightningManager";
} // namespace

const char* LightningManager::ClassName() { return kLightningManagerClassName; }
ClassId LightningManager::GetClassId() { return ClassId::LightningManager; }

// --- Start -------------------------------------------------------------------------------------------

// Lightning's initialiser (called by the lightning client object's setter). Plain float maths (rule 7). See docs/notes/objects.md, "Lightning".

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
    if (AbsBits(AbsBits(to.z - from.z) - 30.0f) < 0.01f) amplitude = 0.05f;

    main.Init(from, to);
    FreshKeys(main, time, KeyAmplitude(power));
    main.width = strength;
    if (power < 0.0f) main.width = strength * 1.5f;

    if (strength != 0.0f) {
        const float positions[6] = {0.2f, 0.3f, 0.5f, 0.8f, 0.9f, 0.9f};
        std::memcpy(branchPos, positions, sizeof(positions));
        for (int i = 0; i < 6; ++i) {
            float along = branchPos[i];
            float newAmplitude = KeyAmplitude(power);
            float rest = 1.0f - along;
            Vector3 origin = along * from + rest * to;
            branches[i].Init(origin, to);
            FreshKeys(branches[i], time, newAmplitude);
            branches[i].width = (g_clientRandom.Below(0.8f) + 0.7f) * strength * 0.5f;
        }
    } else {
        main.width = 4.0f;
        for (int i = 0; i < 6; ++i) {
            float reach = std::sqrt(LengthSquared(from - to)) * 0.05f;
            float low = -reach;
            float offsetY = g_clientRandom.Range(low, reach);
            float offsetX = g_clientRandom.Range(low, reach);
            float offsetZ = g_clientRandom.Range(low, reach);
            float newAmplitude = KeyAmplitude(power);
            Vector3 offset = {offsetX, offsetY, offsetZ};
            branches[i].Init(offset + from, offset + to);
            FreshKeys(branches[i], time, newAmplitude);
            branches[i].width = g_clientRandom.Below(0.8f) * 3.5f + 2.45f;   // 2.45 .. 3.25
        }
    }

    Animate();
    UpdateBounds();
    bounds.boxMin -= Vector3{1.0f, 1.0f, 1.0f};
    bounds.boxMax += Vector3{1.0f, 1.0f, 1.0f};
    bounds.radius = Length(bounds.boxMax);
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
    bounds = ClipBounds{};
    Lightning::Destroy();
}

// The texture references are null after the reset (releasing them again is a no-op).
Lightning::~Lightning() { Lightning::Destroy(); }

const char* Lightning::ClassName() { return "CLightning"; }
ClassId Lightning::GetClassId() { return ClassId::Lightning; }

EngineObject* LightningManager::NewObject(int /*type*/) { return new Lightning(); }

// --- Load --------------------------------------------------------------------------------------------

// The lightning noise table's load (the lightning manager's Create calls it). Main thread. See docs/notes/objects.md.

// Opens <text folder>gfx.res and reads lightning.dat into the noise table, row by row through a memory stream
// (a short file leaves the rest zero-filled, as the stream's Read does). Returns 1; a missing archive or file ends
// the game.
int LoadLightningNoise() {
    MappedResource resource;
    MemoryStream local;
    resource.OpenResource(g_textFolder + "gfx.res");
    std::uint32_t size;
    void* data = resource.GetFile("lightning.dat", &size);
    if (!data) game::FatalExit("Unable to open file gfx.res\\lightning.dat");

    local.Fill(data, size);
    ByteStream* stream = &local;
    float* row = g_lightningNoise[0];
    for (int i = 0; i < kLightningNoiseRows; ++i, row += kLightningNoiseRowLength)
        stream->Read(row, kLightningNoiseRowLength * sizeof(float));
    resource.CloseFile(data);
    return 1;
}

// --- Draw --------------------------------------------------------------------------------------------

// Drawing a lightning bolt (Lightning's renderer virtual; called by DrawAll) and its
// glow: the segments as textured strips through the render device, then a
// glow: the segments as textured strips through the render device, then a
// the world's frame counter. Plain float maths (rule 7). See docs/notes/objects.md, "Lightning".

namespace {

// The glow's work arrays: the sprite records (zero filled) and the 16-byte points.

struct SpriteRecord {   // 8 bytes: colour, texture rectangle
    std::uint32_t colour = 0;
    const float* rect = nullptr;
};
std::vector<SpriteRecord> g_sprites;
std::vector<SpritePoint> g_glowPoints;

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
    device->SetTexture(0, glowTexture);

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
    float length = Length(towards);
    if (length != 0.0f) towards /= length;
    float scaled = position.x * 15.0f;
    int shift = std::lround(scaled);
    int frame = (OwningWorld()->tick + shift) % 15;
    for (int j = 0; j < 8; ++j) {
        float t = j * 0.125f;   // 1/8
        Vector3 along = towards * t * size;
        g_glowPoints[j] = {position + along, std::sqrt(1.0f - t * t) * size};
        SpriteRecord& s = g_sprites[j];
        s.colour = 0xA0FFFFFFu;
        s.rect = frames[frame % 15];
        frame += 2;
    }
    device->DrawSprites(0, g_glowPoints, &g_sprites);
}

// 1 (also when the bolt is not visible: off screen).
int Lightning::Draw(SceneRenderer* device) {
    EnsureTextureLoaded(texture);
    SceneRenderer* renderer = device;
    renderer->SetTexture(0, texture);
    const TransformPipeline::ClipResult visibility = renderer->pipeline->ClipTest(&bounds);
    if (visibility == TransformPipeline::ClipResult::Outside) return 1;
    const float zero = 0.0f;
    if (power >= zero) Animate();
    const float slant = 0.25f;
    if (power < zero) {    // negative: untextured lengthwise
        renderer->DrawStrip(main.points, main.width, visibility, 0.0f, 0.0f, slant);
        for (LightningSegment& b : branches) renderer->DrawStrip(b.points, b.width, visibility, 0.0f, 0.0f, slant);
    } else {
        float offset = RandomOffset();
        float span = Length(main.step) * std::ssize(main.points) * 0.6f;
        renderer->DrawStrip(main.points, main.width, visibility, span, offset, slant);
        for (LightningSegment& b : branches) {
            offset = RandomOffset();
            span = Length(b.step) * std::ssize(b.points) * 0.6f;
            renderer->DrawStrip(b.points, b.width, visibility, span, offset, slant);
        }
    }

    float count = static_cast<float>(std::ssize(main.points));
    Vector3 start = main.start;
    Vector3 run = count * main.step;
    Vector3 end = start + run;
    float size = std::sqrt(LengthSquared(start - end)) * 0.11f;
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
