// The fade of the light at the map's edges (terrain.h): from the sector build (the fog factor of the vertices'
// specular words), the figures' draws and the ground prints.
#include "terrain.h"
#include "options.h"

#include "ei/x87.h"
#include "lib/float_select.h"

#include <cmath>
#include <cstdint>

namespace terrain {

std::uint8_t Terrain::EdgeFade(const Vector3& point) {
    if (!lib::Has(landscapeFlags, LandscapeFlag::EdgeFade)) return 0xFF;
    const float fade = options::g_options.borderFogDistance;
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

} // namespace terrain
