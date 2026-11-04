#pragma once

// The records a mesh draw reads (MeshDraw, ShadowMesh). The game's meshes hold them as their files do (.fig, .lnk).

#include <cstdint>

namespace render {

// A texture coordinate (u, v).
struct TexCoord {
    float u, v;
};

// A triangle corner: the index of its position, of its normal (which also picks its colour, specular and lighting
// word; the interface's sprites write 0) and of its texture coordinate.
struct MeshCorner {
    std::uint16_t position;
    std::uint16_t normal;
    std::uint16_t texCoord;
};

static_assert(sizeof(TexCoord) == 8 && sizeof(MeshCorner) == 6);   // as the files hold them

} // namespace render
