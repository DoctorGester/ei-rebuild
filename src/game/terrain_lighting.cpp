// Terrain vertex lighting: SectorData::Light runs over a rectangle of a sector's vertices with the FPU
// switched to chop rounding, so every operation below rounds towards zero there. The GPU lights the colours
// (renderer.md section 13); the CPU writes only what the game reads: the edge fade and the water layer's
// specular word.
#include "terrain.h"
#include "terrain_sector.h"
#include "spell.h"

#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/vecmath.h"

#include <cstdint>

namespace terrain {
namespace {

// Whether the sun's ray march (Terrain::SunReaches) from vertex (column, row) of `sector` can change
// nothing of the game's. Its answer feeds only the vertex colours, which the GPU lighting does not read;
// but each sector it steps into is looked up with SectorAt (loaded when missing, its use and the cache's
// order refreshed), which feeds the sector cache (and through its builds the global ISAAC draws). Those
// lookups change nothing when every sector the ray can step into is cached and was used this frame already:
// then the march is left out. Bounds as SunReaches steps: at most the ray's length in steps, each moving at
// most one vertex along x and one along y, a sector crossed when the column (row) passes 32 going up or 0
// going down.
bool SunMarchTouchesNothing(const Terrain& terrain, const SectorData& sector, int column, int row) {
    const SunRay ray = SunRayOf(terrain);
    if (ray.Flat()) return true;   // it returns before any step
    const int length = ray.Length();
    auto crossings = [length](int at, int direction) {
        int toFirst = direction > 0 ? kSectorRow - at : at + 1;   // moves to the first crossing
        return length >= toFirst ? 1 + (length - toFirst) / (kSectorRow - 1) : 0;
    };
    const int acrossX = crossings(column, ray.directionX), acrossY = crossings(row, ray.directionY);
    for (int j = 0; j <= acrossY; ++j)
        for (int i = 0; i <= acrossX; ++i) {
            if (i == 0 && j == 0) continue;   // its own sector is read directly
            int sx = sector.sx + i * ray.directionX, sy = sector.sy + j * ray.directionY;
            if (sx < 0 || sy < 0 || sx >= terrain.sectorsX || sy >= terrain.sectorsY) continue;   // SectorAt: none
            const SectorData* cached = terrain.CachedSector(sx, sy);
            if (!cached || cached->lastUsed < terrain.frame) return false;
        }
    return true;
}

} // namespace

// The harness digest's "water" reads this word (Animate writes the crests over its colour bytes). The other
// lights only fed the colour word.
void SectorData::LightSpecular(int i) {
    const SectorPosition& position = vertices[i];
    SectorVertex& record = records[i];
    Vector3 specular = {0.0f, 0.0f, 0.0f};
    for (const LightRef* ref = record.lights; ref; ref = ref->next) {
        CLight* light = ref->light;
        if (!(light->flags_ & LightFlag::Specular)) continue;
        Vector3 offset = light->localPosition_ - position;
        float distance = Length<Sum3::ZYX>(offset);
        float inverse = static_cast<float>(1.0f / distance);
        Vector3 direction = {static_cast<float>(inverse * offset.x), static_cast<float>(inverse * offset.y),
                             static_cast<float>(inverse * offset.z)};
        Vector4 source = {record.materialColour.x, record.materialColour.y, record.materialColour.z, 1.0f};
        Vector4 shaded;
        light->AddPoint(&shaded, 1.0f, &source, &record.normal, &direction, distance);
        specular.x = MaxKeepB(specular.x, shaded.x);
        specular.y = MaxKeepB(specular.y, shaded.y);
        specular.z = MaxKeepB(specular.z, shaded.z);
    }
    // Without a specular light the channels stay 0.
    record.specular = PackArgb(owner->EdgeFade(position), libcrt::Ftol(specular.x * 255.0f),
                               libcrt::Ftol(specular.y * 255.0f), libcrt::Ftol(specular.z * 255.0f));
}

void SectorData::Light(bool skipSun, int x0, int y0, int x1, int y1) {
    const x87::ScopedRounding chop(x87::kRoundChop);
    // The GPU lights both layers (renderer.md section 13). The water layer keeps its specular word (LightSpecular:
    // the harness digest reads it); the ground layer gets only the edge fade (the fog factor of its specular
    // word), and the sun's ray march runs only where it could change the sector cache (SunMarchTouchesNothing).
    if (hasWater) {
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                int i = kSectorVertices + y * kSectorRow + x;
                if (records[i].waterMaterial == SectorVertex::kNoWaterMaterial) continue;
                LightSpecular(i);
            }
        }
    }
    const bool march = !skipSun && lib::Has(owner->landscapeFlags, LandscapeFlag::SunShadows);
    bool quiet = false;   // the march can change nothing for the rest of this call
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            int i = y * kSectorRow + x;
            if (march && !quiet) {
                quiet = SunMarchTouchesNothing(*owner, *this, x, y);
                if (!quiet) owner->SunReaches(*this, x, y);
            }
            records[i].specular = static_cast<std::uint32_t>(owner->EdgeFade(vertices[i])) << 24;
        }
    }
    built |= SectorStage::Lit;
    if (!skipSun) relightCountdown = -1;
}

} // namespace terrain
