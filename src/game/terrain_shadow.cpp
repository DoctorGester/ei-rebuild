// The landscape's light tests (terrain.h): the sun's shadow test of the vertex lighting (006C92C0,
// from SectorData::ShadeSun) and the fade of the light at the map's edges (006CB020, from the vertex
// lighting, the figures' draws and the ground prints).
#include "terrain.h"
#include "terrain_sector.h"
#include "spell.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/vecmath.h"
#include "game_data.h"

#include <cstdint>
#include <cstring>

namespace {

constexpr int kRow = 33;            // vertices per sector row
constexpr int kLastCell = 31;       // the column / row a ray enters a sector at from above
constexpr int kLoaded = 1;          // SectorAt's stage: the vertex positions

// The width of the fade at the map's edges (0079BD34; the interface's screens set it).
float MapEdgeFade() { return ei::GameData<const float>(0x0079BD34); }

std::uint32_t Bits(float value) {
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}
bool SignSet(float value) { return (Bits(value) & 0x80000000u) != 0; }

// The original's select (FLD 0; FCOMP v; C0 picks 0): 0 for a positive or unordered value.
float AtMostZero(float value) { return 0.0f >= value ? value : 0.0f; }

} // namespace

int Terrain::SunReaches(const terrain::SectorPosition* position) {
    terrain::SectorData* sector = lightingSector;
    int column = lightingX;
    int row = lightingY;
    int sx = sector->sx;
    int sy = sector->sy;
    const terrain::SectorPosition* vertex = sector->vertices.data + (row * kRow + column);

    // The line's steps and the height gained per step, turned to run upwards in both directions.
    int stepX = sunStepX;
    int stepY = sunStepY;
    float slopeX = sunSlopeX;
    float slopeY = sunSlopeY;
    int directionX = 1;
    int directionY = 1;
    if (stepX < 0 || (stepX == 0 && SignSet(slopeX))) {
        slopeX = -slopeX;
        directionX = -1;
        stepX = -stepX;
    }
    if (stepY < 0 || (stepY == 0 && SignSet(slopeY))) {
        slopeY = -slopeY;
        directionY = -1;
        stepY = -stepY;
    }
    // A ray that does not rise: lit when the sun's direction points down.
    if (SignSet(slopeX) || SignSet(slopeY)) return static_cast<int>(Bits(sun->direction_.z) & 0x80000000u);

    const int length = stepY > stepX ? stepY : stepX;
    float rayHeight = position->z;
    int error = 0;
    for (int steps = 0; steps < length; ++steps) {
        // The original compares the two floats' bits as unsigned integers.
        if (Bits(vertex->z) > Bits(rayHeight)) return 0;
        bool crossed = false;
        if (error <= 2 * stepX - stepY) {
            rayHeight = static_cast<float>(rayHeight + slopeX);
            column += directionX;
            error += 2 * stepY;
            if (column == -1) {
                column = kLastCell;
                --sx;
                crossed = true;
            } else if (column == kRow) {
                column = 1;
                ++sx;
                crossed = true;
            } else {
                vertex += directionX;
            }
        }
        // A step that left the sector in x does not also step in y.
        if (!crossed && (error >= stepX || stepX <= stepY)) {
            rayHeight = static_cast<float>(rayHeight + slopeY);
            row += directionY;
            error -= 2 * stepX;
            if (row == -1) {
                row = kLastCell;
                --sy;
                crossed = true;
            } else if (row == kRow) {
                row = 1;
                ++sy;
                crossed = true;
            } else {
                vertex += directionY * kRow;
            }
        }
        if (crossed) {
            auto* next = SectorAt(sx, sy, kLoaded, 1);
            if (!next) return 1;
            vertex = next->vertices.data + (row * kRow + column);
        }
    }
    return 1;
}
EI_REWRITTEN(0x006C92C0, Terrain::SunReaches);

std::uint8_t Terrain::EdgeFade(const Vector3* point) {
    if ((landscapeFlags & 4) == 0) return 0xFF;
    const float fade = MapEdgeFade();
    // How far inside the band's inner border the point is, per edge (negative: in the band).
    float left = static_cast<float>(point->x - fade - 1.0f);
    float bottom = static_cast<float>(point->y - fade - 1.0f);
    float right = static_cast<float>(width - fade - point->x - 1.0f);
    float top = static_cast<float>(height - fade - point->y - 1.0f);
    if (!(SignSet(left) || SignSet(bottom) || SignSet(right) || SignSet(top))) return 0xFF;
    left = AtMostZero(left);
    bottom = AtMostZero(bottom);
    right = AtMostZero(right);
    top = AtMostZero(top);
    // ((top^2 + right^2) + bottom^2) + left^2, the partial sums rounded to float (as at 24 bits).
    float sum = static_cast<float>(top * top + right * right);
    sum = static_cast<float>(sum + bottom * bottom);
    float depth = static_cast<float>(std::sqrt(sum + left * left));
    depth = fade >= depth ? depth : fade;
    int amount = x87::fistp(static_cast<float>(depth / fade * 255.0f));
    return static_cast<std::uint8_t>(0xFF - static_cast<std::uint8_t>(amount));
}
EI_REWRITTEN(0x006CB020, Terrain::EdgeFade);
