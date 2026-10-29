#include "core_maths.h"
#include "effects.h"
#include "game_singletons.h"
#include "random.h"
#include "sound.h"
#include "texture.h"
#include "world.h"

#include "lib/containers.h"
#include "lib/nan_audit.h"

#include <array>
#include <bit>
#include <cmath>
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


float Unsigned(std::uint32_t value) { return static_cast<float>(value); }

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
        bounds.boxMin.x = p.x >= bounds.boxMin.x ? bounds.boxMin.x : p.x;
        bounds.boxMin.y = p.y >= bounds.boxMin.y ? bounds.boxMin.y : p.y;
        bounds.boxMin.z = p.z >= bounds.boxMin.z ? bounds.boxMin.z : p.z;
        bounds.boxMax.x = bounds.boxMax.x >= p.x ? bounds.boxMax.x : p.x;
        bounds.boxMax.y = bounds.boxMax.y >= p.y ? bounds.boxMax.y : p.y;
        bounds.boxMax.z = bounds.boxMax.z >= p.z ? bounds.boxMax.z : p.z;
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
            cell->pos.x = (Unsigned(r) * kFogJitter + cell->x - kQuarter) * 0.5f;
            r = g_clientRandom.Next();
            cell->pos.y = (Unsigned(r) * kFogJitter + cell->y - kQuarter) * 0.5f;
            float ground = static_cast<float>(World()->SurfaceHeightAt(cell->pos.x, cell->pos.y));
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
