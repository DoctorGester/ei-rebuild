// The landscape's per-frame work: the update (Terrain::RunTick), the sun and its staggered relighting
// (SetSun), the sectors' relight countdowns, animated tiles (SectorData::AnimateTiles) and water waves
// (SectorData::AnimateWaves). Waves: every water vertex has a phase offset (owner wavePhases); the phase along
// the wave direction runs from the sector's first water vertex (mod 2 pi) and steps by the direction per
// vertex. The 512-entry sine table at that phase lifts the vertex (by waveHeight times the material's
// priority) and gives the crest its specular colour; materials of kind kKeepsGroundXY move the vertex sideways
// (only where its depth is negative), the others up and in a circle driven by the world time (its sines per
// column and row once per call: CircleSines).
#include "terrain.h"
#include "terrain_sector.h"
#include "game_singletons.h"
#include "random.h"
#include "render/render.h"
#include "spell.h"
#include "world.h"

#include "ei/x87.h"
#include "lib/float_select.h"
#include "lib/vecmath.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace terrain {
namespace {

// Table entries per radian: 256 / pi (doubles) as the division leaves it at 24-bit precision (not the folded
// double).
constexpr float kSineIndex = static_cast<float>(256.0 / 3.141592653589793);
constexpr float kQuarterTurn = 0.785398126f;
constexpr float kTimeScale = 0.05f;                        // the circling waves' speed per world tick
constexpr float kGlowScale = 50.0f;
constexpr float kRiseScale = 0.25f;
constexpr float kCircleScale = 3.0f;
constexpr int kTileFrameEvery = 3;                         // animated tiles step every third frame
constexpr int kAlwaysSteps = 1000;                         // a tile speed that steps every time

// The sine table entry at `phase` radians: (phase * 256 / pi) stored as a float and rounded to nearest, mod 512.
inline float Sine(const Terrain* owner, double phase) {
    return owner->sine[x87::fistp(static_cast<float>(phase * kSineIndex)) & 0x1FF];
}

inline std::uint32_t ColourByte(double v) { return static_cast<std::uint32_t>(x87::fistp(static_cast<float>(v))) & 0xFF; }

// The circling waves' sine per column (and per row: the same values) at the world time, once per call.
void CircleSines(const Terrain* owner, float* sines) {
    float time = static_cast<float>((g_clientWorld.tick_ + g_clientWorld.tickFraction_) * kTimeScale);
    for (int i = 0; i < kSectorRow; ++i) sines[i] = Sine(owner, i * kQuarterTurn + time);
}

// The relight order, (sy, sx) of the sectors. The cache holds one sector per grid cell (Terrain::SectorAt),
// so no two are equal and any sort gives the same order.
bool SectorBefore(const SectorData* x, const SectorData* y) {
    if (x->sy != y->sy) return x->sy < y->sy;
    return x->sx < y->sx;
}

} // namespace

void SectorData::Clear() {
    records.clear();
    for (int quarter = 0; quarter < 4; ++quarter) {
        for (std::vector<std::uint8_t>& flags : tileFlags[quarter]) flags.clear();
        for (std::vector<TileVertex>& list : tiles[quarter]) list.clear();
    }
    sx = sy = -1;
    detail = 0;
    seamAfter = seamBefore = false;
    hasWater = false;
    hasWaterTiles = false;
    data = nullptr;
    owner = nullptr;
    built = SectorStage::None;
    lastUsed = 0;
    relightCountdown = 0;
    clipMode = kCulled;
    render::TerrainChanged(this, render::TerrainChange::Rebuilt);
}

void SectorData::ScheduleRelight(int frames) {
    if (relightCountdown < 0 || frames < relightCountdown) relightCountdown = frames;
}

void SectorData::RelightTick() {
    if (relightCountdown < 0) return;
    if (--relightCountdown <= 0) {
        relightCountdown = -1;
        built &= ~SectorStage::Lit;
    }
}

void SectorData::AnimateTiles(int frame) {
    if (frame % kTileFrameEvery != 0 || !lib::Has(owner->landscapeFlags, LandscapeFlag::AnimatedTiles)) return;
    bool stepped = false;
    for (AnimatedTile& tile : animated) {
        if (tile.frame == 0 && tile.speed < kAlwaysSteps &&
            g_random.Next() % kAlwaysSteps + 1 > static_cast<std::uint32_t>(tile.speed)) {
            continue;
        }
        tile.frame = (tile.frame + 1) % tile.frames;
        stepped = true;
        const TileUv& uv = owner->TileUvAt(tile.rotation, tile.frameBase + tile.frame, 0);
        for (int k = 0; k < TileUv::kVertices; ++k) {
            tile.vertices[k].u = uv.u[k];
            tile.vertices[k].v = 1.0f - uv.v[k];
        }
    }
    if (stepped) render::TerrainChanged(this, render::TerrainChange::Moved);   // the pages kept on the GPU
}

void SectorData::AnimateWaves() {
    if (!hasWater || !lib::Has(owner->landscapeFlags, LandscapeFlag::Waves)) return;

    SectorPosition* positions = &vertices[kSectorVertices];
    SectorVertex* water = &records[kSectorVertices];
    const float* phaseOffset = owner->wavePhases.data();
    Vector2 direction = {owner->waveDirection.x, owner->waveDirection.y};
    float height = owner->waveHeight;
    float phase = owner->wavePhase;
    float fullTurn = static_cast<float>(kPi + kPi);
    Vector2 first = {positions[0].x, positions[0].y};
    float rowStart = static_cast<float>(std::fmod(Dot2D(direction, first) + fullTurn, static_cast<double>(fullTurn)));

    int lastMaterial = -1;
    float lastTurn = 0.0f;
    float amplitude = 0.0f;
    int kind = 0;
    Vector3 crest = {0.0f, 0.0f, 0.0f};
    float circle[kSectorRow];
    bool circling = false;
    for (int row = 0; row < kSectorRow; ++row) {
        float along = rowStart;
        for (int column = 0; column < kSectorRow; ++column, ++phaseOffset) {
            SectorVertex& record = water[row * kSectorRow + column];
            SectorPosition& position = positions[row * kSectorRow + column];
            int material = record.waterMaterial;
            if (material != SectorVertex::kNoWaterMaterial) {
                if (material != lastMaterial) {
                    const TerrainMaterial& m = owner->materials[material];
                    amplitude = static_cast<float>(height * m.priority);
                    kind = m.kind;
                    double glow = owner->waveGlow * m.priority * kGlowScale;
                    crest = {static_cast<float>(glow * m.waveColour.x), static_cast<float>(glow * m.waveColour.y),
                             static_cast<float>(glow * m.waveColour.z)};
                    double turn = std::fmod(static_cast<double>(phase * m.priority), 2.0 * kPi);
                    float delta = static_cast<float>(turn - lastTurn);
                    lastTurn = static_cast<float>(turn);
                    rowStart = static_cast<float>(delta + rowStart);
                    along = static_cast<float>(delta + along);
                    lastMaterial = material;
                }
                float wave = Sine(owner, along + *phaseOffset);
                if (!SignBit(wave)) {
                    record.specular = PackArgb(record.specular >> 24, ColourByte(wave * crest.x), ColourByte(wave * crest.y),
                                               ColourByte(wave * crest.z));
                } else {
                    record.specular &= 0xFF000000u;
                }
                double lift = wave * amplitude;
                if (kind == TerrainMaterial::kKeepsGroundXY) {
                    if (SignBit(record.depth)) {
                        position.x = static_cast<float>(lift + record.position.x);
                        position.y = static_cast<float>(lift + record.position.y);
                    }
                } else {
                    position.z = static_cast<float>(lift * kRiseScale + record.position.z);
                    if (!circling) {
                        CircleSines(owner, circle);
                        circling = true;
                    }
                    position.x = circle[column] * amplitude * kCircleScale + record.position.x;
                    position.y = circle[row] * amplitude * kCircleScale + record.position.y;
                }
            }
            along = static_cast<float>(along + direction.x);
        }
        rowStart = static_cast<float>(rowStart + direction.y);
    }
    render::TerrainChanged(this, render::TerrainChange::WaterMoved);
}

void Terrain::SetSun(CLight* light, const Vector4* colour) {
    ambient = *colour;
    sun = light;
    if (lightInterval <= 0) {
        for (SectorData* sector : sectorCache) sector->built &= ~SectorStage::Lit;
        return;
    }
    // The lit sectors, sorted, get relight countdowns spread evenly over the next interval.
    std::vector<SectorData*> sorted;
    for (SectorData* sector : sectorCache)
        if (lib::Has(sector->built, SectorStage::Lit)) sorted.push_back(sector);
    std::sort(sorted.begin(), sorted.end(), SectorBefore);
    if (sorted.empty()) return;
    float step = static_cast<float>(static_cast<double>(lightInterval - 1) / std::ssize(sorted));
    float at = 0.0f;
    for (SectorData* sector : sorted) {
        int frames = x87::fistp(at);
        at = static_cast<float>(at + step);
        sector->ScheduleRelight(frames);
        if (frames == 0) sector->built &= ~SectorStage::Lit;
    }
}

bool Terrain::RunTick() {
    if (lightCountdown > 0 && --lightCountdown == 0) {
        lightCountdown = lightInterval;
        SetSun(sun, &ambient);
    }
    for (SectorData* sector : sectorCache)
        if (lib::Has(sector->built, SectorStage::Lit)) sector->RelightTick();
    if (lib::Has(landscapeFlags, LandscapeFlag::Ambience)) PlayAmbience();
    wavePhase = static_cast<float>(waveSpeed + wavePhase);
    ++frame;
    for (int sy = visible.firstY; sy <= visible.lastY; ++sy) {
        for (int sx = visible.firstX; sx <= visible.lastX; ++sx) {
            SectorData* sector = SectorAt(sx, sy, SectorStage::Lit, false);
            if (sector && sector->IsDrawn()) {
                sector->AnimateTiles(frame);
                sector->AnimateWaves();
            }
        }
    }
    return true;
}

} // namespace terrain
