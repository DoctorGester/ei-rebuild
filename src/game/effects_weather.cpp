#include "core_maths.h"
#include "effects.h"
#include "game_singletons.h"
#include "random.h"
#include "sound.h"
#include "texture.h"
#include "world.h"

#include "lib/containers.h"
#include "lib/nan_audit.h"

#include "ei/x87.h"

#include <array>
#include <bit>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

// Rain/snow fading and the visibility fog cells. The order of operations is fixed (the goldens depend on it; see
// effects.cpp).

namespace {

constexpr float kQuarter = 0.25f;
constexpr double kFadePi = std::bit_cast<double>(0x400921F9F01B866Eull);  // 3.14159, sic
constexpr float kVolumeScale = 100.0f;
constexpr float kDensityScale = 64.0f;
constexpr float kFogMinAlpha = 0.01f;
static_assert(std::bit_cast<std::uint32_t>(kFogMinAlpha) == 0x3C23D70Au);
// Random jitter of a fog cell centre (2^-33 with its last bit set).
constexpr double kFogJitter = std::bit_cast<double>(0x3DE0000000100000ull);
constexpr float kFar = 1e30f;
static_assert(std::bit_cast<std::uint32_t>(kFar) == 0x7149F2CAu);


// Reads a float back from memory after storing it (the compiler would otherwise reuse the unrounded register
// value).
float LoadFloat(const float& f) { return *static_cast<const volatile float*>(&f); }

double Unsigned(std::uint32_t value) {
    volatile std::int64_t wide = value;
    return static_cast<double>(wide);
}

constexpr int kWeatherTextureKind = static_cast<int>(TextureKind::Effects);
constexpr int kRainPriority = 1000;
constexpr int kFullDensity = 64;
const char* WeatherTextureName() { return "RainSnow"; }
const char* RainSoundName() { return "nature\\rain\\circle.wav"; }

} // namespace

// --- Rain and snow ------------------------------------------------------------------------------

// Fades the density (and the rain loop's volume) along half a cosine from fadeStart over
// fadeDuration, towards targetMode; when done, switches mode and stops the sound if it is off.
void CRainSnow::UpdateFade(float now) {
    // A NaN duration takes the last branch, which does the same as the first.
    EI_NAN_AUDIT(now, fadeStart);
    if (fadeDuration == 0.0f) {   // no fade running
        mode = targetMode;
        if (targetMode == 1 && sound) g_clientWorld.sounds_->SetSoundVolume(sound, 100, 0);
    } else if ((now - fadeStart) / fadeDuration <= 1.0f) {
        double c = x87::fcos((now - fadeStart) / fadeDuration * kFadePi);
        float level = static_cast<float>((targetMode == 0 ? c + 1.0f : 1.0f - c) * 0.5f);
        if (sound) g_clientWorld.sounds_->SetSoundVolume(sound, x87::fistp(level * kVolumeScale), 0);
        density = x87::fistp(level * kDensityScale);
        return;
    } else {
        mode = targetMode;
        if (targetMode == 1 && sound) g_clientWorld.sounds_->SetSoundVolume(sound, 100, 0);
    }
    if (mode == 0 && sound) {
        g_clientWorld.sounds_->StopSound(sound);
        sound = 0;
    }
    density = 64;
    fadeDuration = 0.0f;
}

void CRainSnow::SetWind(const Vector3& direction, float strength) {
    float x = static_cast<float>(strength * direction.x);
    float y = static_cast<float>(strength * direction.y);
    float z = static_cast<float>(strength * direction.z);
    wind.x = x;
    wind.y = y;
    wind.z = z;
}

void CRainSnow::Clear() {
    if (sound) {
        g_clientWorld.sounds_->StopSound(sound);
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

void CRainSnow::Start(int newMode, float duration) {
    LoadNamedTexture(texture, kWeatherTextureKind, WeatherTextureName());
    if (sound && newMode) {
        g_clientWorld.sounds_->StopSound(sound);
        sound = 0;
    }
    if (newMode == 1) sound = g_clientWorld.sounds_->Play2D(RainSoundName(), kRainPriority, 0, 0, newMode, 0, 0, 0);
    targetMode = newMode;
    if (mode == 0) mode = newMode;
    fadeDuration = duration;
    fadeStart = static_cast<float>(world->tick_);
}

CRainSnow::CRainSnow() {

    world = nullptr;
    sound = 0;
    Clear();
}

CRainSnow::~CRainSnow() {
    Clear();

    texture.Reset();
}

const char* CRainSnow::ClassName() { return "CWeather"; }
ClassId CRainSnow::GetClassId() { return ClassId::RainSnow; }

// --- Visibility fog -----------------------------------------------------------------------------

// The bounding box (0.5 larger than the cell centres) and sphere of all cells, for culling.
void CVisibilityFog::ComputeBounds() {
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
    bounds.boxMax.x = LoadFloat(bounds.boxMax.x) + 0.5f;
    bounds.boxMax.y = LoadFloat(bounds.boxMax.y) + 0.5f;
    bounds.boxMax.z = LoadFloat(bounds.boxMax.z) + 0.5f;
    bounds.boxMin.x = LoadFloat(bounds.boxMin.x) - 0.5f;
    bounds.boxMin.y = LoadFloat(bounds.boxMin.y) - 0.5f;
    bounds.boxMin.z = LoadFloat(bounds.boxMin.z) - 0.5f;
    // The x sum stays on the FPU stack until it is halved; y and z go through float temporaries.
    float sy = static_cast<float>(LoadFloat(bounds.boxMin.y) + LoadFloat(bounds.boxMax.y));
    float sz = static_cast<float>(LoadFloat(bounds.boxMin.z) + LoadFloat(bounds.boxMax.z));
    float cx = static_cast<float>((LoadFloat(bounds.boxMax.x) + LoadFloat(bounds.boxMin.x)) * 0.5f);
    float cy = static_cast<float>(sy * 0.5f);
    float cz = static_cast<float>(sz * 0.5f);
    bounds.center.x = cx;
    bounds.center.y = cy;
    bounds.center.z = cz;
    bounds.boxMin.x = LoadFloat(bounds.boxMin.x) - cx;
    bounds.boxMin.y = LoadFloat(bounds.boxMin.y) - cy;
    bounds.boxMin.z = LoadFloat(bounds.boxMin.z) - cz;
    bounds.boxMax.x = LoadFloat(bounds.boxMax.x) - cx;
    bounds.boxMax.y = LoadFloat(bounds.boxMax.y) - cy;
    bounds.boxMax.z = LoadFloat(bounds.boxMax.z) - cz;
    float x = LoadFloat(bounds.boxMax.x);
    float y = LoadFloat(bounds.boxMax.y);
    float z = LoadFloat(bounds.boxMax.z);
    boundsSpare = {0.0f, 0.0f, 0.0f};
    bounds.radius = static_cast<float>(std::sqrt(LengthSquaredUnrounded<Sum3::XYZ>(x, y, z)));
    bounds.flags = lib::Bits(BoundsFlag::Sphere | BoundsFlag::Box);
    boundsValid = 1;
}

// Per-frame fade: visible cells halve their distance to opaque, the others halve their alpha and
// are dropped below 0.01; every cell steps its texture tile. Returns whether the fog is still
// needed (referenced elsewhere or has cells).
bool CVisibilityFog::RunTick() {
    std::vector<std::uint32_t> dead;
    for (auto& [key, cell] : cells) {
        if (cell.visible)
            cell.alpha = 1.0f - (1.0f - cell.alpha) * 0.5f;
        else
            cell.alpha = cell.alpha * 0.5f;
        EI_NAN_AUDIT(cell.alpha);
        if (LoadFloat(cell.alpha) < kFogMinAlpha) {
            dead.push_back(key);
        }
        cell.frame = static_cast<std::int8_t>((cell.frame + 1) % 15);
    }
    for (const std::uint32_t key : dead) cells.erase(key);
    return refCount_ > 1 || cells.size() != 0;
}

// Marks the cells of the visible squares (adding new ones, transparent, at a jittered point on
// the ground) and clears the mark on all others.
void CVisibilityFog::Reveal(const FogGrid* grid) {
    for (auto& [key, cell] : cells) cell.visible = 0;
    for (int row = 0; row < grid->cells.height; ++row) {
        for (int col = 0; col < grid->cells.width; ++col) {
            if (grid->cells.rows[row][col] == 0) continue;
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
            std::uint32_t r = g_random.Next();
            cell->pos.x = static_cast<float>((Unsigned(r) * kFogJitter + cell->x - kQuarter) * 0.5f);
            r = g_random.Next();
            cell->pos.y = static_cast<float>((Unsigned(r) * kFogJitter + cell->y - kQuarter) * 0.5f);
            // The water surface height, used unrounded.
            double ground = World()->SurfaceHeightAt(LoadFloat(cell->pos.x), LoadFloat(cell->pos.y));
            cell->alpha = 0.0f;
            cell->pos.z = static_cast<float>(ground + 0.5f);
            cell->frame = static_cast<std::int8_t>(g_random.Next() % 15);
            boundsValid = 0;
        }
    }
}

// ---- The fog object's virtuals ------------------------------------------------------------------

void CVisibilityFog::ClearCells() {
    cells.clear();
    boundsValid = 0;
}

CVisibilityFog::CVisibilityFog() {
    std::memset(static_cast<void*>(&bounds), 0, offsetof(CVisibilityFog, cells) - offsetof(CVisibilityFog, bounds));
    boundsValid = 0;
}

CVisibilityFog::~CVisibilityFog() {
    ClearCells();
}

void CVisibilityFog::Create() {
    ClearCells();
}
const char* CVisibilityFog::ClassName() { return "CVisibilityFog"; }
ClassId CVisibilityFog::GetClassId() { return ClassId::VisibilityFog; }
