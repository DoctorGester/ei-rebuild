// Terrain vertex lighting: SectorData::Light runs over a rectangle of a sector's vertices with the FPU
// switched to chop rounding (_controlfp(0x300, 0x300)), so every operation below, including the FISTPs of
// the colour channels, rounds towards zero there. Per vertex: ShadeSun sets the lit colour from the
// ambient light and the sun, and LightVertex adds the point lights registered with the vertex, packing the
// colour and specular words the terrain draw sends to the device. On the ground layer both dim light under
// water by 1 - absorption * depth^2 * falloff (times the light's vertical component squared).
#include "terrain.h"
#include "terrain_sector.h"
#include "spell.h"

#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/vecmath.h"
#include "game_data.h"
#include "render/render.h"

#include <cstdint>
#include <cstring>

namespace terrain {
namespace {

constexpr int kVertices = 33 * 33;     // per layer
constexpr int kRow = 33;

// 1 / the water depth scale (set by a static initialiser).
constexpr float kWaterFalloff = /*007C8908*/ 0.06666667f;   // a static initialiser: 1 / 15

// The light's shading of a colour (r, g, b, a) for a vertex normal is CLight::AddDirectional (the sun: its
// direction +0xD4) or AddPoint (a point light at `distance` along the unit `direction`). Both clamp each
// channel to 1 and copy the alpha through.

// Select idiom (FLD a; FCOMP b; FNSTSW; SHL 0x17; SAR 0x1F: C0, "a < b or unordered", picks b).
float Max(float a, float b) { return a >= b ? a : b; }
float AtMostOne(float v) { return v <= 1.0f ? v : 1.0f; }
float AtLeastZero(float v) { return v >= 0.0f ? v : 0.0f; }

bool SignClear(float v) {
    std::uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    return (bits & 0x80000000u) == 0;
}

// Channel bytes: the value times 255 stored as a float, then FISTP (here: chop).
std::uint32_t Channel(float v) { return static_cast<std::uint32_t>(x87::fistp(static_cast<float>(v * 255.0f))); }

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
            const Terrain::SectorCache::iterator node = terrain->sectorGrid[sx][sy];
            if (node == Terrain::SectorCache::iterator{} || (*node)->lastUsed < terrain->frame) return false;
        }
    return true;
}

} // namespace

void SectorData::ShadeSun(CLight* sun, const SectorPosition* position, SectorVertex* record, Terrain* terrain,
                          int layer) {
    bool shadows = layer == 0 && (terrain->landscapeFlags & 0x10) != 0;
    float attenuation = 1.0f;
    Vector3 ambient = {terrain->ambient.x, terrain->ambient.y, terrain->ambient.z};
    Vector3 colour = record->materialColour;
    const SectorVertex& water = record[kVertices];
    if (hasWater && layer == 0 && water.waterMaterial != 0xFFFF && SignClear(record->depth)) {
        float absorption = terrain->materials[water.waterMaterial].absorption;
        float loss = static_cast<float>(Product(kWaterFalloff, absorption, record->depth, record->depth));
        float dim = AtLeastZero(static_cast<float>(1.0f - loss));
        ambient.x = static_cast<float>(dim * ambient.x);
        ambient.y = static_cast<float>(dim * ambient.y);
        ambient.z = static_cast<float>(dim * ambient.z);
        float height = sun->localPosition_.z;
        attenuation = AtLeastZero(static_cast<float>(1.0 - Product(height, height, loss)));
        colour.x = Max(water.materialColour.x, record->materialColour.x);
        colour.y = Max(water.materialColour.y, record->materialColour.y);
        colour.z = Max(water.materialColour.z, record->materialColour.z);
    }
    if (shadows && terrain->SunReaches(position) == 0) {
        record->lit.x = AtMostOne(static_cast<float>(ambient.x + colour.x));
        record->lit.y = AtMostOne(static_cast<float>(ambient.y + colour.y));
        record->lit.z = AtMostOne(static_cast<float>(ambient.z + colour.z));
        return;
    }
    // The fourth component is an uninitialised stack slot; only copied through.
    Vector4 shaded;
    Vector4 source = {colour.x, colour.y, colour.z, 0.0f};
    sun->AddDirectional(&shaded, attenuation, &source, &record->normal);
    record->lit.x = Max(shaded.x, ambient.x);
    record->lit.y = Max(shaded.y, ambient.y);
    record->lit.z = Max(shaded.z, ambient.z);
}

void SectorData::LightVertex(const SectorPosition* position, SectorVertex* record, Terrain* terrain, int layer) {
    Vector3 colour = record->lit;
    Vector3 specular = {0.0f, 0.0f, 0.0f};
    Vector3 at = *position;
    for (LightRef* ref = record->lights; ref;) {
        LightRef* next = ref->next;
        CLight* light = ref->light;
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
        if (light->flags_ & 0x40) {
            specular.x = Max(specular.x, shaded.x);
            specular.y = Max(specular.y, shaded.y);
            specular.z = Max(specular.z, shaded.z);
            hasSpecular = 1;
        } else {
            colour.x = Max(colour.x, shaded.x);
            colour.y = Max(colour.y, shaded.y);
            colour.z = Max(colour.z, shaded.z);
        }
        ref = next;
    }
    // FISTP results are or-ed whole (a negative channel would set the bytes above it).
    std::uint32_t r = Channel(AtMostOne(Max(colour.x, specular.x)));
    std::uint32_t g = Channel(AtMostOne(Max(colour.y, specular.y)));
    std::uint32_t b = Channel(AtMostOne(Max(colour.z, specular.z)));
    record->colour = ((((static_cast<std::uint32_t>(record->materialByte) << 8) | r) << 8 | g) << 8) | b;
    std::uint32_t edge = terrain->EdgeFade(position);
    if (hasSpecular) {
        std::uint32_t word = (edge & 0xFF) << 8 | gamecrt::Ftol(specular.x * 255.0f);
        word = word << 8 | gamecrt::Ftol(specular.y * 255.0f);
        word = word << 8 | gamecrt::Ftol(specular.z * 255.0f);
        record->specular = word;
    } else {
        record->specular = (edge & 0xFF) << 24;
    }
}

// LightVertex's specular word alone, for the water layer when the GPU lights the colours: the harness
// digest's "water" reads that word (the edge fade, then the specular lights; Animate writes the crests over
// its colour bytes). The same operations as LightVertex for the lights with flag 0x40; the others only fed
// the colour word (AddPoint changes nothing).
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
        std::uint32_t word = (edge & 0xFF) << 8 | gamecrt::Ftol(specular.x * 255.0f);
        word = word << 8 | gamecrt::Ftol(specular.y * 255.0f);
        word = word << 8 | gamecrt::Ftol(specular.z * 255.0f);
        record->specular = word;
    } else {
        record->specular = (edge & 0xFF) << 24;
    }
}

void SectorData::Light(CLight* sun, Terrain* terrain, int skipSun, int x0, int y0, int x1, int y1) {
    hasSpecular = 0;
    unsigned control = libcrt::Controlfp(0, 0);
    libcrt::Controlfp(0x300, 0x300);
    terrain->lightingSector = this;
    // With the new renderer the GPU lights both layers (render::GpuTerrainLighting). The water layer keeps
    // its specular word (LightSpecular: the harness digest reads it); the ground layer gets only the edge
    // fade (the fog factor of its specular word), and the sun's ray march runs only where it could change
    // the sector cache (SunMarchTouchesNothing).
    const bool gpu = render::GpuTerrainLighting();
    if (hasWater) {
        for (int y = y0; y <= y1; ++y) {
            terrain->lightingY = y;
            for (int x = x0; x <= x1; ++x) {
                terrain->lightingX = x;
                int i = kVertices + y * kRow + x;
                if (records[i].waterMaterial == 0xFFFF) continue;
                if (gpu) {
                    LightSpecular(&vertices[i], &records[i], terrain, 1);
                    continue;
                }
                if (!skipSun) ShadeSun(sun, &vertices[i], &records[i], terrain, 1);
                LightVertex(&vertices[i], &records[i], terrain, 1);
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
            if (gpu) {
                if (march && !quiet && !(quiet = SunMarchTouchesNothing(terrain, this, x, y)))
                    terrain->SunReaches(&vertices[i]);
                records[i].specular = static_cast<std::uint32_t>(terrain->EdgeFade(&vertices[i])) << 24;
                continue;
            }
            if (!skipSun) ShadeSun(sun, &vertices[i], &records[i], terrain, 0);
            LightVertex(&vertices[i], &records[i], terrain, 0);
        }
    }
    built |= 4;
    libcrt::Controlfp(control, 0xFFFFFFFF);
    if (!skipSun) relightCountdown = -1;
}

} // namespace terrain
