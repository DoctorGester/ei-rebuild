// Terrain vertex lighting: SectorData::Light runs over a rectangle of a sector's vertices with the FPU
// switched to chop rounding (_controlfp(0x300, 0x300)), so every operation below rounds towards zero there.
// The GPU lights the colours (renderer.md section 13); the CPU writes only what the game reads: the edge fade
// and the water layer's specular word (its point lights dimmed under water by 1 - absorption * depth^2 *
// falloff times the light's vertical component squared).
#include "terrain.h"
#include "terrain_sector.h"
#include "spell.h"

#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/vecmath.h"

#include <bit>
#include <cstdint>
#include <cstring>

namespace terrain {
namespace {

constexpr int kVertices = 33 * 33;     // per layer
constexpr int kRow = 33;

// 1 / the water depth scale (set by a static initialiser).
constexpr float kWaterFalloff = /*007C8908*/ 0.06666667f;   // a static initialiser: 1 / 15

// A point light's shading of a colour (r, g, b, a) for a vertex normal is CLight::AddPoint (at `distance` along
// the unit `direction`): each channel clamped to 1, the alpha copied through.

// Select idiom: `b` when a < b or unordered.
float Max(float a, float b) { return a >= b ? a : b; }
float AtLeastZero(float v) { return v >= 0.0f ? v : 0.0f; }

bool SignClear(float v) {
    std::uint32_t bits;
    bits = std::bit_cast<std::uint32_t>(v);
    return (bits & 0x80000000u) == 0;
}

// Whether the sun's ray march (Terrain::SunReaches) from vertex (column, row) of `sector` can change
// nothing of the game's. Its answer feeds only the vertex colours, which the GPU lighting does not read;
// but each sector it steps into is looked up with SectorAt (loaded when missing, its use and the cache's
// order refreshed), which feeds the sector cache (and through its builds the global ISAAC draws). Those
// lookups change nothing when every sector the ray can step into is cached and was used this frame already:
// then the march is left out. Bounds as SunReaches steps: the directions as it turns them, at most `length`
// steps, each moving at most one vertex along x and one along y, a sector crossed when the column (row)
// passes 32 going up or 0 going down.
bool SunMarchTouchesNothing(const Terrain* terrain, const SectorData* sector, int column, int row) {
    int stepX = terrain->sunStepX, stepY = terrain->sunStepY;
    float slopeX = terrain->sunSlopeX, slopeY = terrain->sunSlopeY;
    int directionX = 1, directionY = 1;
    if (stepX < 0 || (stepX == 0 && !SignClear(slopeX))) {
        slopeX = -slopeX;
        directionX = -1;
        stepX = -stepX;
    }
    if (stepY < 0 || (stepY == 0 && !SignClear(slopeY))) {
        slopeY = -slopeY;
        directionY = -1;
        stepY = -stepY;
    }
    if (!SignClear(slopeX) || !SignClear(slopeY)) return true;   // it returns before any step
    const int length = stepY > stepX ? stepY : stepX;
    auto crossings = [length](int at, int direction) {
        int toFirst = direction > 0 ? kRow - at : at + 1;   // moves to the first crossing
        return length >= toFirst ? 1 + (length - toFirst) / (kRow - 1) : 0;
    };
    const int acrossX = crossings(column, directionX), acrossY = crossings(row, directionY);
    for (int j = 0; j <= acrossY; ++j)
        for (int i = 0; i <= acrossX; ++i) {
            if (i == 0 && j == 0) continue;   // its own sector is read directly
            int sx = sector->sx + i * directionX, sy = sector->sy + j * directionY;
            if (sx < 0 || sy < 0 || sx >= terrain->sectorsX || sy >= terrain->sectorsY) continue;   // SectorAt: none
            const std::list<terrain::SectorData*>::iterator node = terrain->sectorGrid[sx][sy];
            if (node == std::list<terrain::SectorData*>::iterator{} || (*node)->lastUsed < terrain->frame) return false;
        }
    return true;
}

} // namespace

// The water layer's specular word (the GPU lights the colours): the harness digest's "water" reads that word
// (the edge fade, then the specular lights; Animate writes the crests over its colour bytes): the vertex lighting
// of the lights with flag 0x40; the others only fed the colour word (AddPoint changes nothing).
void SectorData::LightSpecular(const SectorPosition* position, SectorVertex* record, Terrain* terrain, int layer) {
    Vector3 specular = {0.0f, 0.0f, 0.0f};
    Vector3 at = *position;
    for (LightRef* ref = record->lights; ref;) {
        LightRef* next = ref->next;
        CLight* light = ref->light;
        if (!(light->flags_ & 0x40)) {
            ref = next;
            continue;
        }
        float attenuation = 1.0f;
        Vector3 offset = light->localPosition_ - at;
        offset.y = static_cast<float>(offset.y);
        offset.z = static_cast<float>(offset.z);
        float distance = Length<Sum3::ZYX>(offset);
        float inverse = static_cast<float>(1.0f / distance);
        Vector3 direction = {static_cast<float>(inverse * offset.x), static_cast<float>(inverse * offset.y),
                             static_cast<float>(inverse * offset.z)};
        const SectorVertex& water = record[kVertices];
        if (layer == 0 && hasWater && water.waterMaterial != 0xFFFF && SignClear(record->depth)) {
            float absorption = terrain->materials[water.waterMaterial].absorption;
            double loss = Product(absorption, record->depth, record->depth, direction.z, direction.z, kWaterFalloff);
            attenuation = AtLeastZero(static_cast<float>(1.0 - loss));
        }
        Vector4 source = {record->materialColour.x, record->materialColour.y, record->materialColour.z, 1.0f};
        Vector4 shaded;
        light->AddPoint(&shaded, attenuation, &source, &record->normal, &direction, distance);
        specular.x = Max(specular.x, shaded.x);
        specular.y = Max(specular.y, shaded.y);
        specular.z = Max(specular.z, shaded.z);
        hasSpecular = 1;
        ref = next;
    }
    std::uint32_t edge = terrain->EdgeFade(position);
    if (hasSpecular) {
        std::uint32_t word = (edge & 0xFF) << 8 | libcrt::Ftol(specular.x * 255.0f);
        word = word << 8 | libcrt::Ftol(specular.y * 255.0f);
        word = word << 8 | libcrt::Ftol(specular.z * 255.0f);
        record->specular = word;
    } else {
        record->specular = (edge & 0xFF) << 24;
    }
}

void SectorData::Light(Terrain* terrain, int skipSun, int x0, int y0, int x1, int y1) {
    hasSpecular = 0;
    // The game's _controlfp(_RC_CHOP, _MCW_RC): chop rounding (Channel's rounding truncates), the word restored at
    // the end; loaded only when it changes.
    const std::uint32_t control = x87::ControlWord();
    const std::uint32_t chop = (control & ~x87::kRoundingBits) | x87::kRoundChop;
    if (chop != control) x87::SetControlWord(chop);
    terrain->lightingSector = this;
    // The GPU lights both layers (renderer.md section 13). The water layer keeps its specular word (LightSpecular:
    // the harness digest reads it); the ground layer gets only the edge fade (the fog factor of its specular word), and the sun's ray march
    // runs only where it could change the sector cache (SunMarchTouchesNothing).
    if (hasWater) {
        for (int y = y0; y <= y1; ++y) {
            terrain->lightingY = y;
            for (int x = x0; x <= x1; ++x) {
                terrain->lightingX = x;
                int i = kVertices + y * kRow + x;
                if (records[i].waterMaterial == 0xFFFF) continue;
                LightSpecular(&vertices[i], &records[i], terrain, 1);
            }
        }
    }
    const bool march = !skipSun && (terrain->landscapeFlags & 0x10) != 0;
    bool quiet = false;   // the march can change nothing for the rest of this call
    for (int y = y0; y <= y1; ++y) {
        terrain->lightingY = y;
        for (int x = x0; x <= x1; ++x) {
            terrain->lightingX = x;
            int i = y * kRow + x;
            if (march && !quiet && !(quiet = SunMarchTouchesNothing(terrain, this, x, y)))
                terrain->SunReaches(&vertices[i]);
            records[i].specular = static_cast<std::uint32_t>(terrain->EdgeFade(&vertices[i])) << 24;
        }
    }
    built |= 4;
    if (chop != control) x87::SetControlWord(control);
    if (!skipSun) relightCountdown = -1;
}

} // namespace terrain
