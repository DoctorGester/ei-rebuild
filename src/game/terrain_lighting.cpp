// Terrain vertex lighting (006C31C0-006C3C7F): SectorData::Light runs over a rectangle of a sector's
// vertices with the FPU switched to chop rounding (_controlfp(0x300, 0x300)), so every operation
// below, including the FISTPs of the colour channels, rounds towards zero there.
//
// Per vertex: ShadeSun sets the lit colour (+0x37) from the ambient light and the sun, and
// LightVertex adds the point lights registered with the vertex (006CAC10), packing the colour and
// specular words the terrain draw sends to the device. On the ground layer both dim light under
// water by 1 - absorption * depth^2 * falloff (times the light's vertical component squared).
#include "terrain_sector.h"
#include "spell.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/vecmath.h"
#include "originals_effects.h"

#include <cstdint>
#include <cstring>

namespace terrain {
namespace {

constexpr int kVertices = 33 * 33;     // per layer
constexpr int kRow = 33;

// 1 / the water depth scale (007C8908, set by a static initialiser from 0074417C).
float WaterFalloff() { return *reinterpret_cast<const float*>(0x007C8908); }

// The light's shading of a colour (r, g, b, a) for a vertex normal: 0068D310 directional (the sun:
// its direction +0xD4), 0068D5A0 a point light at `distance` along the unit `direction`. Both clamp
// each channel to 1 and copy the alpha through (RET 0x10 / 0x18).
__declspec(naked) void __fastcall ShadeDirectional(CLight* /*light*/, void*, float* /*out*/, float /*attenuation*/,
                                                   const float* /*colour*/, const float* /*normal*/)
    EI_ORIGINAL(0x0068D310)
__declspec(naked) void __fastcall ShadePoint(CLight* /*light*/, void*, float* /*out*/, float /*attenuation*/,
                                             const float* /*colour*/, const float* /*normal*/,
                                             const Vector3* /*direction*/, float /*distance*/) EI_ORIGINAL(0x0068D5A0)

// The original's select idioms (FLD a; FCOMP b; FNSTSW; SHL 0x17; SAR 0x1F: C0, "a < b or
// unordered", picks b).
float Max(float a, float b) { return !(a >= b) ? b : a; }
float AtMostOne(float v) { return !(1.0f >= v) ? 1.0f : v; }
float AtLeastZero(float v) { return !(v >= 0.0f) ? 0.0f : v; }

bool SignClear(float v) {
    std::uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    return (bits & 0x80000000u) == 0;
}

// Channel bytes: the value times 255 stored as a float, then FISTP (here: chop).
std::uint32_t Channel(float v) { return static_cast<std::uint32_t>(x87::fistp(RoundToFloat(v * 255.0f))); }

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
        float loss = RoundToFloat(Product(WaterFalloff(), absorption, record->depth, record->depth));
        float dim = AtLeastZero(RoundToFloat(1.0f - loss));
        ambient.x = RoundToFloat(dim * ambient.x);
        ambient.y = RoundToFloat(dim * ambient.y);
        ambient.z = RoundToFloat(dim * ambient.z);
        float height = sun->position_.z;
        attenuation = AtLeastZero(RoundToFloat(1.0 - Product(height, height, loss)));
        colour.x = Max(water.materialColour.x, record->materialColour.x);
        colour.y = Max(water.materialColour.y, record->materialColour.y);
        colour.z = Max(water.materialColour.z, record->materialColour.z);
    }
    if (shadows && terrain->SunReachesAtOriginal(position) == 0) {
        record->lit.x = AtMostOne(RoundToFloat(ambient.x + colour.x));
        record->lit.y = AtMostOne(RoundToFloat(ambient.y + colour.y));
        record->lit.z = AtMostOne(RoundToFloat(ambient.z + colour.z));
        return;
    }
    // The fourth component is an uninitialised stack slot in the original; only copied through.
    float shaded[4];
    float source[4] = {colour.x, colour.y, colour.z, 0.0f};
    ShadeDirectional(sun, nullptr, shaded, attenuation, source, &record->normal.x);
    record->lit.x = Max(shaded[0], ambient.x);
    record->lit.y = Max(shaded[1], ambient.y);
    record->lit.z = Max(shaded[2], ambient.z);
}

void SectorData::LightVertex(const SectorPosition* position, SectorVertex* record, Terrain* terrain, int layer) {
    Vector3 colour = record->lit;
    Vector3 specular = {0.0f, 0.0f, 0.0f};
    Vector3 at = *position;
    for (LightRef* ref = record->lights; ref;) {
        LightRef* next = ref->next;
        CLight* light = ref->light;
        float attenuation = 1.0f;
        Vector3 offset = light->position_ - at;
        offset.y = RoundToFloat(offset.y);
        offset.z = RoundToFloat(offset.z);
        float distance = Length<Sum3::ZYX>(offset);
        float inverse = RoundToFloat(1.0f / distance);
        Vector3 direction = {RoundToFloat(inverse * offset.x), RoundToFloat(inverse * offset.y),
                             RoundToFloat(inverse * offset.z)};
        const SectorVertex& water = record[kVertices];
        if (layer == 0 && hasWater && water.waterMaterial != 0xFFFF && SignClear(record->depth)) {
            float absorption = terrain->materials[water.waterMaterial].absorption;
            double loss = Product(absorption, record->depth, record->depth, direction.z, direction.z, WaterFalloff());
            attenuation = AtLeastZero(RoundToFloat(1.0 - loss));
        }
        float source[4] = {record->materialColour.x, record->materialColour.y, record->materialColour.z, 1.0f};
        float shaded[4];
        ShadePoint(light, nullptr, shaded, attenuation, source, &record->normal.x, &direction, distance);
        if (light->flags_ & 0x40) {
            specular.x = Max(specular.x, shaded[0]);
            specular.y = Max(specular.y, shaded[1]);
            specular.z = Max(specular.z, shaded[2]);
            hasSpecular = 1;
        } else {
            colour.x = Max(colour.x, shaded[0]);
            colour.y = Max(colour.y, shaded[1]);
            colour.z = Max(colour.z, shaded[2]);
        }
        ref = next;
    }
    // FISTP results are or-ed whole (a negative channel would set the bytes above it).
    std::uint32_t r = Channel(AtMostOne(Max(colour.x, specular.x)));
    std::uint32_t g = Channel(AtMostOne(Max(colour.y, specular.y)));
    std::uint32_t b = Channel(AtMostOne(Max(colour.z, specular.z)));
    record->colour = ((((static_cast<std::uint32_t>(record->materialByte) << 8) | r) << 8 | g) << 8) | b;
    std::uint32_t edge = IsLit(terrain, nullptr, position);
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
    unsigned control = gamecrt::controlfp(0, 0);
    gamecrt::controlfp(0x300, 0x300);
    terrain->lightingSector = this;
    if (hasWater) {
        for (int y = y0; y <= y1; ++y) {
            terrain->lightingY = y;
            for (int x = x0; x <= x1; ++x) {
                terrain->lightingX = x;
                int i = kVertices + y * kRow + x;
                if (records[i].waterMaterial == 0xFFFF) continue;
                if (!skipSun) ShadeSun(sun, &vertices[i], &records[i], terrain, 1);
                LightVertex(&vertices[i], &records[i], terrain, 1);
            }
        }
    }
    for (int y = y0; y <= y1; ++y) {
        terrain->lightingY = y;
        for (int x = x0; x <= x1; ++x) {
            terrain->lightingX = x;
            int i = y * kRow + x;
            if (!skipSun) ShadeSun(sun, &vertices[i], &records[i], terrain, 0);
            LightVertex(&vertices[i], &records[i], terrain, 0);
        }
    }
    built |= 4;
    gamecrt::controlfp(control, 0xFFFFFFFF);
    if (!skipSun) relightCountdown = -1;
}

EI_REPLACE_METHOD(0x006C3A00, SectorData::Light);
EI_REPLACE_METHOD(0x006C36B0, SectorData::ShadeSun);
EI_REPLACE_METHOD(0x006C31C0, SectorData::LightVertex);

} // namespace terrain

__declspec(naked) int Terrain::SunReachesAtOriginal(const terrain::SectorPosition*) EI_ORIGINAL(0x006C92C0)
