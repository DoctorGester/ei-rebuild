#include "core_maths.h"

#include "clip.h"   // TLVertex
#include "effects.h"
#include "figure_mesh.h"   // VertexPacket
#include "game_singletons.h"
#include "model.h"         // SceneRenderer, TransformPipeline
#include "random.h"
#include "sound.h"
#include "texture.h"
#include "world.h"
#include "lib/bytes.h"
#include "lib/containers.h"
#include "lib/float_select.h"
#include "lib/nan_audit.h"

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

constexpr float kQuarter = 0.25f;
constexpr float kFadePi = 3.14159f;   // sic
constexpr float kVolumeScale = 100.0f;
constexpr float kDensityScale = 64.0f;
constexpr float kFogMinAlpha = 0.01f;
static_assert(std::bit_cast<std::uint32_t>(kFogMinAlpha) == 0x3C23D70Au);
constexpr float kFogJitter = 1.1641532e-10f;   // 2^-33: a fog cell centre's random jitter
constexpr float kFar = 1e30f;
static_assert(std::bit_cast<std::uint32_t>(kFar) == 0x7149F2CAu);

constexpr int kWeatherTextureKind = static_cast<int>(TextureKind::Effects);
constexpr int kRainPriority = 1000;
constexpr int kFullDensity = 64;
const char* WeatherTextureName() { return "RainSnow"; }
const char* RainSoundName() { return "nature\\rain\\circle.wav"; }

} // namespace

// --- Rain and snow ------------------------------------------------------------------------------

// Fades the density (and the rain loop's volume) along half a cosine from fadeStart over
// fadeDuration, towards targetMode; when done, switches mode and stops the sound if it is off.
void RainSnow::UpdateFade(float now) {
    // A NaN duration takes the last branch, which does the same as the first.
    EI_NAN_AUDIT(now, fadeStart);
    if (fadeDuration == 0.0f) {   // no fade running
        mode = targetMode;
        if (targetMode == 1 && sound) g_clientWorld.sounds->SetSoundVolume(sound, 100, 0);
    } else if ((now - fadeStart) / fadeDuration <= 1.0f) {
        float c = std::cos((now - fadeStart) / fadeDuration * kFadePi);
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
    float x = strength * direction.x;
    float y = strength * direction.y;
    float z = strength * direction.z;
    wind.x = x;
    wind.y = y;
    wind.z = z;
}

void RainSnow::Clear() {
    if (sound) {
        g_clientWorld.sounds->StopSound(sound);
        sound = 0;
    }
    texture.Reset();
    wind = Vector3{0.0f, 0.0f, 0.0f};
    world = nullptr;
    particleCount = 0;
    mode = 0;
    targetMode = 0;
    fadeStart = 0.0f;
    fadeDuration = 0.0f;
    density = kFullDensity;
}

void RainSnow::Start(int newMode, float duration) {
    LoadNamedTexture(texture, kWeatherTextureKind, WeatherTextureName());
    if (sound && newMode) {
        g_clientWorld.sounds->StopSound(sound);
        sound = 0;
    }
    if (newMode == 1) sound = g_clientWorld.sounds->Play2D(RainSoundName(), kRainPriority, 0, 0, newMode, 0, 0, 0);
    targetMode = newMode;
    if (mode == 0) mode = newMode;
    fadeDuration = duration;
    fadeStart = static_cast<float>(world->tick);
}

RainSnow::RainSnow() {

    world = nullptr;
    sound = 0;
    Clear();
}

RainSnow::~RainSnow() {
    Clear();

    texture.Reset();
}

const char* RainSnow::ClassName() { return "CWeather"; }
ClassId RainSnow::GetClassId() { return ClassId::RainSnow; }

// --- Visibility fog -----------------------------------------------------------------------------

// The bounding box (0.5 larger than the cell centres) and sphere of all cells, for culling.
void VisibilityFog::ComputeBounds() {
    bounds.boxMin = {kFar, kFar, kFar};
    bounds.boxMax = {-kFar, -kFar, -kFar};
    for (const auto& [key, cell] : cells) {
        const Vector3& p = cell.pos;
        bounds.boxMin.x = MinKeepA(p.x, bounds.boxMin.x);
        bounds.boxMin.y = MinKeepA(p.y, bounds.boxMin.y);
        bounds.boxMin.z = MinKeepA(p.z, bounds.boxMin.z);
        bounds.boxMax.x = MaxKeepB(bounds.boxMax.x, p.x);
        bounds.boxMax.y = MaxKeepB(bounds.boxMax.y, p.y);
        bounds.boxMax.z = MaxKeepB(bounds.boxMax.z, p.z);
    }
    bounds.boxMax.x = bounds.boxMax.x + 0.5f;
    bounds.boxMax.y = bounds.boxMax.y + 0.5f;
    bounds.boxMax.z = bounds.boxMax.z + 0.5f;
    bounds.boxMin.x = bounds.boxMin.x - 0.5f;
    bounds.boxMin.y = bounds.boxMin.y - 0.5f;
    bounds.boxMin.z = bounds.boxMin.z - 0.5f;
    float sy = bounds.boxMin.y + bounds.boxMax.y;
    float sz = bounds.boxMin.z + bounds.boxMax.z;
    float cx = (bounds.boxMax.x + bounds.boxMin.x) * 0.5f;
    float cy = sy * 0.5f;
    float cz = sz * 0.5f;
    bounds.center.x = cx;
    bounds.center.y = cy;
    bounds.center.z = cz;
    bounds.boxMin.x = bounds.boxMin.x - cx;
    bounds.boxMin.y = bounds.boxMin.y - cy;
    bounds.boxMin.z = bounds.boxMin.z - cz;
    bounds.boxMax.x = bounds.boxMax.x - cx;
    bounds.boxMax.y = bounds.boxMax.y - cy;
    bounds.boxMax.z = bounds.boxMax.z - cz;
    float x = bounds.boxMax.x;
    float y = bounds.boxMax.y;
    float z = bounds.boxMax.z;
    boundsSpare = {0.0f, 0.0f, 0.0f};
    bounds.radius = std::sqrt(LengthSquaredUnrounded<Sum3::XYZ>(x, y, z));
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
            cell.alpha = cell.alpha * 0.5f;
        EI_NAN_AUDIT(cell.alpha);
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
            cell->pos.x = (WordToFloat(r) * kFogJitter + cell->x - kQuarter) * 0.5f;
            r = g_clientRandom.Next();
            cell->pos.y = (WordToFloat(r) * kFogJitter + cell->y - kQuarter) * 0.5f;
            float ground = static_cast<float>(OwningWorld()->SurfaceHeightAt(cell->pos.x, cell->pos.y));
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

VisibilityFog::VisibilityFog() {
    std::memset(static_cast<void*>(&bounds), 0, offsetof(VisibilityFog, cells) - offsetof(VisibilityFog, bounds));
    boundsValid = 0;
}

VisibilityFog::~VisibilityFog() {
    ClearCells();
}

void VisibilityFog::Create() {
    ClearCells();
}
const char* VisibilityFog::ClassName() { return "CVisibilityFog"; }
ClassId VisibilityFog::GetClassId() { return ClassId::VisibilityFog; }

// --- Draw --------------------------------------------------------------------------------------------

// The visibility fog's draw (its cells as sprites through the scene renderer) and the rain / snow layer's work
// arrays. See docs/notes/renderer.md (section 12).

// ---- The visibility fog ---------------------------------------------------------------------------------

namespace {

// A cell's sprite: its colour and its tile of the fog texture.
struct FogSprite {
    std::uint32_t colour;
    const FogTile* tile;
};

// The draw's work arrays: the sprites' centres and sizes and their records.
std::vector<ParticleVertex> g_points;
std::vector<FogSprite> g_sprites;

constexpr int kWorkRecords = 0x1000;      // the arrays' first size
constexpr unsigned kBoundsCells = 12000;  // only a fog of more cells gets its bounds computed here
constexpr float kFogSpriteSize = 0.4f;
constexpr float kColourScale = 255.0f;
constexpr int kOutOfView = 2;

constexpr int kDrops = 0x8000;            // drops (flakes) at most; each is a triangle
constexpr int kDropVertices = kDrops * 3;
constexpr int kRings = 40;                // the offsets below: 40, -40, 39, -39, ..., 1, -1, 0

// A drop's triangle on the 4 x 4 "RainSnow" texture's first tile: the three corners inside the
// tile (u from the left, v from the top of the tile, which is the texture's bottom row upwards).
struct Corner {
    float u, v;
};
constexpr Corner kCorners[3] = {{0.02f, 0.02f}, {0.98f, 0.02f}, {0.5f, 0.98f}};
constexpr float kTile = 0.25f;

} // namespace

int VisibilityFog::Draw(SceneRenderer* renderer) {
    if (cells.size() == 0) return 1;
    if (cells.size() > kBoundsCells && !boundsValid) ComputeBounds();
    if (renderer->pipeline->ClipTest(&bounds) == kOutOfView) return 1;

    g_sprites.resize(kWorkRecords);
    g_points.resize(kWorkRecords);
    g_sprites.resize(static_cast<int>(cells.size()));
    g_points.resize(static_cast<int>(cells.size()));

    int remaining = static_cast<int>(cells.size());
    ParticleVertex* point = g_points.data();
    FogSprite* sprite = g_sprites.data();
    for (auto& node : cells) {
        if (--remaining < 0) break;
        const FogCell& cell = node.second;
        lib::StoreBytes(point, cell.pos);
        point->size = kFogSpriteSize;
        const int alpha = std::lround(cell.alpha * kColourScale);
        sprite->colour = (static_cast<std::uint32_t>(alpha) << 24) | 0x00FFFFFFu;
        sprite->tile = g_fogTiles + cell.frame;
        ++point;
        ++sprite;
    }
    return renderer->DrawSprites(0, &g_points, &g_sprites);
}

// ---- Rain and snow ----------------------------------------------------------------------------------------

void RainSnow::PrepareBuffers() {
    positions.resize(kDrops);
    vertices.resize(kDropVertices);
    frames.resize(kDrops);

    offsets.resize(2 * kRings + 1);
    for (int i = 0; i < kRings; ++i) {
        offsets[2 * i] = kRings - i;
        offsets[2 * i + 1] = -offsets[2 * i];
    }
    offsets[std::ssize(offsets) - 1] = 0;

    // Every vertex's fixed part: white, no specular, its corner's texture coordinates.
    for (int i = 0; i < std::ssize(vertices); ++i) {
        TLVertex& vertex = vertices[i];
        const Corner& corner = kCorners[i % 3];
        vertex.color = 0xFFFFFFFFu;
        vertex.specular = 0xFF000000u;
        vertex.u = corner.u * kTile;
        vertex.v = 1.0f - (1.0f - corner.v) * kTile;
    }

    indices.resize(kDropVertices);
    for (int i = 0; i < std::ssize(indices); ++i) indices[i] = static_cast<std::uint16_t>(i);
}

// --- Weather draw ------------------------------------------------------------------------------------

// The weather's draw (on the weather object; called by the world view's effects pass with the renderer). Mode
// (0 none, 1 rain, 2 snow): the weather is stepped (with the world's tick), its texture bound on stage 0, and
// the rain or snow drawn. Those two build their particles' pre-transformed vertices (vertices, indices) and
// hand them to the new renderer (src/render; RainSnow::DrawParticles). See docs/notes/effects.md and
// docs/notes/renderer.md.

// Returns what the rain / snow draw returned, 1 when there is nothing to draw, 0 without
// a world or with an unknown mode.
int RainSnow::Draw(SceneRenderer* renderer) {
    World* currentWorld = world;
    if (!currentWorld) return 0;
    if (this->mode == 0) return 1;
    UpdateFade(static_cast<float>(currentWorld->tick));
    if (this->mode == 0) return 1;
    EnsureTextureLoaded(texture);
    renderer->SetTexture(0, texture);
    int currentMode = mode;
    int result;
    if (currentMode == 1) result = DrawRain(renderer);
    else if (currentMode == 2) result = DrawSnow(renderer);
    else return 0;
    return result;
}
