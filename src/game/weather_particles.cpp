// The rain and snow draws of the world's rain / snow layer (A-S13): 004E4240 (rain) and 004E4C80
// (snow), thiscall with the scene renderer, RET 4, EAX 1; the layer's draw 004E5680
// (weather_draw.cpp) calls the one of the mode after the fade's step and the texture's bind.
//
// Both walk the 81 x 81 cells (half a unit apart) around the point the wind carries a particle to
// the camera from. A cell's place gives a 16-bit number; with the world's time it gives the cycle
// the cell's particle is in (a rain drop falls for 200 ticks, a snowflake for 1000) and how far it
// has fallen. `density` of 64 cycles show a particle; the cycle also shifts it within the cell and
// picks one of the texture's four frames. The corners go to the transform library as packets of
// four, the ones on screen become pre-transformed triangles drawn with the device's current state.
//
// Kept as the original has it: the vertices' colour and specular are never written (what the
// buffer held stays, the distance fog only lowers the specular's alpha), the packets' w neither;
// the rain's height uses the snow's rise (007AEDA0; both are 40); a height that is not a number
// would loop forever in the rain's wrap. See docs/notes/renderer.md, section 12.
#include "effects.h"

#include "ai_motion.h"
#include "camera.h"
#include "clip.h"
#include "core_maths.h"
#include "device.h"
#include "figure_mesh.h"
#include "link_figure.h"
#include "model.h"
#include "originals_library.h"
#include "texture.h"
#include "world.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "game_data.h"

#include <bit>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace {

constexpr int kGridCells = 81;              // the cells along each side (the offsets array)
constexpr int kMaxPackets = 0x8000;         // room for the corners while they are built
constexpr int kDensitySteps = 0x3F;         // a cycle's low six bits against `density`
constexpr int kSeedCount = 32;

constexpr float kRainSink = 0.5f;                                   // 0073D708 added to the wind's fall
constexpr float kSnowSink = std::bit_cast<float>(0x3DCCCCCDu);      // 0073D70C (0.1)
constexpr float kRainCycle = 200.0f;                                // 0073D700 ticks
constexpr float kSnowCycle = 1000.0f;                               // 0073D704
constexpr float kCellsPerUnit = 2.0f;                               // 0073D710
constexpr float kJitterScale = 0.0078125f;                          // 0073D730 (1 / 128)
constexpr float kJitterHeight = 10.0f;                              // 0073D714
constexpr float kDropLength = 1.0f;                                 // 0073D718
constexpr float kDropHalfWidth = std::bit_cast<float>(0x3D072B02u); // 0073D71C (0.033)
constexpr float kNearDepth = 4.0f;                                  // 0073BAA0 a drop nearer is not drawn
constexpr float kFrameWidth = 0.25f;                                // 0073C3B0 a frame of the texture
constexpr float kFrameMiddle = 0.125f;                              // 0073C520
constexpr float kRainTop = 0.75f, kRainBottom = 1.0f;               // the rain's rows of the texture
constexpr float kSnowTop = 0.5f;                                    // the snow's
constexpr float kSnowBottom = std::bit_cast<float>(0x3F3D70A4u);    // 0.74
constexpr float kFlakeWidth = std::bit_cast<float>(0x3E4CCCCDu);    // 0073D720 (0.2)
constexpr float kHalf = 0.5f;                                       // 0073B86C
constexpr float kFlakeAbove = std::bit_cast<float>(0x3E99999Au);    // 0073C54C (0.3)
constexpr float kFlakeBelow = std::bit_cast<float>(0x3F333333u);    // 0073C538 (0.7)
constexpr float kSwayShift = 1000.0f;                               // 0073C34C
constexpr double kSwayPeriod = 50.0;                                // 0073D0A0
constexpr int kSwayCells = 0x7C;

// Set by static initialisers (004E38A0 .. 004E3920).
float CellSize() { return ei::GameData<const float>(0x007AEDA8); }     // 0.5
float RainRise() { return ei::GameData<const float>(0x007AEDA4); }     // 40: half a rain cycle's fall less 10
float SnowRise() { return ei::GameData<const float>(0x007AEDA0); }     // 40: the same for snow
float HalfHeight() { return ei::GameData<const float>(0x007AED9C); }   // 30: the band around the camera
float FlakeHeight() { return ei::GameData<const float>(0x007AED98); }  // 0.2

// A number per (column squared + row) & 31, mixed into a cell's.
const std::uint32_t* RainSeeds() { return reinterpret_cast<const std::uint32_t*>(0x0078DEA4); }
const std::uint32_t* SnowSeeds() { return reinterpret_cast<const std::uint32_t*>(0x0078DF24); }

// A snowflake's sway (0xA00 bytes; five tables built at start-up by 004E3980): 128 smoothed random
// values and the cubic between each two neighbours.
struct SwayTable {
    float values[128];
    MotionCubic segments[128];
};
static_assert(sizeof(SwayTable) == 0xA00);
SwayTable& SwayXByHeight() { return ei::GameData<SwayTable>(0x007AE398); }
SwayTable& SwayXByPlace() { return ei::GameData<SwayTable>(0x007AC598); }
SwayTable& SwayYByHeight() { return ei::GameData<SwayTable>(0x007AD998); }
SwayTable& SwayYByPlace() { return ei::GameData<SwayTable>(0x007ABB98); }
SwayTable& SwayZByHeight() { return ei::GameData<SwayTable>(0x007ACF98); }

// The table's value at `at`, whose whole part (_ftol) is `cell`: 0 outside the table (as 004E3930,
// which the draw has inline; the cubic's value 004E5660 is left unrounded).
double Sway(SwayTable& table, int cell, float at) {
    if (cell < 0 || cell >= kSwayCells) return 0.0;
    return table.segments[cell].Value(static_cast<float>(at - static_cast<float>(cell)));
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

ParticleFrame Begin(CRainSnow& layer, float sink, float rise) {
    ParticleFrame frame;
    frame.wind = layer.wind;
    frame.wind.z = static_cast<float>(layer.wind.z - sink);
    const CWorldClient* world = static_cast<const CWorldClient*>(layer.world);
    frame.time = static_cast<float>(static_cast<float>(world->tick_) + world->tickFraction_);
    const Vector3 eye = world->camera_->localPosition_;
    // The time a particle takes from `rise` above the camera, and where it starts from then.
    const float time = static_cast<float>((eye.z - rise) / frame.wind.z);
    frame.baseX = x87::fistp(static_cast<float>((eye.x - frame.wind.x * time) * kCellsPerUnit));
    frame.baseY = x87::fistp(static_cast<float>((eye.y - frame.wind.y * time) * kCellsPerUnit));
    layer.particleCount = 0;
    layer.positions.SetSize(kMaxPackets);
    const float below = static_cast<float>(eye.z - HalfHeight());
    frame.low = !(below <= 0.0f) ? below : 0.0f;
    frame.high = static_cast<float>(HalfHeight() + eye.z);
    return frame;
}

// The cycle's shift of a particle: -128 .. 127.
int Jitter(int cycle, int factor) {
    return static_cast<int>((static_cast<std::uint32_t>(cycle) * static_cast<std::uint32_t>(factor)) & 0xFF) - 128;
}
constexpr int kJitterX = -59, kJitterY = 67, kJitterZ = -127;

void Put(CRainSnow& layer, float x, float y, float z) {
    const int index = layer.particleCount++;
    VertexPacket& packet = layer.positions.data[index >> 2];
    packet.block[0][index & 3] = x;
    packet.block[1][index & 3] = y;
    packet.block[2][index & 3] = z;
}

// The corners built so far projected with the top of the pipeline's stack 2, an outcode each.
void Project(CRainSnow& layer, CSceneRenderer* renderer) {
    layer.positions.SetSize((layer.particleCount + 3) >> 2);
    layer.projected.SetSizeDiscard(layer.positions.size * 4);
    layer.outcodes.SetSize(layer.positions.size * 4, -1);
    CTransformPipeline* pipeline = renderer->pipeline_;
    pipeline->transform_->ProjectClipped(pipeline->Top(CTransformPipeline::kClipStack), layer.positions.data,
                                         layer.projected.data, layer.outcodes.data, layer.positions.size);
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
int Finish(CRainSnow& layer, CSceneRenderer* renderer, int used) {
    layer.vertices.SetSizeDiscard(used);
    if (used != 0) renderer->pipeline_->transform_->ToScreen(layer.vertices.data, layer.vertices.size);
    return used;
}

} // namespace

int CRainSnow::BuildRain(CSceneRenderer* renderer) {
    const ParticleFrame frame = Begin(*this, kRainSink, RainRise());
    // The drop's tail: its length against the wind.
    const float length = LengthXYZ(frame.wind);
    const float tailX = static_cast<float>(-(frame.wind.x / length * kDropLength));
    const float tailY = static_cast<float>(-(frame.wind.y / length * kDropLength));

    int drops = 0;
    for (int i = 0; i < kGridCells; ++i) {
        const int a = offsets.data[i];
        const int row = frame.baseY + a;
        for (int j = 0; j < kGridCells; ++j) {
            const int b = offsets.data[j];
            const int col = frame.baseX + b;
            const std::uint32_t column = static_cast<std::uint32_t>(col), line = static_cast<std::uint32_t>(row);
            const std::uint32_t seed =
                (RainSeeds()[(column * column + line) & (kSeedCount - 1)] + column * 172u + line * 13u) & 0xFFFF;
            const float phase = static_cast<float>(static_cast<float>(static_cast<int>(seed)) + frame.time);
            const int cycle = x87::fistp(static_cast<float>(phase / kRainCycle));
            if ((cycle & kDensitySteps) >= density) continue;
            const float fall = phase - static_cast<float>(cycle) * kRainCycle;
            // The height, wrapped into the band around the camera.
            float z = fall * frame.wind.z + SnowRise();
            if (z > frame.high) {
                const float band = HalfHeight() + HalfHeight();
                do z -= band;
                while (z > frame.high);
            }
            if (!(z >= frame.low)) {
                const float band = HalfHeight() + HalfHeight();
                do z += band;
                while (!(z >= frame.low));
            }
            const float cell = CellSize();
            const float x = static_cast<float>(static_cast<float>(col) * cell + fall * frame.wind.x +
                                       static_cast<float>(Jitter(cycle, kJitterX)) * cell * kJitterScale);
            const float y = static_cast<float>(static_cast<float>(row) * cell + fall * frame.wind.y +
                                       static_cast<float>(Jitter(cycle, kJitterY)) * cell * kJitterScale);
            const float bottom = static_cast<float>(Jitter(cycle, kJitterZ)) * kJitterHeight * kJitterScale + z;
            frames.data[drops] = static_cast<std::uint8_t>(cycle & 3);

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

    vertices.SetSizeDiscard(particleCount);
    const std::uint8_t* frameOf = frames.data;
    int used = 0;
    for (int first = 0; first < particleCount; first += 3, ++frameOf) {
        const std::uint8_t* codes = outcodes.data + first;
        if (codes[0] != 0 || codes[1] != 0 || codes[2] != 0) continue;
        const Vector4* corners = projected.data + first;
        if (!(corners[0].z >= kNearDepth)) continue;
        TLVertex* triangle = vertices.data + used;
        for (int corner = 0; corner < 3; ++corner) SetCorner(triangle[corner], corners[corner]);
        SetFrame(triangle, *frameOf, kRainTop, kRainBottom);
        used += 3;
    }
    return Finish(*this, renderer, used);
}

int CRainSnow::BuildSnow(CSceneRenderer* renderer) {
    const ParticleFrame frame = Begin(*this, kSnowSink, SnowRise());

    int flakes = 0;
    for (int i = 0; i < kGridCells; ++i) {
        const int row = frame.baseY + offsets.data[i];
        for (int j = 0; j < kGridCells; ++j) {
            const int col = offsets.data[j] + frame.baseX;
            const std::uint32_t column = static_cast<std::uint32_t>(col), line = static_cast<std::uint32_t>(row);
            const std::uint32_t seed =
                (SnowSeeds()[(column * column + line) & (kSeedCount - 1)] - column * 30064u + line * 17843u) & 0xFFFF;
            const float phase = static_cast<float>(static_cast<float>(static_cast<int>(seed)) + frame.time);
            const int cycle = x87::fistp(static_cast<float>(phase / kSnowCycle));
            if ((cycle & kDensitySteps) >= density) continue;
            const float fall = static_cast<float>(phase - static_cast<float>(cycle) * kSnowCycle);
            float z = static_cast<float>(fall * frame.wind.z + SnowRise());
            if (!(z >= frame.low)) continue;
            if (z > frame.high) continue;
            const float cell = CellSize();
            float x = static_cast<float>(static_cast<float>(col) * cell + fall * frame.wind.x +
                                 static_cast<float>(Jitter(cycle, kJitterX)) * cell * kJitterScale);
            float y = static_cast<float>(static_cast<float>(row) * cell + fall * frame.wind.y +
                                 static_cast<float>(Jitter(cycle, kJitterY)) * cell * kJitterScale);
            z = static_cast<float>(static_cast<float>(Jitter(cycle, kJitterZ)) * kJitterHeight * kJitterScale + z);

            // The sway: by the height on all three axes, and by the place along x and y.
            const int level = static_cast<int>(gamecrt::Ftol(z));
            const float swayX = static_cast<float>(Sway(SwayXByHeight(), level, z));
            const float placeX = static_cast<float>(libcrt::StackFmod(static_cast<float>(x + kSwayShift), kSwayPeriod));
            x = static_cast<float>(swayX + Sway(SwayXByPlace(), static_cast<int>(gamecrt::Ftol(placeX)), placeX) + x);
            const float swayY = static_cast<float>(Sway(SwayYByHeight(), level, z));
            const float placeY = static_cast<float>(libcrt::StackFmod(static_cast<float>(y + kSwayShift), kSwayPeriod));
            y = static_cast<float>(swayY + Sway(SwayYByPlace(), static_cast<int>(gamecrt::Ftol(placeY)), placeY) + y);
            Put(*this, x, y, static_cast<float>(Sway(SwayZByHeight(), level, z) + z));
            frames.data[flakes] = static_cast<std::uint8_t>(cycle & 3);
            ++flakes;
        }
    }
    Project(*this, renderer);

    vertices.SetSizeDiscard(particleCount * 3);
    const std::uint8_t* frameOf = frames.data;
    int used = 0;
    for (int flake = 0; flake < particleCount; ++flake, ++frameOf) {
        if (outcodes.data[flake] != 0) continue;
        TLVertex* triangle = vertices.data + used;
        for (int corner = 0; corner < 3; ++corner) SetCorner(triangle[corner], projected.data[flake]);
        // A triangle around the point, in the projection's units.
        triangle[0].x = triangle[0].x - kFlakeWidth * kHalf;
        triangle[1].x = triangle[1].x + kFlakeWidth * kHalf;
        triangle[0].y = triangle[0].y - FlakeHeight() * kFlakeAbove;
        triangle[1].y = triangle[1].y - FlakeHeight() * kFlakeAbove;
        triangle[2].y = triangle[2].y + FlakeHeight() * kFlakeBelow;
        SetFrame(triangle, *frameOf, kSnowTop, kSnowBottom);
        used += 3;
    }
    return Finish(*this, renderer, used);
}

void CRainSnow::DrawParticles(CSceneRenderer* renderer, int vertexCount) {
    CTextureDevice* device = renderer->device_;
    TLVertex* data = vertices.data;
    const int count = vertices.size;
    if (device->distanceFog_) device->pipeline_->transform_->FogVertices(data, count);
    gameapi::EnterCriticalSection(device::Lock());
    device->lastLoadResult_ = device::DrawIndexedPrimitive(device::D3D(device), 
        D3DPT_TRIANGLELIST, D3DFVF_TLVERTEX, data, static_cast<DWORD>(count), indices.data, static_cast<DWORD>(vertexCount), 0);
    gameapi::LeaveCriticalSection(device::Lock());
    device->vertexCount_ += count;
    device->primitiveCount_ += static_cast<int>(static_cast<unsigned>(vertexCount) / 3u);
    device->drawCount_ += 1;
}

int CRainSnow::DrawRain(CSceneRenderer* renderer) {
    const int vertexCount = BuildRain(renderer);
    if (vertexCount != 0) DrawParticles(renderer, vertexCount);
    return 1;
}
EI_REPLACE_METHOD(0x004E4240, CRainSnow::DrawRain);

int CRainSnow::DrawSnow(CSceneRenderer* renderer) {
    const int vertexCount = BuildSnow(renderer);
    if (vertexCount != 0) DrawParticles(renderer, vertexCount);
    return 1;
}
EI_REPLACE_METHOD(0x004E4C80, CRainSnow::DrawSnow);
