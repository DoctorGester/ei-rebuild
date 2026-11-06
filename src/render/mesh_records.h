#pragma once

// The records a mesh draw reads (MeshDraw, ShadowMesh). The game's meshes hold them as their files do (.fig, .lnk).

#include "lib/bytes.h"
#include "lib/vecmath.h"

#include <cstdint>

namespace render {

// Four vertex positions in SoA form for the projections: x[4], y[4], z[4] and w[4] (the loaders set w to
// 1.0). Vertex i of a mesh is component i & 3 of packet i >> 2. Also the moved positions the part drawers
// build. `block` is four rows of lanes, not a matrix (check_cleanup skips the name).
struct VertexPacket {
    float block[4][4] = {};
    // The rows by name (the interface sprites' quads: the four corners' x, y, z (0) and w (1)).
    float (&x())[4] { return block[0]; }
    float (&y())[4] { return block[1]; }
    float (&z())[4] { return block[2]; }
    float (&w())[4] { return block[3]; }
    const float (&x() const)[4] { return block[0]; }
    const float (&y() const)[4] { return block[1]; }
    const float (&z() const)[4] { return block[2]; }
    const float (&w() const)[4] { return block[3]; }
};

// Four normals in the same SoA form (mirrored together with the positions); the loaders copy them verbatim.
struct NormalPacket {
    float block[4][4] = {};
};

// Vertex `i` of a mesh's packets (positions or normals): lane i % 4 of packet i / 4.
template <class Packet>
Vector3 PacketVertex(const Packet* packets, int i) {
    const Packet& packet = packets[i / 4];
    const int lane = i % 4;
    return Vector3{packet.block[0][lane], packet.block[1][lane], packet.block[2][lane]};
}

// A texture coordinate (u, v).
struct TexCoord {
    float u = 0.0f, v = 0.0f;
};

// A triangle corner: the index of its position, of its normal (which also picks its colour, specular and lighting
// word; the interface's sprites write 0) and of its texture coordinate.
struct MeshCorner {
    std::uint16_t position = 0;
    std::uint16_t normal = 0;
    std::uint16_t texCoord = 0;
};

// Where the renderer keeps a figure mesh's copies on the GPU (drawn; read as a caster): 1 + their slot, 0 for none.
// Held in the mesh (FigureMesh::meshSlots) and written by the renderer, so a draw finds its copy without a lookup.
struct MeshSlots {
    std::uint32_t drawn = 0, caster = 0;
};

static_assert(sizeof(TexCoord) == 8 && sizeof(MeshCorner) == 6 && sizeof(VertexPacket) == 0x40 &&
              sizeof(NormalPacket) == 0x40);   // as the files hold them
constexpr bool NoPadding(const VertexPacket*) { return lib::FieldsFill<&VertexPacket::block>(); }
constexpr bool NoPadding(const NormalPacket*) { return lib::FieldsFill<&NormalPacket::block>(); }
constexpr bool NoPadding(const TexCoord*) { return lib::FieldsFill<&TexCoord::u, &TexCoord::v>(); }

} // namespace render
