// The landscape's light tests (terrain.h): the sun's shadow test of the vertex lighting (SunReaches) and the
// fade of the light at the map's edges (from the vertex lighting, the figures' draws and the ground prints).
#include "terrain.h"
#include "terrain_sector.h"
#include "options.h"
#include "spell.h"

#include "ei/x87.h"
#include "lib/float_select.h"
#include "lib/vecmath.h"

#include <bit>
#include <cmath>
#include <cstdint>

using terrain::kSectorRow;

namespace {

constexpr int kLastCell = 31;       // the column / row a ray enters a sector at from above

} // namespace

terrain::SunRay terrain::SunRayOf(const Terrain& terrain) {
    SunRay ray = {terrain.sunStepX, terrain.sunStepY, 1, 1, terrain.sunSlopeX, terrain.sunSlopeY};
    if (ray.stepX < 0 || (ray.stepX == 0 && SignBit(ray.slopeX))) {
        ray.slopeX = -ray.slopeX;
        ray.directionX = -1;
        ray.stepX = -ray.stepX;
    }
    if (ray.stepY < 0 || (ray.stepY == 0 && SignBit(ray.slopeY))) {
        ray.slopeY = -ray.slopeY;
        ray.directionY = -1;
        ray.stepY = -ray.stepY;
    }
    return ray;
}

bool terrain::SunRay::Flat() const { return SignBit(slopeX) || SignBit(slopeY); }

bool Terrain::SunReaches(const terrain::SectorData& sector, int column, int row) {
    int sx = sector.sx;
    int sy = sector.sy;
    const terrain::SectorPosition* vertex = sector.vertices.data() + (row * kSectorRow + column);

    // The line's steps and the height gained per step, turned to run upwards in both directions.
    const terrain::SunRay ray = terrain::SunRayOf(*this);
    // A ray that does not rise: lit when the sun's direction points down.
    if (ray.Flat()) return SignBit(sun->direction_.z);

    const int length = ray.Length();
    float rayHeight = vertex->z;
    int error = 0;
    for (int steps = 0; steps < length; ++steps) {
        // The two floats' bits are compared as unsigned integers.
        if (std::bit_cast<std::uint32_t>(vertex->z) > std::bit_cast<std::uint32_t>(rayHeight)) return false;
        bool crossed = false;
        if (error <= 2 * ray.stepX - ray.stepY) {
            rayHeight = static_cast<float>(rayHeight + ray.slopeX);
            column += ray.directionX;
            error += 2 * ray.stepY;
            if (column == -1) {
                column = kLastCell;
                --sx;
                crossed = true;
            } else if (column == kSectorRow) {
                column = 1;
                ++sx;
                crossed = true;
            } else {
                vertex += ray.directionX;
            }
        }
        // A step that left the sector in x does not also step in y.
        if (!crossed && (error >= ray.stepX || ray.stepX <= ray.stepY)) {
            rayHeight = static_cast<float>(rayHeight + ray.slopeY);
            row += ray.directionY;
            error -= 2 * ray.stepX;
            if (row == -1) {
                row = kLastCell;
                --sy;
                crossed = true;
            } else if (row == kSectorRow) {
                row = 1;
                ++sy;
                crossed = true;
            } else {
                vertex += ray.directionY * kSectorRow;
            }
        }
        if (crossed) {
            auto* next = SectorAt(sx, sy, SectorStage::Loaded, true);
            if (!next) return true;
            vertex = next->vertices.data() + (row * kSectorRow + column);
        }
    }
    return true;
}

std::uint8_t Terrain::EdgeFade(const Vector3& point) {
    if (!lib::Has(landscapeFlags, LandscapeFlag::EdgeFade)) return 0xFF;
    const float fade = options::g_options.borderFogDistance_;
    // How far inside the band's inner border the point is, per edge (negative: in the band).
    float left = static_cast<float>(point.x - fade - 1.0f);
    float bottom = static_cast<float>(point.y - fade - 1.0f);
    float right = static_cast<float>(width - fade - point.x - 1.0f);
    float top = static_cast<float>(height - fade - point.y - 1.0f);
    if (!(SignBit(left) || SignBit(bottom) || SignBit(right) || SignBit(top))) return 0xFF;
    left = MinKeepA(0.0f, left);
    bottom = MinKeepA(0.0f, bottom);
    right = MinKeepA(0.0f, right);
    top = MinKeepA(0.0f, top);
    // ((top^2 + right^2) + bottom^2) + left^2, the partial sums rounded to float (as at 24 bits).
    float sum = static_cast<float>(top * top + right * right);
    sum = static_cast<float>(sum + bottom * bottom);
    float depth = static_cast<float>(std::sqrt(sum + left * left));
    depth = fade >= depth ? depth : fade;
    int amount = x87::fistp(static_cast<float>(depth / fade * 255.0f));
    return static_cast<std::uint8_t>(0xFF - static_cast<std::uint8_t>(amount));
}
