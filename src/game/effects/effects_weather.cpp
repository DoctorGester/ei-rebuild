#include "engine/scene/clip.h"   // TLVertex
#include "engine/core/core_maths.h"
#include "game/effects/effects.h"
#include "engine/models/figure_mesh.h"   // VertexPacket
#include "game/app/singletons.h"
#include "engine/scene/scene_renderer.h"   // SceneRenderer
#include "engine/scene/pipeline.h"   // TransformPipeline
#include "engine/models/texture.h"
#include "engine/core/random.h"
#include "game/sound/sound.h"
#include "game/effects/weather_field.h"
#include "game/world/world.h"

#include "lib/bytes.h"
#include "lib/containers.h"
#include "lib/float_select.h"
#include "lib/maths.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

// Rain/snow fading and the visibility fog cells.

namespace {

constexpr float kFogCellOffset = 0.25f;
constexpr float kFadePi = 3.14159f;   // sic
constexpr float kVolumeScale = 100.0f;
constexpr float kDensityScale = 64.0f;
constexpr float kFogMinAlpha = 0.01f;
constexpr float kFar = 1e30f;

constexpr int kWeatherTextureKind = static_cast<int>(TextureKind::Effects);
constexpr const char* kWeatherTextureName = "RainSnow";
constexpr int kRainPriority = 1000;
constexpr int kFullDensity = 64;
constexpr const char* kRainSoundName = "nature\\rain\\circle.wav";

} // namespace

// --- Rain and snow ------------------------------------------------------------------------------

// Fades the density (and the rain loop's volume) along half a cosine from fadeStart over
// fadeDuration, towards targetMode; when done, switches mode and stops the sound if it is off.
void RainSnow::UpdateFade(float now) {
    // A NaN duration takes the last branch, which does the same as the first.
    if (fadeDuration == 0.0f) {   // no fade running
        mode = targetMode;
        if (targetMode == 1 && sound) g_clientWorld.sounds->SetSoundVolume(sound, 100, 0);
    } else if ((now - fadeStart) / fadeDuration <= 1.0f) {
        float c = lib::Cos((now - fadeStart) / fadeDuration * kFadePi);
        float level = (targetMode == 0 ? c + 1.0f : 1.0f - c) * 0.5f;
        if (sound) g_clientWorld.sounds->SetSoundVolume(sound, std::lround(level * kVolumeScale), 0);
        density = std::lround(level * kDensityScale);
        return;
    } else {
        mode = targetMode;
        if (targetMode == 1 && sound) g_clientWorld.sounds->SetSoundVolume(sound, 100, 0);
    }
    if (mode == 0 && sound) {
        g_clientWorld.sounds->StopSound(sound);
        sound = 0;
    }
    density = 64;
    fadeDuration = 0.0f;
}

void RainSnow::SetWind(const Vector3& direction, float strength) {
    wind = direction * strength;
}

void RainSnow::Clear() {
    if (sound) {
        g_clientWorld.sounds->StopSound(sound);
        sound = 0;
    }
    texture.Reset();
    ground.clear();
    groundOf = nullptr;
    wind = Vector3{0.0f, 0.0f, 0.0f};
    world = nullptr;
    carry = Vector3{0.0f, 0.0f, 0.0f};
    carrying = false;
    mode = 0;
    targetMode = 0;
    fadeStart = 0.0f;
    fadeDuration = 0.0f;
    density = kFullDensity;
}

void RainSnow::Start(int newMode, float duration) {
    if (sound && newMode) {
        g_clientWorld.sounds->StopSound(sound);
        sound = 0;
    }
    if (newMode == 1) sound = g_clientWorld.sounds->Play2D(kRainSoundName, kRainPriority, 0, 0, {.loop = true});
    targetMode = newMode;
    if (mode == 0) mode = newMode;
    fadeDuration = duration;
    fadeStart = static_cast<float>(world->tick);
}

RainSnow::RainSnow() {
    world = nullptr;
    Clear();
}

RainSnow::~RainSnow() { Clear(); }

const char* RainSnow::ClassName() { return "CWeather"; }
ClassId RainSnow::GetClassId() { return ClassId::RainSnow; }

// --- Visibility fog -----------------------------------------------------------------------------

// The bounding box (0.5 larger than the cell centres) and sphere of all cells, for culling.
void VisibilityFog::ComputeBounds() {
    bounds.boxMin = {kFar, kFar, kFar};
    bounds.boxMax = {-kFar, -kFar, -kFar};
    for (const auto& [key, cell] : cells) {
        bounds.boxMin = MinKeepA(cell.pos, bounds.boxMin);
        bounds.boxMax = MaxKeepB(bounds.boxMax, cell.pos);
    }
    const Vector3 margin = {0.5f, 0.5f, 0.5f};
    bounds.boxMax += margin;
    bounds.boxMin -= margin;
    bounds.center = (bounds.boxMax + bounds.boxMin) * 0.5f;
    bounds.boxMin -= bounds.center;
    bounds.boxMax -= bounds.center;
    bounds.radius = std::sqrt(LengthSquared(bounds.boxMax));
    bounds.flags = lib::Bits(BoundsFlag::Sphere | BoundsFlag::Box);
    boundsValid = 1;
}

// Per-frame fade: visible cells halve their distance to opaque, the others halve their alpha and
// are dropped below 0.01; every cell steps its texture tile. Returns whether the fog is still
// needed (referenced elsewhere or has cells).
bool VisibilityFog::RunTick() {
    std::vector<std::uint32_t> dead;
    for (auto& [key, cell] : cells) {
        if (cell.visible)
            cell.alpha = 1.0f - (1.0f - cell.alpha) * 0.5f;
        else
            cell.alpha *= 0.5f;
        if (cell.alpha < kFogMinAlpha) {
            dead.push_back(key);
        }
        cell.frame = static_cast<std::int8_t>((cell.frame + 1) % 15);
    }
    for (const std::uint32_t key : dead) cells.erase(key);
    return refCount > 1 || cells.size() != 0;
}

// Marks the cells of the visible squares (adding new ones, transparent, at a jittered point on
// the ground) and clears the mark on all others.
void VisibilityFog::Reveal(const FogGrid* grid) {
    for (auto& [key, cell] : cells) cell.visible = 0;
    for (int row = 0; row < grid->cells.height; ++row) {
        for (int col = 0; col < grid->cells.width; ++col) {
            if (grid->cells[row][col] == 0) continue;
            std::uint32_t key = ((grid->y0 + row) << 16) + grid->x0 + col;
            if (auto found = cells.find(key); found != cells.end()) {
                found->second.visible = 1;
                continue;
            }
            // The pair template is filled by an overlapping copy from the key, so every dword of the new value
            // starts as the key.
            std::array<std::uint32_t, sizeof(FogCell) / 4> words;
            words.fill(key);
            FogCell* cell = &cells.try_emplace(key, std::bit_cast<FogCell>(words)).first->second;
            cell->visible = 1;
            cell->x = static_cast<std::int16_t>(grid->x0 + col);
            cell->y = static_cast<std::int16_t>(grid->y0 + row);
            std::uint32_t r = g_clientRandom.Next();
            cell->pos.x = (ScaleWord(r, 0.5f) + cell->x - kFogCellOffset) * 0.5f;
            r = g_clientRandom.Next();
            cell->pos.y = (ScaleWord(r, 0.5f) + cell->y - kFogCellOffset) * 0.5f;
            float ground = OwningWorld()->SurfaceHeightAt(cell->pos.x, cell->pos.y);
            cell->alpha = 0.0f;
            cell->pos.z = ground + 0.5f;
            cell->frame = static_cast<std::int8_t>(g_clientRandom.Next() % 15);
            boundsValid = 0;
        }
    }
}

// ---- The fog object's virtuals ------------------------------------------------------------------

void VisibilityFog::ClearCells() {
    cells.clear();
    boundsValid = 0;
}

VisibilityFog::VisibilityFog() = default;

VisibilityFog::~VisibilityFog() {
    ClearCells();
}

void VisibilityFog::Create() {
    ClearCells();
}
const char* VisibilityFog::ClassName() { return "CVisibilityFog"; }
ClassId VisibilityFog::GetClassId() { return ClassId::VisibilityFog; }

// --- Draw --------------------------------------------------------------------------------------------

// The visibility fog's draw (its cells as sprites through the scene renderer) and the rain / snow layer's. See
// docs/notes/renderer.md (section 12).

// ---- The visibility fog ---------------------------------------------------------------------------------

namespace {

// A cell's sprite: its colour and its tile of the fog texture.
struct FogSprite {
    std::uint32_t colour = 0;
    const FogTile* tile = nullptr;
};

// The draw's work arrays: the sprites' centres and sizes and their records.
std::vector<SpritePoint> g_points;
std::vector<FogSprite> g_sprites;

constexpr int kWorkRecords = 0x1000;      // the arrays' first size
constexpr unsigned kBoundsCells = 12000;  // only a fog of more cells gets its bounds computed here
constexpr float kFogSpriteSize = 0.4f;
constexpr float kColourScale = 255.0f;

} // namespace

int VisibilityFog::Draw(SceneRenderer* renderer) {
    if (cells.size() == 0) return 1;
    if (cells.size() > kBoundsCells && !boundsValid) ComputeBounds();
    if (renderer->pipeline->ClipTest(&bounds) == TransformPipeline::ClipResult::Outside) return 1;

    g_sprites.resize(kWorkRecords);
    g_points.resize(kWorkRecords);
    g_sprites.resize(static_cast<int>(cells.size()));
    g_points.resize(static_cast<int>(cells.size()));

    int remaining = static_cast<int>(cells.size());
    SpritePoint* point = g_points.data();
    FogSprite* sprite = g_sprites.data();
    for (auto& node : cells) {
        if (--remaining < 0) break;
        const FogCell& cell = node.second;
        point->position = cell.pos;
        point->size = kFogSpriteSize;
        const int alpha = std::lround(cell.alpha * kColourScale);
        sprite->colour = (static_cast<std::uint32_t>(alpha) << 24) | 0x00FFFFFFu;
        sprite->tile = g_fogTiles + cell.frame;
        ++point;
        ++sprite;
    }
    return renderer->DrawSprites(0, g_points, &g_sprites);
}

// --- Weather draw ------------------------------------------------------------------------------------

// The weather's draw (called by the world view's effects pass with the renderer). Mode (0 none, 1 rain, 2 snow):
// the weather is stepped (with the world's tick) and the rain drawn on the white texture or the snow on the
// "RainSnow" texture's flake pictures (weather_particles.cpp). See docs/notes/effects.md and docs/notes/renderer.md. Returns 1 (also when there is
// nothing to draw), 0 without a world or with an unknown mode.
int RainSnow::Draw(SceneRenderer* renderer) {
    if (!world) return 0;
    if (mode == 0) return 1;
    UpdateFade(static_cast<float>(world->tick));
    if (mode == 0) return 1;
    if (mode != 1 && mode != 2) return 0;
    if (mode == 2) {
        if (!texture) LoadNamedTexture(texture, kWeatherTextureKind, kWeatherTextureName);
        EnsureTextureLoaded(texture);
        renderer->SetTexture(0, texture);
    } else {
        renderer->SetTexture(0, nullptr);
    }
    DrawParticles(renderer, mode == 1 ? weather::Kind::Rain : weather::Kind::Snow);
    return 1;
}
