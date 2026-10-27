#include "effects.h"
#include "texture.h"
#include "at_exit_functions.h"

#include "core_maths.h"
#include "random.h"
#include "sound.h"
#include "world.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/stlp_hash.h"
#include "lib/stlp.h"
#include "lib/mfc_coll.h"
#include "game_data.h"
#include "game_singletons.h"
#include "lib/nan_audit.h"

#include <bit>
#include <cstring>

// Rain/snow fading and the visibility fog cells. The order of operations is fixed (the goldens depend on it; see
// effects.cpp).

namespace {

constexpr float kZero = 0.0f;
constexpr float kHalf = 0.5f;
constexpr float kOne = 1.0f;
constexpr float kQuarter = 0.25f;
constexpr double kFadePi = std::bit_cast<double>(0x400921F9F01B866Eull);  // 3.14159, sic
constexpr float kVolumeScale = 100.0f;
constexpr float kDensityScale = 64.0f;
constexpr float kFogMinAlpha = std::bit_cast<float>(0x3C23D70Au);      // 0.01
// Random jitter of a fog cell centre (2^-33 with its last bit set).
constexpr double kFogJitter = std::bit_cast<double>(0x3DE0000000100000ull);
constexpr float kFogCellSize = 1.0f;
constexpr float kFar = std::bit_cast<float>(0x7149F2CAu);              // 1e30


// Reads a float back from memory after storing it (the compiler would otherwise reuse the unrounded register
// value).
float LoadFloat(const float& f) { return *static_cast<const volatile float*>(&f); }

double Unsigned(std::uint32_t value) {
    volatile std::int64_t wide = value;
    return static_cast<double>(wide);
}

// vector<uint32> as the fog's static list of cells to drop (a function-local static: constructed on the first
// update, its guard bit 0), with its constructor and insert-overflow.
using KeyVector = stlp::vector<std::uint32_t>;
static_assert(sizeof(KeyVector) == 12);
void KeyVectorInsertOverflow(KeyVector* v, std::uint32_t* pos, const std::uint32_t* value, std::uint32_t count) {
    v->InsertOverflow<stlp::PlainElement<std::uint32_t>>(pos, *value, count);
}

constinit stlp::vector<std::uint32_t> g_deadCells = /*007ABB58*/ {};
std::uint8_t g_deadCellsGuard = /*007ABB64*/ {};
void __cdecl DestroyDeadCells() { stlp::VectorAbi<std::uint32_t>::DestroyBase(&g_deadCells); }

// The cell map's own insert (resize, then insert a copy of the pair unless the key exists; returns the pair) and
// erase by key: the 28-byte dword-keyed table's.
using CellTable = stlp::HashtableAbi<stlp::DwordKeyTraits<28>>;
CellTable::Table* CellTableOf(CVisibilityFog::CellMap* map) { return reinterpret_cast<CellTable::Table*>(map); }
std::uint32_t* InsertCell(CVisibilityFog::CellMap* map, const std::uint32_t* pair) {
    return reinterpret_cast<std::uint32_t*>(
        static_cast<CellTable*>(CellTableOf(map))->FindOrInsert(*reinterpret_cast<const CellTable::Value*>(pair)));
}

} // namespace

// --- Rain and snow ------------------------------------------------------------------------------

// Fades the density (and the rain loop's volume) along half a cosine from fadeStart over
// fadeDuration, towards targetMode; when done, switches mode and stops the sound if it is off.
void CRainSnow::UpdateFade(float now) {
    // A NaN duration takes the last branch, which does the same as the first.
    EI_NAN_AUDIT(now, fadeStart);
    if (fadeDuration == kZero) {   // no fade running
        mode = targetMode;
        if (targetMode == 1 && sound) TheSoundManager()->SetSoundVolume(sound, 100, 0);
    } else if ((now - fadeStart) / fadeDuration <= kOne) {
        double c = x87::fcos((now - fadeStart) / fadeDuration * kFadePi);
        float level = static_cast<float>((targetMode == 0 ? c + kOne : kOne - c) * kHalf);
        if (sound) TheSoundManager()->SetSoundVolume(sound, x87::fistp(level * kVolumeScale), 0);
        density = x87::fistp(level * kDensityScale);
        return;
    } else {
        mode = targetMode;
        if (targetMode == 1 && sound) TheSoundManager()->SetSoundVolume(sound, 100, 0);
    }
    if (mode == 0 && sound) {
        TheSoundManager()->StopSound(sound);
        sound = 0;
    }
    density = 64;
    fadeDuration = 0.0f;
}
EI_REWRITTEN(0x004E40B0, CRainSnow::UpdateFade);

void CRainSnow::FinishFade() {
    mode = targetMode;
    if (targetMode == 1 && sound) TheSoundManager()->SetSoundVolume(sound, 100, 0);
    if (mode == 0 && sound) {
        TheSoundManager()->StopSound(sound);
        sound = 0;
    }
    fadeDuration = 0.0f;
    density = 64;
}
EI_REWRITTEN(0x004E4050, CRainSnow::FinishFade);

void CRainSnow::SetWind(const Vector3& direction, float strength) {
    float x = static_cast<float>(strength * direction.x);
    float y = static_cast<float>(strength * direction.y);
    float z = static_cast<float>(strength * direction.z);
    wind.x = x;
    wind.y = y;
    wind.z = z;
}
EI_REWRITTEN(0x004E41F0, CRainSnow::SetWind);

namespace {
constexpr int kWeatherTextureKind = 6;
constexpr int kRainPriority = 1000;
constexpr int kFullDensity = 64;
const char* WeatherTextureName() { return "RainSnow"; }
const char* RainSoundName() { return "nature\\rain\\circle.wav"; }
} // namespace

void CRainSnow::Clear() {
    if (sound) {
        TheSoundManager()->StopSound(sound);
        sound = 0;
    }
    CTexture* old = texture;
    texture = nullptr;
    if (old) old->Release();
    wind = Vector3{0.0f, 0.0f, 0.0f};
    world = nullptr;
    particleCount = 0;
    mode = 0;
    targetMode = 0;
    fadeStart = 0.0f;
    fadeDuration = 0.0f;
    density = kFullDensity;
}
EI_REWRITTEN(0x004E3B80, CRainSnow::Clear);
// The compiler's jump thunk to it, registered as the same replacement.
EI_REWRITTEN(0x005617B0, CRainSnow::Clear);

void CRainSnow::Start(int newMode, float duration) {
    texture.Load(kWeatherTextureKind, WeatherTextureName());
    if (sound && newMode) {
        TheSoundManager()->StopSound(sound);
        sound = 0;
    }
    if (newMode == 1) sound = TheSoundManager()->Play2D(RainSoundName(), kRainPriority, 0, 0, newMode, 0, 0, 0);
    targetMode = newMode;
    if (mode == 0) mode = newMode;
    std::memcpy(&fadeDuration, &duration, sizeof(fadeDuration));
    fadeStart = static_cast<float>(world->tick_);
}
EI_REWRITTEN(0x004E3FC0, CRainSnow::Start);

CRainSnow::CRainSnow() {
    ClearFields();
    texture = nullptr;
    projected.Construct();
    positions.Construct();
    outcodes.Construct();
    frames.Construct();
    vertices.Construct();
    indices.Construct();
    offsets.Construct();
    world = nullptr;
    sound = 0;
    Clear();
}

CRainSnow::~CRainSnow() {
    Clear();
    offsets.FreeData();
    indices.Destruct();
    vertices.FreeData();
    frames.Destruct();
    outcodes.Destruct();
    positions.FreeData();
    projected.FreeData();
    if (texture) texture->Release();
}
EI_REWRITTEN_SPECIAL(0x005616C0, CRainSnow::~CRainSnow);
EI_REWRITTEN_SPECIAL(0x005617E0, CRainSnow::~CRainSnow);

const char* CRainSnow::GetClassName() { return "CWeather"; }
int CRainSnow::GetClassId() { return 0x1D; }
EI_REWRITTEN(0x005617C0, CRainSnow::GetClassName);
EI_REWRITTEN(0x005617D0, CRainSnow::GetClassId);

// --- Visibility fog -----------------------------------------------------------------------------

CWorld* CVisibilityFog::World() { return ObjectWorld(this); }

// The bounding box (0.5 larger than the cell centres) and sphere of all cells, for culling.
void CVisibilityFog::ComputeBounds() {
    bounds.boxMin = {kFar, kFar, kFar};
    bounds.boxMax = {-kFar, -kFar, -kFar};
    cells.ForEach([this](CellMap::Node* n) {
        const Vector3& p = n->value.pos;
        bounds.boxMin.x = p.x >= bounds.boxMin.x ? bounds.boxMin.x : p.x;
        bounds.boxMin.y = p.y >= bounds.boxMin.y ? bounds.boxMin.y : p.y;
        bounds.boxMin.z = p.z >= bounds.boxMin.z ? bounds.boxMin.z : p.z;
        bounds.boxMax.x = bounds.boxMax.x >= p.x ? bounds.boxMax.x : p.x;
        bounds.boxMax.y = bounds.boxMax.y >= p.y ? bounds.boxMax.y : p.y;
        bounds.boxMax.z = bounds.boxMax.z >= p.z ? bounds.boxMax.z : p.z;
    });
    bounds.boxMax.x = LoadFloat(bounds.boxMax.x) + kHalf;
    bounds.boxMax.y = LoadFloat(bounds.boxMax.y) + kHalf;
    bounds.boxMax.z = LoadFloat(bounds.boxMax.z) + kHalf;
    bounds.boxMin.x = LoadFloat(bounds.boxMin.x) - kHalf;
    bounds.boxMin.y = LoadFloat(bounds.boxMin.y) - kHalf;
    bounds.boxMin.z = LoadFloat(bounds.boxMin.z) - kHalf;
    // The x sum stays on the FPU stack until it is halved; y and z go through float temporaries.
    float sy = static_cast<float>(LoadFloat(bounds.boxMin.y) + LoadFloat(bounds.boxMax.y));
    float sz = static_cast<float>(LoadFloat(bounds.boxMin.z) + LoadFloat(bounds.boxMax.z));
    float cx = static_cast<float>((LoadFloat(bounds.boxMax.x) + LoadFloat(bounds.boxMin.x)) * kHalf);
    float cy = static_cast<float>(sy * kHalf);
    float cz = static_cast<float>(sz * kHalf);
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
    bounds.flags = 3;
    boundsValid = 1;
}
EI_REWRITTEN(0x004E1770, CVisibilityFog::ComputeBounds);

// Per-frame fade: visible cells halve their distance to opaque, the others halve their alpha and
// are dropped below 0.01; every cell steps its texture tile. Returns whether the fog is still
// needed (referenced elsewhere or has cells).
int CVisibilityFog::UpdateCells() {
    KeyVector& dead = g_deadCells;
    if (!(g_deadCellsGuard & 1)) {
        g_deadCellsGuard |= 1;
        dead.ConstructEmpty();
        libcrt::AtExit(DestroyDeadCells);
    }
    dead.Finish() = dead.Start();
    World();   // called and ignored
    cells.ForEach([&dead](CellMap::Node* n) {
        FogCell& cell = n->value;
        if (cell.visible)
            cell.alpha = kOne - (kOne - cell.alpha) * kHalf;
        else
            cell.alpha = cell.alpha * kHalf;
        EI_NAN_AUDIT(cell.alpha);
        if (LoadFloat(cell.alpha) < kFogMinAlpha) {
            if (dead.Finish() == dead.EndOfStorage()) {
                KeyVectorInsertOverflow(&dead, dead.Finish(), &n->key, 1);
            } else {
                if (dead.Finish()) *dead.Finish() = n->key;
                ++dead.Finish();
            }
        }
        cell.frame = static_cast<std::int8_t>((cell.frame + 1) % 15);
    });
    std::uint32_t* end = dead.end();
    for (std::uint32_t* key = dead.begin(); key != end; ++key) {
        std::uint32_t copy = *key;
        CellTableOf(&cells)->EraseKey(copy);
    }
    return (refCount_ > 1 || cells.Size() != 0) ? 1 : 0;
}
EI_REWRITTEN(0x004E1AB0, CVisibilityFog::UpdateCells);

// Marks the cells of the visible squares (adding new ones, transparent, at a jittered point on
// the ground) and clears the mark on all others.
void CVisibilityFog::Reveal(const FogGrid* grid) {
    cells.ForEach([](CellMap::Node* n) { n->value.visible = 0; });
    for (int row = 0; row < grid->cells.height; ++row) {
        for (int col = 0; col < grid->cells.width; ++col) {
            if (grid->cells.rows[row][col] == 0) continue;
            std::uint32_t key = ((grid->y0 + row) << 16) + grid->x0 + col;
            if (CellMap::Node* found = cells.Find(key)) {
                found->value.visible = 1;
                continue;
            }
            // The pair template is filled by an overlapping copy from the key, so every dword of the new value
            // starts as the key.
            std::uint32_t pair[7];
            for (std::uint32_t& d : pair) d = key;
            FogCell* cell = reinterpret_cast<FogCell*>(InsertCell(&cells, pair) + 1);
            cell->visible = 1;
            cell->x = static_cast<std::int16_t>(grid->x0 + col);
            cell->y = static_cast<std::int16_t>(grid->y0 + row);
            std::uint32_t r = IsaacRandom::Global().Next();
            cell->pos.x = static_cast<float>((Unsigned(r) * kFogJitter + cell->x - kQuarter) * (kFogCellSize * kHalf));
            r = IsaacRandom::Global().Next();
            cell->pos.y = static_cast<float>((Unsigned(r) * kFogJitter + cell->y - kQuarter) * (kFogCellSize * kHalf));
            // The water surface height, used unrounded.
            double ground = World()->SurfaceHeightAt(LoadFloat(cell->pos.x), LoadFloat(cell->pos.y));
            cell->alpha = 0.0f;
            cell->pos.z = static_cast<float>(ground + kHalf);
            cell->frame = static_cast<std::int8_t>(IsaacRandom::Global().Next() % 15);
            boundsValid = 0;
        }
    }
}
EI_REWRITTEN(0x004E1C60, CVisibilityFog::Reveal);

// ---- The fog object's virtuals ------------------------------------------------------------------

// The cell nodes (0x20 bytes) go back to the pool's 32-byte free list.
static_assert(sizeof(CVisibilityFog::CellMap::Node) > 24 && sizeof(CVisibilityFog::CellMap::Node) <= 32);

void CVisibilityFog::ClearCells() {
    cells.Clear();
    boundsValid = 0;
}

namespace {
constexpr std::size_t kFogBuckets = 100;   // the hint the cell map is created with
} // namespace

CVisibilityFog::CVisibilityFog() {
    ClearFields();
    std::memset(static_cast<void*>(&bounds), 0, offsetof(CVisibilityFog, cells) - offsetof(CVisibilityFog, bounds));
    cells.InitBuckets(kFogBuckets);
    boundsValid = 0;
}

CVisibilityFog::~CVisibilityFog() {
    ClearCells();
    cells.Destroy();
}
EI_REWRITTEN_SPECIAL(0x004E24E0, CVisibilityFog::~CVisibilityFog);
EI_REWRITTEN_SPECIAL(0x004E24C0, CVisibilityFog::~CVisibilityFog);

int CVisibilityFog::Create() {
    ClearCells();
    return 1;
}
const char* CVisibilityFog::GetClassName() { return "CVisibilityFog"; }
int CVisibilityFog::GetClassId() { return 0x5F; }
EI_REWRITTEN(0x004E23D0, CVisibilityFog::Create);
EI_REWRITTEN(0x004E2440, CVisibilityFog::Destroy);
EI_REWRITTEN(0x004E24A0, CVisibilityFog::GetClassName);
EI_REWRITTEN(0x004E24B0, CVisibilityFog::GetClassId);

EI_REPLACE(0x004E1C50, DestroyDeadCells);
