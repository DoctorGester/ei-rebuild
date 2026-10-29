// The rain and snow draws of the world's rain / snow layer (weather_draw.cpp calls the one of the mode): both walk the 81 x 81
// cells (half a unit apart) around the point the wind carries a particle to the camera from; a cell's place and the
// world's time give its particle's cycle and fall, `density` of 64 cycles show one. See docs/notes/renderer.md, section 12.
// Kept as the game has it: the vertices' colour and specular are never written (what the buffer held stays, the distance
// fog only lowers the specular's alpha), the packets' w neither; the rain's height uses the snow's rise (kSnowRise; both
// are 40); a height that is not a number would loop forever in the rain's wrap.
#include "ai_motion.h"
#include "camera.h"
#include "clip.h"
#include "core_maths.h"
#include "effects.h"
#include "figure_mesh.h"
#include "link_figure.h"
#include "model.h"
#include "render/render.h"
#include "texture.h"
#include "weather_noise.h"
#include "world.h"

#include "lib/crt.h"
#include "lib/nan_audit.h"

#include "ei/x87.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace {

constexpr int kGridCells = 81;              // the cells along each side (the offsets array)
constexpr int kMaxPackets = 0x8000;         // room for the corners while they are built
constexpr int kDensitySteps = 0x3F;         // a cycle's low six bits against `density`
constexpr int kSeedCount = 32;

constexpr float kRainSink = 0.5f;                                   // added to the wind's fall
constexpr float kSnowSink = 0.1f;
static_assert(std::bit_cast<std::uint32_t>(kSnowSink) == 0x3DCCCCCDu);
constexpr float kRainCycle = 200.0f;                                // ticks
constexpr float kSnowCycle = 1000.0f;
constexpr float kCellsPerUnit = 2.0f;
constexpr float kJitterScale = 0.0078125f;                          // (1 / 128)
constexpr float kJitterHeight = 10.0f;
constexpr float kDropLength = 1.0f;
constexpr float kDropHalfWidth = 0.033f;
static_assert(std::bit_cast<std::uint32_t>(kDropHalfWidth) == 0x3D072B02u);
constexpr float kNearDepth = 4.0f;                                  // a drop nearer is not drawn
constexpr float kFrameWidth = 0.25f;                                // a frame of the texture
constexpr float kFrameMiddle = 0.125f;
constexpr float kRainTop = 0.75f, kRainBottom = 1.0f;               // the rain's rows of the texture
constexpr float kSnowTop = 0.5f;                                    // the snow's
constexpr float kSnowBottom = 0.74f;
static_assert(std::bit_cast<std::uint32_t>(kSnowBottom) == 0x3F3D70A4u);
constexpr float kFlakeWidth = 0.2f;
static_assert(std::bit_cast<std::uint32_t>(kFlakeWidth) == 0x3E4CCCCDu);
constexpr float kFlakeAbove = 0.3f;
static_assert(std::bit_cast<std::uint32_t>(kFlakeAbove) == 0x3E99999Au);
constexpr float kFlakeBelow = 0.7f;
static_assert(std::bit_cast<std::uint32_t>(kFlakeBelow) == 0x3F333333u);
constexpr float kSwayShift = 1000.0f;
constexpr double kSwayPeriod = 50.0;
constexpr int kSwayCells = 0x7C;

// Set by static initialisers.
constexpr float kCellSize = /*007AEDA8*/ 0.5f;
constexpr float kRainRise = /*007AEDA4*/ 40.0f;     // half a rain cycle's fall less 10: 0.5 * 200 * 0.5 - 10
constexpr float kSnowRise = /*007AEDA0*/ 40.0f;     // the same for snow: 0.1 * 1000 * 0.5 - 10
constexpr float kHalfHeight = /*007AED9C*/ 30.0f;   // the band around the camera: 40 / 2 + 10
constexpr float kFlakeHeight = /*007AED98*/ 0.2f;   // a copy of kFlakeWidth

// A number per (column squared + row) & 31, mixed into a cell's.
constexpr std::uint32_t kRainSeeds[kSeedCount] = /*0078DEA4*/ {
    0x11, 0x2B, 0x1C8, 0x3AE, 0x20, 0xEA, 0x361, 0x5F, 0x141, 0x2F, 0x38D, 0x11C, 0x21F, 0x18C, 0xC1, 0x78,
    0x62, 0x310, 0x279, 0xA, 0x103, 0x4D, 0x76, 0x42, 0x399, 0x351, 0x164, 0x50, 0x1DB, 0xEB, 0xD5, 0x33A,
};
constexpr std::uint32_t kSnowSeeds[kSeedCount] = /*0078DF24*/ {
    0x1529, 0x927, 0x8698, 0x328E, 0x2280, 0x2FCA, 0xB329, 0x20CB, 0xB109, 0x228F, 0x182C1, 0xD7F4, 0x12AFF, 0xB154,
    0x2FA1, 0x17F48, 0x1A8E, 0x137A8, 0x2D71, 0x164E, 0xD3F3, 0xD95, 0x13126, 0x4F2, 0x17E1D, 0x13C35, 0x8BCA, 0x224C,
    0x1328B, 0x11A2B, 0x85A5, 0x16A9A,
};


// The table's value at `at`, whose whole part (_ftol) is `cell`: 0 outside the table (as the game's helper,
// which the draw has inline; the cubic's value is left unrounded).
double Sway(const WeatherNoiseCurve& curve, int cell, float at) {
    if (cell < 0 || cell >= kSwayCells) return 0.0;
    const WeatherNoiseSegment& segment = curve.segments[cell];
    MotionCubic cubic{segment.value, segment.slope, segment.quadratic, segment.cubic};
    return cubic.Value(static_cast<float>(at - static_cast<float>(cell)));
}

// What both draws start from.
struct ParticleFrame {
    Vector3 wind;   // the layer's, falling `sink` faster
    float time;     // the world's tick and its fraction
    int baseX;      // the grid's middle cell
    int baseY;
    float low;      // the heights particles are kept between: the camera's -/+ HalfHeight, not below 0
    float high;
};

ParticleFrame Begin(RainSnow& layer, float sink, float rise) {
    ParticleFrame frame;
    frame.wind = layer.wind;
    frame.wind.z = static_cast<float>(layer.wind.z - sink);
    const WorldClient* world = layer.world;
    frame.time = static_cast<float>(static_cast<float>(world->tick) + world->tickFraction);
    const Vector3 eye = camera::FrameView().position;
    // The time a particle takes from `rise` above the camera, and where it starts from then.
    const float time = static_cast<float>((eye.z - rise) / frame.wind.z);
    frame.baseX = x87::fistp(static_cast<float>((eye.x - frame.wind.x * time) * kCellsPerUnit));
    frame.baseY = x87::fistp(static_cast<float>((eye.y - frame.wind.y * time) * kCellsPerUnit));
    layer.particleCount = 0;
    layer.positions.resize(kMaxPackets);
    const float below = static_cast<float>(eye.z - kHalfHeight);
    frame.low = below <= 0.0f ? 0.0f : below;
    frame.high = static_cast<float>(kHalfHeight + eye.z);
    return frame;
}

// The cycle's shift of a particle: -128 .. 127.
int Jitter(int cycle, int factor) {
    return static_cast<int>((static_cast<std::uint32_t>(cycle) * static_cast<std::uint32_t>(factor)) & 0xFF) - 128;
}
constexpr int kJitterX = -59, kJitterY = 67, kJitterZ = -127;

void Put(RainSnow& layer, float x, float y, float z) {
    const int index = layer.particleCount++;
    VertexPacket& packet = layer.positions[index >> 2];
    packet.block[0][index & 3] = x;
    packet.block[1][index & 3] = y;
    packet.block[2][index & 3] = z;
}

// The corners built so far projected with the top of the pipeline's stack 2, an outcode each.
void Project(RainSnow& layer, SceneRenderer* renderer) {
    layer.positions.resize((layer.particleCount + 3) >> 2);
    layer.projected.resize(std::ssize(layer.positions) * 4);
    layer.outcodes.resize(std::ssize(layer.positions) * 4);
    const TransformPipeline* pipeline = renderer->pipeline;
    pipeline->ProjectClipped(*pipeline->Top(TransformPipeline::kClipStack), layer.positions.data(),
                             layer.projected.data(), layer.outcodes.data(), std::ssize(layer.positions));
}

void SetCorner(TLVertex& vertex, const Vector4& projected) { std::memcpy(&vertex, &projected, sizeof(projected)); }

// The texture frame's coordinates on a particle's three vertices.
void SetFrame(TLVertex* triangle, std::uint8_t frame, float top, float bottom) {
    const float left = static_cast<float>(frame) * kFrameWidth;
    triangle[0].u = left;
    triangle[0].v = top;
    triangle[1].u = left + kFrameWidth;
    triangle[1].v = top;
    triangle[2].u = left + kFrameMiddle;
    triangle[2].v = bottom;
}

// The vertices kept (three per particle on screen) and taken to the screen.
int Finish(RainSnow& layer, SceneRenderer* renderer, int used) {
    layer.vertices.resize(used);
    if (used != 0) renderer->pipeline->ToScreen(layer.vertices.data(), std::ssize(layer.vertices));
    return used;
}

} // namespace

int RainSnow::BuildRain(SceneRenderer* renderer) {
    const ParticleFrame frame = Begin(*this, kRainSink, kRainRise);
    // The drop's tail: its length against the wind.
    const float length = LengthXYZ(frame.wind);
    const float tailX = static_cast<float>(-(frame.wind.x / length * kDropLength));
    const float tailY = static_cast<float>(-(frame.wind.y / length * kDropLength));

    int drops = 0;
    for (int i = 0; i < kGridCells; ++i) {
        const int a = offsets[i];
        const int row = frame.baseY + a;
        for (int j = 0; j < kGridCells; ++j) {
            const int b = offsets[j];
            const int col = frame.baseX + b;
            const std::uint32_t column = static_cast<std::uint32_t>(col), line = static_cast<std::uint32_t>(row);
            const std::uint32_t seed =
                (kRainSeeds[(column * column + line) & (kSeedCount - 1)] + column * 172u + line * 13u) & 0xFFFF;
            const float phase = static_cast<float>(static_cast<float>(static_cast<int>(seed)) + frame.time);
            const int cycle = x87::fistp(static_cast<float>(phase / kRainCycle));
            if ((cycle & kDensitySteps) >= density) continue;
            const float fall = phase - static_cast<float>(cycle) * kRainCycle;
            // The height, wrapped into the band around the camera.
            float z = fall * frame.wind.z + kSnowRise;
            if (z > frame.high) {
                const float band = kHalfHeight + kHalfHeight;
                do z -= band;
                while (z > frame.high);
            }
            EI_NAN_AUDIT(z, frame.low);
            if (z < frame.low) {
                const float band = kHalfHeight + kHalfHeight;
                do z += band;
                while (z < frame.low);   // ends on a NaN (the game's did not)
            }
            const float cell = kCellSize;
            const float x = static_cast<float>(static_cast<float>(col) * cell + fall * frame.wind.x +
                                       static_cast<float>(Jitter(cycle, kJitterX)) * cell * kJitterScale);
            const float y = static_cast<float>(static_cast<float>(row) * cell + fall * frame.wind.y +
                                       static_cast<float>(Jitter(cycle, kJitterY)) * cell * kJitterScale);
            const float bottom = static_cast<float>(Jitter(cycle, kJitterZ)) * kJitterHeight * kJitterScale + z;
            frames[drops] = static_cast<std::uint8_t>(cycle & 3);

            // The drop's base lies across the direction the cell is seen in, more or less.
            if (std::abs(b) > std::abs(a)) {
                const float side = b < 0 ? -kDropHalfWidth : kDropHalfWidth;
                Put(*this, x, y + side, bottom);
                Put(*this, x, y - side, bottom);
            } else {
                const float side = a > 0 ? -kDropHalfWidth : kDropHalfWidth;
                Put(*this, x + side, y, bottom);
                Put(*this, x - side, y, bottom);
            }
            Put(*this, x + tailX, y + tailY, bottom + kDropLength);
            ++drops;
        }
    }
    Project(*this, renderer);

    vertices.resize(particleCount);
    const std::uint8_t* frameOf = frames.data();
    int used = 0;
    for (int first = 0; first < particleCount; first += 3, ++frameOf) {
        const std::uint8_t* codes = outcodes.data() + first;
        if (codes[0] != 0 || codes[1] != 0 || codes[2] != 0) continue;
        const Vector4* corners = projected.data() + first;
        EI_NAN_AUDIT(corners[0].z);
        if (corners[0].z < kNearDepth) continue;
        TLVertex* triangle = vertices.data() + used;
        for (int corner = 0; corner < 3; ++corner) SetCorner(triangle[corner], corners[corner]);
        SetFrame(triangle, *frameOf, kRainTop, kRainBottom);
        used += 3;
    }
    return Finish(*this, renderer, used);
}

int RainSnow::BuildSnow(SceneRenderer* renderer) {
    const ParticleFrame frame = Begin(*this, kSnowSink, kSnowRise);

    int flakes = 0;
    for (int i = 0; i < kGridCells; ++i) {
        const int row = frame.baseY + offsets[i];
        for (int j = 0; j < kGridCells; ++j) {
            const int col = offsets[j] + frame.baseX;
            const std::uint32_t column = static_cast<std::uint32_t>(col), line = static_cast<std::uint32_t>(row);
            const std::uint32_t seed =
                (kSnowSeeds[(column * column + line) & (kSeedCount - 1)] - column * 30064u + line * 17843u) & 0xFFFF;
            const float phase = static_cast<float>(static_cast<float>(static_cast<int>(seed)) + frame.time);
            const int cycle = x87::fistp(static_cast<float>(phase / kSnowCycle));
            if ((cycle & kDensitySteps) >= density) continue;
            const float fall = static_cast<float>(phase - static_cast<float>(cycle) * kSnowCycle);
            float z = static_cast<float>(fall * frame.wind.z + kSnowRise);
            EI_NAN_AUDIT(z);
            if (z < frame.low) continue;
            if (z > frame.high) continue;
            const float cell = kCellSize;
            float x = static_cast<float>(static_cast<float>(col) * cell + fall * frame.wind.x +
                                 static_cast<float>(Jitter(cycle, kJitterX)) * cell * kJitterScale);
            float y = static_cast<float>(static_cast<float>(row) * cell + fall * frame.wind.y +
                                 static_cast<float>(Jitter(cycle, kJitterY)) * cell * kJitterScale);
            z = static_cast<float>(static_cast<float>(Jitter(cycle, kJitterZ)) * kJitterHeight * kJitterScale + z);

            // The sway: by the height on all three axes, and by the place along x and y.
            const int level = static_cast<int>(libcrt::Ftol(z));
            const float swayX = static_cast<float>(Sway(g_swayXByHeight, level, z));
            const float placeX = static_cast<float>(std::fmod(static_cast<float>(x + kSwayShift), kSwayPeriod));
            x = static_cast<float>(swayX + Sway(g_swayXByPlace, static_cast<int>(libcrt::Ftol(placeX)), placeX) + x);
            const float swayY = static_cast<float>(Sway(g_swayYByHeight, level, z));
            const float placeY = static_cast<float>(std::fmod(static_cast<float>(y + kSwayShift), kSwayPeriod));
            y = static_cast<float>(swayY + Sway(g_swayYByPlace, static_cast<int>(libcrt::Ftol(placeY)), placeY) + y);
            Put(*this, x, y, static_cast<float>(Sway(g_swayZByHeight, level, z) + z));
            frames[flakes] = static_cast<std::uint8_t>(cycle & 3);
            ++flakes;
        }
    }
    Project(*this, renderer);

    vertices.resize(particleCount * 3);
    const std::uint8_t* frameOf = frames.data();
    int used = 0;
    for (int flake = 0; flake < particleCount; ++flake, ++frameOf) {
        if (outcodes[flake] != 0) continue;
        TLVertex* triangle = vertices.data() + used;
        for (int corner = 0; corner < 3; ++corner) SetCorner(triangle[corner], projected[flake]);
        // A triangle around the point, in the projection's units.
        triangle[0].x = triangle[0].x - kFlakeWidth * 0.5f;
        triangle[1].x = triangle[1].x + kFlakeWidth * 0.5f;
        triangle[0].y = triangle[0].y - static_cast<float>(kFlakeHeight * kFlakeAbove);
        triangle[1].y = triangle[1].y - static_cast<float>(kFlakeHeight * kFlakeAbove);
        triangle[2].y = triangle[2].y + static_cast<float>(kFlakeHeight * kFlakeBelow);
        SetFrame(triangle, *frameOf, kSnowTop, kSnowBottom);
        used += 3;
    }
    return Finish(*this, renderer, used);
}

void RainSnow::DrawParticles(SceneRenderer* renderer, int vertexCount) {
    renderer->device->DrawTlTriangles(vertices.data(), std::ssize(vertices), indices.data(), vertexCount);
}

int RainSnow::DrawRain(SceneRenderer* renderer) {
    const int vertexCount = BuildRain(renderer);
    if (vertexCount != 0) DrawParticles(renderer, vertexCount);
    return 1;
}

int RainSnow::DrawSnow(SceneRenderer* renderer) {
    const int vertexCount = BuildSnow(renderer);
    if (vertexCount != 0) DrawParticles(renderer, vertexCount);
    return 1;
}
