#include "effects.h"
#include "texture.h"
#include "at_exit_functions.h"

#include "core_maths.h"
#include "random.h"
#include "sound.h"
#include "world.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "ei/x87.h"
#include "lib/stlp_hash.h"
#include "originals_effects.h"
#include "originals_engine.h"
#include "lib/stlp.h"
#include "originals_library.h"
#include "originals_render.h"
#include "lib/mfc_coll.h"

#include <bit>
#include <cstring>

// Rain/snow fading and the visibility fog cells. Order of operations follows the original
// instruction sequences (see effects.cpp).

namespace {

constexpr float kZero = 0.0f;
constexpr float kHalf = 0.5f;                                           // 0073B86C
constexpr float kOne = 1.0f;                                            // 0073B84C
constexpr float kQuarter = 0.25f;                                       // 0073C3B0
constexpr double kFadePi = std::bit_cast<double>(0x400921F9F01B866Eull);  // 0073D738 (3.14159, sic)
constexpr float kVolumeScale = 100.0f;                                  // 0073BAB0
constexpr float kDensityScale = 64.0f;                                  // 0073D734
constexpr float kFogMinAlpha = std::bit_cast<float>(0x3C23D70Au);      // 0073C548 (0.01)
// Random jitter of a fog cell centre (0073CDA0, 2^-33 with its last bit set).
constexpr double kFogJitter = std::bit_cast<double>(0x3DE0000000100000ull);
constexpr float kFogCellSize = 1.0f;                                    // 0073D6A4
constexpr float kFar = std::bit_cast<float>(0x7149F2CAu);              // 1e30

IsaacRandom& GlobalRandom() { return *reinterpret_cast<IsaacRandom*>(0x007A9D40); }

bool LessOrUnordered(double a, double b) { return !(a >= b); }

// Reads a float back from memory, as the original does after storing it (the compiler would
// otherwise reuse the unrounded register value).
float Load(const float& f) { return *static_cast<const volatile float*>(&f); }

double Unsigned(std::uint32_t value) {
    volatile std::int64_t wide = value;
    return static_cast<double>(wide);
}

// The sound manager (*007AF784): 006B9100 sets a playing sound's volume (0..100), 006B94A0 stops it.
void* SoundManager() { return *reinterpret_cast<void**>(0x007AF784); }

// The game's atexit (006E727B).

// vector<uint32> as the fog's static list of cells to drop (007ABB58), with the original's
// constructor (004E2BA0) and insert-overflow (004E2FA0).
using KeyVector = stlp::vector<std::uint32_t>;
static_assert(sizeof(KeyVector) == 12);
void KeyVectorInsertOverflow(KeyVector* v, std::uint32_t* pos, const std::uint32_t* value, std::uint32_t count) {
    v->InsertOverflow<stlp::PlainElement<std::uint32_t>>(pos, *value, count);
}

KeyVector& DeadCells() { return *reinterpret_cast<KeyVector*>(0x007ABB58); }
std::uint8_t& DeadCellsGuard() { return *reinterpret_cast<std::uint8_t*>(0x007ABB64); }

// The cell map's own insert (004E2E90: resize, then insert a copy of the pair unless the key
// exists; returns the pair) and erase by key (004E2F00): the 28-byte dword-keyed table's.
using CellTable = stlp::HashtableAbi<stlp::DwordKeyTraits<28>>;
CellTable::Table* CellTableOf(CVisibilityFog::CellMap* map) { return reinterpret_cast<CellTable::Table*>(map); }
std::uint32_t* InsertCell(CVisibilityFog::CellMap* map, const std::uint32_t* pair) {
    return reinterpret_cast<std::uint32_t*>(
        CellTable::FindOrInsert(CellTableOf(map), nullptr, reinterpret_cast<const CellTable::Value*>(pair)));
}

} // namespace

// --- Rain and snow ------------------------------------------------------------------------------

// Fades the density (and the rain loop's volume) along half a cosine from fadeStart over
// fadeDuration, towards targetMode; when done, switches mode and stops the sound if it is off.
void CRainSnow::UpdateFade(float now) {
    if (!(fadeDuration < kZero) && !(fadeDuration > kZero)) {   // 0 or NaN: no fade running
        mode = targetMode;
        if (targetMode == 1 && sound) static_cast<CSoundManager*>(SoundManager())->SetSoundVolume(sound, 100, 0);
    } else if (!((now - fadeStart) / fadeDuration > kOne)) {
        double c = x87::fcos((now - fadeStart) / fadeDuration * kFadePi);
        float level = static_cast<float>((targetMode == 0 ? c + kOne : kOne - c) * kHalf);
        if (sound) static_cast<CSoundManager*>(SoundManager())->SetSoundVolume(sound, x87::fistp(level * kVolumeScale), 0);
        density = x87::fistp(level * kDensityScale);
        return;
    } else {
        mode = targetMode;
        if (targetMode == 1 && sound) static_cast<CSoundManager*>(SoundManager())->SetSoundVolume(sound, 100, 0);
    }
    if (mode == 0 && sound) {
        static_cast<CSoundManager*>(SoundManager())->StopSound(sound);
        sound = 0;
    }
    density = 64;
    fadeDuration = 0.0f;
}
EI_REPLACE_METHOD(0x004E40B0, CRainSnow::UpdateFade);

// Ends a fade at once.
void CRainSnow::FinishFade() {
    mode = targetMode;
    if (targetMode == 1 && sound) static_cast<CSoundManager*>(SoundManager())->SetSoundVolume(sound, 100, 0);
    if (mode == 0 && sound) {
        static_cast<CSoundManager*>(SoundManager())->StopSound(sound);
        sound = 0;
    }
    fadeDuration = 0.0f;
    density = 64;
}
EI_REPLACE_METHOD(0x004E4050, CRainSnow::FinishFade);

// SetWind script command (00564190 calls it): the drops' slant.
void CRainSnow::SetWind(const Vector3& direction, float strength) {
    float x = static_cast<float>(strength * direction.x);
    float y = static_cast<float>(strength * direction.y);
    float z = static_cast<float>(strength * direction.z);
    wind.x = x;
    wind.y = y;
    wind.z = z;
}
EI_REPLACE_METHOD(0x004E41F0, CRainSnow::SetWind);

namespace {
constexpr int kWeatherTextureKind = 6;
constexpr int kRainPriority = 1000;
constexpr int kFullDensity = 64;
// 0078AE48 "RainSnow" and 0078DFA4 "nature\rain\circle.wav".
const char* WeatherTextureName() { return reinterpret_cast<const char*>(0x0078AE48); }
const char* RainSoundName() { return reinterpret_cast<const char*>(0x0078DFA4); }
} // namespace

void CRainSnow::Clear() {
    if (sound) {
        static_cast<CSoundManager*>(SoundManager())->StopSound(sound);
        sound = 0;
    }
    CEngineObject* old = texture;
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
EI_REPLACE_METHOD(0x004E3B80, CRainSnow::Clear);
// The compiler's jump thunks to it (functions.csv "thunk" rows; S16a: the standalone image has no
// code at them): the same replacement, so their calls skip its check.
EI_REPLACE_METHOD(0x005617B0, CRainSnow::Clear);

void CRainSnow::Start(int newMode, float duration) {
    TextureHolderLoad(reinterpret_cast<CTexture**>(&texture), nullptr, kWeatherTextureKind, WeatherTextureName());
    if (sound && newMode) {
        static_cast<CSoundManager*>(SoundManager())->StopSound(sound);
        sound = 0;
    }
    if (newMode == 1) sound = sound::Manager()->Play2D(RainSoundName(), kRainPriority, 0, 0, newMode, 0, 0, 0);
    targetMode = newMode;
    if (mode == 0) mode = newMode;
    std::memcpy(&fadeDuration, &duration, sizeof(fadeDuration));
    fadeStart = static_cast<float>(world->tick_);
}
EI_REPLACE_METHOD(0x004E3FC0, CRainSnow::Start);

void CRainSnow::DestructBody() {
    ei::SetVtable(this, kRainSnowVtable);
    Clear();
    offsets.FreeData();
    reinterpret_cast<mfc::impl::CWordArray*>(&indices)->Destruct();   // 006FEECD
    vertices.FreeData();
    reinterpret_cast<mfc::impl::CByteArray*>(&frames)->Destruct();   // 006FF61D
    reinterpret_cast<mfc::impl::CByteArray*>(&outcodes)->Destruct();
    positions.FreeData();
    projected.FreeData();
    if (texture) texture->Release();
    DestructBase();
}
EI_REPLACE_METHOD(0x005616C0, CRainSnow::DestructBody);

CRainSnow* CRainSnow::DeletingDestruct(unsigned flags) {
    DestructBody();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x005617E0, CRainSnow::DeletingDestruct);

// --- Visibility fog -----------------------------------------------------------------------------

void* CVisibilityFog::World() { return ObjectWorld(reinterpret_cast<const CEngineObject*>(this)); }

// The bounding box (0.5 larger than the cell centres) and sphere of all cells, for culling.
void CVisibilityFog::ComputeBounds() {
    boundsMin = {kFar, kFar, kFar};
    boundsMax = {-kFar, -kFar, -kFar};
    cells.ForEach([this](CellMap::Node* n) {
        const Vector3& p = n->value.pos;
        boundsMin.x = LessOrUnordered(p.x, boundsMin.x) ? p.x : boundsMin.x;
        boundsMin.y = LessOrUnordered(p.y, boundsMin.y) ? p.y : boundsMin.y;
        boundsMin.z = LessOrUnordered(p.z, boundsMin.z) ? p.z : boundsMin.z;
        boundsMax.x = LessOrUnordered(boundsMax.x, p.x) ? p.x : boundsMax.x;
        boundsMax.y = LessOrUnordered(boundsMax.y, p.y) ? p.y : boundsMax.y;
        boundsMax.z = LessOrUnordered(boundsMax.z, p.z) ? p.z : boundsMax.z;
    });
    boundsMax.x = Load(boundsMax.x) + kHalf;
    boundsMax.y = Load(boundsMax.y) + kHalf;
    boundsMax.z = Load(boundsMax.z) + kHalf;
    boundsMin.x = Load(boundsMin.x) - kHalf;
    boundsMin.y = Load(boundsMin.y) - kHalf;
    boundsMin.z = Load(boundsMin.z) - kHalf;
    // The x sum stays on the FPU stack until it is halved; y and z go through float temporaries.
    float sy = static_cast<float>(Load(boundsMin.y) + Load(boundsMax.y));
    float sz = static_cast<float>(Load(boundsMin.z) + Load(boundsMax.z));
    float cx = static_cast<float>((Load(boundsMax.x) + Load(boundsMin.x)) * kHalf);
    float cy = static_cast<float>(sy * kHalf);
    float cz = static_cast<float>(sz * kHalf);
    center.x = cx;
    center.y = cy;
    center.z = cz;
    boundsMin.x = Load(boundsMin.x) - cx;
    boundsMin.y = Load(boundsMin.y) - cy;
    boundsMin.z = Load(boundsMin.z) - cz;
    boundsMax.x = Load(boundsMax.x) - cx;
    boundsMax.y = Load(boundsMax.y) - cy;
    boundsMax.z = Load(boundsMax.z) - cz;
    float x = Load(boundsMax.x);
    float y = Load(boundsMax.y);
    float z = Load(boundsMax.z);
    boundsSpare = {0.0f, 0.0f, 0.0f};
    radius = static_cast<float>(x87::fsqrt(LengthSquaredUnrounded<Sum3::XYZ>(x, y, z)));
    boundsKind = 3;
    boundsValid = 1;
}
EI_REPLACE_METHOD(0x004E1770, CVisibilityFog::ComputeBounds);

// Per-frame fade: visible cells halve their distance to opaque, the others halve their alpha and
// are dropped below 0.01; every cell steps its texture tile. Returns whether the fog is still
// needed (referenced elsewhere or has cells).
int CVisibilityFog::UpdateCells() {
    KeyVector& dead = DeadCells();
    if (!(DeadCellsGuard() & 1)) {
        DeadCellsGuard() |= 1;
        dead.ConstructEmpty();   // 004E2BA0
        GameAtExit(ei::static_init::Exit004E1C50);
    }
    dead.Finish() = dead.Start();
    World();   // called and ignored, as in the original
    cells.ForEach([&dead](CellMap::Node* n) {
        FogCell& cell = n->value;
        if (cell.visible)
            cell.alpha = kOne - (kOne - cell.alpha) * kHalf;
        else
            cell.alpha = cell.alpha * kHalf;
        if (LessOrUnordered(Load(cell.alpha), kFogMinAlpha)) {
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
        CellTable::EraseKey(CellTableOf(&cells), nullptr, &copy);
    }
    return (refCount_ > 1 || cells.Size() != 0) ? 1 : 0;
}
EI_REPLACE_METHOD(0x004E1AB0, CVisibilityFog::UpdateCells);

// Marks the cells of the visible squares (adding new ones, transparent, at a jittered point on
// the ground) and clears the mark on all others.
void CVisibilityFog::Reveal(const FogGrid* grid) {
    cells.ForEach([](CellMap::Node* n) { n->value.visible = 0; });
    for (int row = 0; row < grid->height; ++row) {
        for (int col = 0; col < grid->width; ++col) {
            if (grid->rows[row][col] == 0) continue;
            std::uint32_t key = ((grid->y0 + row) << 16) + grid->x0 + col;
            if (CellMap::Node* found = cells.Find(key)) {
                found->value.visible = 1;
                continue;
            }
            // The original fills its pair template with an overlapping REP MOVSD from the key, so
            // every dword of the new value starts as the key.
            std::uint32_t pair[7];
            for (std::uint32_t& d : pair) d = key;
            FogCell* cell = reinterpret_cast<FogCell*>(InsertCell(&cells, pair) + 1);
            cell->visible = 1;
            cell->x = static_cast<std::int16_t>(grid->x0 + col);
            cell->y = static_cast<std::int16_t>(grid->y0 + row);
            std::uint32_t r = GlobalRandom().Next();
            cell->pos.x = static_cast<float>((Unsigned(r) * kFogJitter + cell->x - kQuarter) * (kFogCellSize * kHalf));
            r = GlobalRandom().Next();
            cell->pos.y = static_cast<float>((Unsigned(r) * kFogJitter + cell->y - kQuarter) * (kFogCellSize * kHalf));
            // The world's slot 30: the water surface height, used unrounded from ST0.
            double ground = static_cast<CWorld*>(World())->SurfaceHeightAt(Load(cell->pos.x), Load(cell->pos.y));
            cell->alpha = 0.0f;
            cell->pos.z = static_cast<float>(ground + kHalf);
            cell->frame = static_cast<std::int8_t>(GlobalRandom().Next() % 15);
            boundsValid = 0;
        }
    }
}
EI_REPLACE_METHOD(0x004E1C60, CVisibilityFog::Reveal);

// ---- The fog object's slots (A-S10) ----------------------------------------------------------------

// The cell nodes (0x20 bytes) go back to the pool's 32-byte free list (0079B230), as the original
// pushes them.
static_assert(sizeof(CVisibilityFog::CellMap::Node) > 24 && sizeof(CVisibilityFog::CellMap::Node) <= 32);

void CVisibilityFog::ClearCells() {
    cells.Clear();
    boundsValid = 0;
}

void CVisibilityFog::Destruct() {
    vtable_ = reinterpret_cast<void*>(0x0073D6B4);
    ClearCells();
    cells.Destroy();
    reinterpret_cast<CEngineObject*>(this)->DestructBase();
}
EI_REPLACE_METHOD(0x004E24E0, CVisibilityFog::Destruct);

CVisibilityFog* CVisibilityFog::DeletingDestruct(int flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x004E24C0, CVisibilityFog::DeletingDestruct);

namespace {
constexpr int kFogClassId = 0x5F;
} // namespace
// Slots 2 / 3 (004E23D0 returns 1), 11 / 12 ("CVisibilityFog" 0078DE80, 0x5F).
int __fastcall FogCreate(CVisibilityFog* self) {
    self->ClearCells();
    return 1;
}
void __fastcall FogDestroy(CVisibilityFog* self) { self->ClearCells(); }
const char* __fastcall FogClassName(CVisibilityFog*) { return reinterpret_cast<const char*>(0x0078DE80); }
int __fastcall FogClassId(CVisibilityFog*) { return kFogClassId; }
EI_REPLACE(0x004E23D0, FogCreate);
EI_REPLACE(0x004E2440, FogDestroy);
EI_REPLACE(0x004E24A0, FogClassName);
EI_REPLACE(0x004E24B0, FogClassId);
