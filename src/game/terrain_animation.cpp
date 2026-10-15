// The landscape's per-frame work (006C3C80-006C4290, 006C8E90-006C926A, 006CB1E0): the update
// (Terrain::RunTick), the sun and its staggered relighting (SetSun), the sectors' relight
// countdowns, animated tiles and water waves (SectorData::Animate), and the sector reset helpers.
//
// Waves: every water vertex has a phase offset (owner wavePhases, 006C57E0); the phase along the
// wave direction runs from the sector's first water vertex (mod 2 pi) and steps by the direction
// per vertex. The 512-entry sine table at that phase lifts the vertex (by waveHeight times the
// material's +0x2C) and gives the crest its specular colour; materials of kind 4 move the vertex
// sideways (only where its depth is negative), the others up and in a circle driven by the world
// time.
#include "terrain.h"
#include "address_order.h"
#include "terrain_sector.h"
#include "random.h"
#include "spell.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/vecmath.h"
#include "originals_library.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace terrain {
namespace {

constexpr int kRow = 33;
constexpr int kVertices = kRow * kRow;   // per layer
constexpr std::uint32_t kLit = 4;        // SectorData::built

constexpr float kPi = 3.14159274f;                         // 00744158 (float)
// Table entries per radian: 256 / pi (00744198 / 00744150, doubles) as the original's FDIV leaves it
// at 24-bit precision (SV7d1a: not the folded double).
constexpr float kSineIndex = static_cast<float>(256.0 / 3.141592653589793);
constexpr float kQuarterTurn = 0.785398126f;               // 00744194
constexpr float kTimeScale = 0.05f;                        // the circling waves' speed per world tick
constexpr float kGlowScale = 50.0f;                        // 00744180
constexpr float kRiseScale = 0.25f;                        // 0073C3B0
constexpr float kCircleScale = 3.0f;                       // 0073BAA4
constexpr float kMinComponent = 0.01f;                     // 0073C548

// The world clock: whole ticks (FILD) and the fraction of the current one.
int GameTicks() { return *reinterpret_cast<const int*>(0x007AF61C); }
float GameTickFraction() { return *reinterpret_cast<const float*>(0x007AFCA8); }
// The sun's ray step scale (007C8964 = 1 * 95, a static initialiser), and the cell size (007441D0).
float SunStepScale() { return *reinterpret_cast<const float*>(0x007C8964); }
float CellSize() { return *reinterpret_cast<const float*>(0x007441D0); }

bool SignClear(float v) {
    std::uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    return (bits & 0x80000000u) == 0;
}

// The sine table entry at `phase` radians: FISTP of (phase * 256 / pi) stored as a float, mod 512.
float Sine(const Terrain* owner, double phase) {
    return owner->sine[x87::fistp(static_cast<float>(phase * kSineIndex)) & 0x1FF];
}

std::uint32_t Byte(double v) { return static_cast<std::uint32_t>(x87::fistp(static_cast<float>(v))) & 0xFF; }

} // namespace

void SectorData::ResetFields() {
    sx = sy = -1;
    detail = seamAfter = seamBefore = 0;
    hasWater = 0;
    hasWaterTiles = 0;
    data = nullptr;
    owner = nullptr;
    pages = perPage = -1;
    built = 0;
    lastUsed = 0;
    relightCountdown = 0;
    clipMode = 2;
    spare38C = 0;
    spare390 = 0;
    hasSpecular = 0;
}

void SectorData::Clear() {
    records.SetSize(0, -1);
    for (int quarter = 0; quarter < 4; ++quarter) {
        for (int page = 0; page < tileFlags[quarter].size; ++page) tileFlags[quarter][page].SetSize(0, -1);
        for (int page = 0; page < tiles[quarter].size; ++page) tiles[quarter][page].SetSize(0, -1);
    }
    ResetFields();
}

void SectorData::ScheduleRelight(int frames) {
    if (relightCountdown < 0 || frames < relightCountdown) relightCountdown = frames;
}

void SectorData::RelightTick() {
    if (relightCountdown < 0) return;
    if (--relightCountdown <= 0) {
        relightCountdown = -1;
        built &= ~kLit;
    }
}

void SectorData::Animate(Terrain* landscape, int frame) {
    if (frame % 3 == 0 && (landscape->landscapeFlags & 2) != 0) {
        for (int i = 0; i < animated.size; ++i) {
            AnimatedTile& tile = animated[i];
            if (tile.frame == 0 && tile.speed < 1000 &&
                IsaacRandom::Global().Next() % 1000 + 1 > static_cast<std::uint32_t>(tile.speed)) {
                continue;
            }
            tile.frame = (tile.frame + 1) % tile.frames;
            const float* uv = landscape->TileUv(tile.rotation, tile.frameBase + tile.frame, 0);
            for (int k = 0; k < 9; ++k) {
                tile.vertices[k].u = uv[k];
                tile.vertices[k].v = 1.0f - uv[k + 9];
            }
        }
    }
    if (!hasWater || (landscape->landscapeFlags & 1) == 0) return;

    SectorPosition* positions = &vertices[kVertices];
    SectorVertex* water = &records[kVertices];
    const float* phaseOffset = landscape->wavePhases.data;
    Vector2 direction = {landscape->waveDirection.x, landscape->waveDirection.y};
    float stepX = static_cast<float>(1.0f * direction.x);   // 00744160: the vertex spacing
    float stepY = static_cast<float>(1.0f * direction.y);
    float height = landscape->waveHeight;
    float phase = landscape->wavePhase;
    float fullTurn = static_cast<float>(kPi + kPi);
    Vector2 first = {positions[0].x, positions[0].y};
    float rowStart = static_cast<float>(std::fmod(Dot2D(direction, first) + fullTurn, static_cast<double>(fullTurn)));

    int lastMaterial = -1;
    float lastTurn = 0.0f;
    float amplitude = 0.0f;
    int kind = 0;
    Vector3 crest = {0.0f, 0.0f, 0.0f};
    for (int row = 0; row < kRow; ++row) {
        float along = rowStart;
        for (int column = 0; column < kRow; ++column, ++phaseOffset) {
            SectorVertex& record = water[row * kRow + column];
            SectorPosition& position = positions[row * kRow + column];
            int material = record.waterMaterial;
            if (material != 0xFFFF) {
                if (material != lastMaterial) {
                    const TerrainMaterial& m = landscape->materials[material];
                    amplitude = static_cast<float>(height * m.priority);
                    kind = m.kind;
                    double glow = landscape->waveGlow * m.priority * kGlowScale;
                    crest = {static_cast<float>(glow * m.waveColour.x), static_cast<float>(glow * m.waveColour.y),
                             static_cast<float>(glow * m.waveColour.z)};
                    double turn = std::fmod(static_cast<double>(phase * m.priority), 2.0 * kPi);
                    float delta = static_cast<float>(turn - lastTurn);
                    lastTurn = static_cast<float>(turn);
                    rowStart = static_cast<float>(delta + rowStart);
                    along = static_cast<float>(delta + along);
                    lastMaterial = material;
                }
                float s = Sine(landscape, along + *phaseOffset);
                if (SignClear(s)) {
                    std::uint32_t r = Byte(s * crest.x);
                    std::uint32_t g = Byte(s * crest.y);
                    std::uint32_t b = Byte(s * crest.z);
                    record.specular = (record.specular & 0xFF000000u) | r << 16 | g << 8 | b;
                } else {
                    record.specular &= 0xFF000000u;
                }
                double lift = s * amplitude;
                if (kind == 4) {
                    if (!SignClear(record.depth)) {
                        position.x = static_cast<float>(lift + record.position.x);
                        position.y = static_cast<float>(lift + record.position.y);
                    }
                } else {
                    position.z = static_cast<float>(lift * kRiseScale + record.position.z);
                    float time = static_cast<float>((GameTicks() + GameTickFraction()) * kTimeScale);
                    float sineX = Sine(landscape, column * kQuarterTurn + time);
                    position.x = sineX * amplitude * kCircleScale + record.position.x;
                    float sineY = Sine(landscape, row * kQuarterTurn + time);
                    position.y = sineY * amplitude * kCircleScale + record.position.y;
                }
            }
            along = static_cast<float>(along + stepX);
        }
        rowStart = static_cast<float>(rowStart + stepY);
    }
}

namespace {

// 006C8E90. The relight order, meant as (sy, sx) of the sectors, but qsort hands it the addresses
// of the array slots: it reads +0xC / +0x8 of those, the pointers three and two slots further on
// (past the end for the last ones). Kept as the original sorts.
int __cdecl CompareSlots(const void* a, const void* b) {
    const auto* x = static_cast<const SectorData*>(a);
    const auto* y = static_cast<const SectorData*>(b);
    if (x->sy < y->sy) return -1;
    if (x->sy > y->sy) return 1;
    if (x->sx < y->sx) return -1;
    return x->sx > y->sx ? 1 : 0;
}

// The order CompareSlots meant, on the sectors themselves: harness runs and the standalone
// (address_order.h), since the slots' neighbours' pointers follow the heap and the last slots read
// past the array.
int __cdecl CompareSectors(const void* a, const void* b) {
    return CompareSlots(*static_cast<const SectorData* const*>(a), *static_cast<const SectorData* const*>(b));
}

// SetSun's function-local static (007C8938, guard bit 1 of 007C8948, freed by 006C9270 at exit).
lib::AlignedArray<SectorData*>& SortedSectors() { return *reinterpret_cast<lib::AlignedArray<SectorData*>*>(0x007C8938); }
std::uint8_t& SortedSectorsGuard() { return *reinterpret_cast<std::uint8_t*>(0x007C8948); }
__declspec(naked) void __cdecl FreeSortedSectors() EI_ORIGINAL(0x006C9270)

// A sun direction component the ray steps divide by: below 0.01 in size (or NaN) it becomes 0.01
// with its sign (_copysign of the double 0.01f).
float AwayFromZero(float v) { return !(std::fabs(v) >= kMinComponent) ? std::copysign(kMinComponent, v) : v; }

} // namespace

} // namespace terrain

using terrain::SectorData;

void Terrain::SetSun(CLight* light, const Vector4* colour) {
    ambient = *colour;
    float x = terrain::AwayFromZero(-light->direction_.x);
    float y = terrain::AwayFromZero(-light->direction_.y);
    float z = terrain::AwayFromZero(-light->direction_.z);
    double inverse = 1.0f / vecmath::Length<Sum3::ZYX>(Vector3{x, y, z});
    x = static_cast<float>(inverse * x);
    y = static_cast<float>(inverse * y);
    double up = z * inverse;
    sunSlopeX = static_cast<float>(up / x);
    sunSlopeY = static_cast<float>(up / y);
    sunRunX = static_cast<float>(x / up);
    sunRunY = static_cast<float>(y / up);
    sunStepX = x87::fistp(static_cast<float>(terrain::SunStepScale() * x / terrain::CellSize()));
    sunStepY = x87::fistp(static_cast<float>(terrain::SunStepScale() * y / terrain::CellSize()));
    sun = light;
    if (lightInterval <= 0) {
        for (SectorCache::Node* node = sectorCache.head; node; node = node->next) node->data->built &= ~terrain::kLit;
        return;
    }
    // The lit sectors, sorted, get relight countdowns spread evenly over the next interval.
    auto& sorted = terrain::SortedSectors();
    if ((terrain::SortedSectorsGuard() & 1) == 0) {
        terrain::SortedSectorsGuard() |= 1;
        sorted = {nullptr, 0, 0, 0};
        GameAtExit(terrain::FreeSortedSectors);
    }
    if (!sorted.data) {
        sorted.data = sorted.Allocate(0);
        sorted.capacity = 0;
    }
    sorted.size = 0;
    for (SectorCache::Node* node = sectorCache.head; node; node = node->next) {
        SectorData* sector = node->data;
        if (sector->built & terrain::kLit) {
            int index = sorted.size;
            sorted.SetSize(index + 1, -1);
            sorted[index] = sector;
        }
    }
    GameQsortOriginal(sorted.data, sorted.size, sizeof(SectorData*),
                      address_order::ById() ? terrain::CompareSectors : terrain::CompareSlots);
    if (sorted.size == 0) return;
    float step = static_cast<float>(static_cast<double>(lightInterval - 1) / sorted.size);
    float at = 0.0f;
    for (int i = 0; i < sorted.size; ++i) {
        int frames = x87::fistp(at);
        at = static_cast<float>(at + step);
        sorted[i]->ScheduleRelight(frames);
        if (frames == 0) sorted[i]->built &= ~terrain::kLit;
    }
}

int Terrain::RunTickImpl() {
    if (lightCountdown > 0 && --lightCountdown == 0) {
        lightCountdown = lightInterval;
        SetSun(sun, &ambient);
    }
    for (SectorCache::Node* node = sectorCache.head; node;) {
        SectorData* sector = node->data;
        node = node->next;
        if (sector->built & terrain::kLit) sector->RelightTick();
    }
    if ((landscapeFlags & 8) != 0) PlayAmbience();
    wavePhase = static_cast<float>(waveSpeed + wavePhase);
    ++frame;
    for (int row = 0; row < visibleColumnFirst.size; ++row) {
        for (int column = visibleColumnFirst[row]; column <= visibleColumnLast[row]; ++column) {
            SectorData* sector = SectorAt(column, visibleRowFirst + row, terrain::kLit, 0);
            if (sector && sector->IsDrawn()) sector->Animate(this, frame);
        }
    }
    return 1;
}

namespace {
} // namespace

EI_REPLACE_METHOD(0x006C3C80, SectorData::Clear);
EI_REPLACE_METHOD(0x006C3D70, SectorData::ResetFields);
EI_REPLACE_METHOD(0x006C3E30, SectorData::ScheduleRelight);
EI_REPLACE_METHOD(0x006C3E50, SectorData::RelightTick);
EI_REPLACE_METHOD(0x006C3E80, SectorData::Animate);
EI_REPLACE_METHOD(0x006C8ED0, Terrain::SetSun);
EI_REPLACE_METHOD(0x006CB1E0, Terrain::RunTickImpl);
