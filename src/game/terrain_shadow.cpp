// The fade of the light at the map's edges (terrain.h): from the sector build (the fog factor of the vertices'
// specular words), the figures' draws and the ground prints.
#include "terrain.h"
#include "options.h"

#include "lib/float_select.h"
#include "lib/rounding.h"

#include <cmath>
#include <cstdint>

namespace terrain {

namespace {

// 255 * depth / fade for a point `depth` inside the fade band, 0 outside it.
float FadeAmount(const Terrain& terrain, const Vector3& point) {
    if (!lib::Has(terrain.landscapeFlags, LandscapeFlag::EdgeFade)) return 0.0f;
    const float fade = options::g_options.borderFogDistance;
    const float width = terrain.width;
    const float height = terrain.height;
    // How far inside the band's inner border the point is, per edge (negative: in the band).
    float left = point.x - fade - 1.0f;
    float bottom = point.y - fade - 1.0f;
    float right = width - fade - point.x - 1.0f;
    float top = height - fade - point.y - 1.0f;
    if (!(SignBit(left) || SignBit(bottom) || SignBit(right) || SignBit(top))) return 0.0f;
    left = MinKeepA(0.0f, left);
    bottom = MinKeepA(0.0f, bottom);
    right = MinKeepA(0.0f, right);
    top = MinKeepA(0.0f, top);
    float depth = std::sqrt(top * top + right * right + bottom * bottom + left * left);
    depth = fade >= depth ? depth : fade;
    return depth / fade * 255.0f;
}

} // namespace

std::uint8_t Terrain::EdgeFade(const Vector3& point) {
    return static_cast<std::uint8_t>(0xFF - static_cast<std::uint8_t>(lib::RoundEven(FadeAmount(*this, point))));
}

std::uint8_t Terrain::EdgeFadeDown(const Vector3& point) {
    return static_cast<std::uint8_t>(0xFF - static_cast<std::uint8_t>(static_cast<int>(FadeAmount(*this, point))));
}

} // namespace terrain
