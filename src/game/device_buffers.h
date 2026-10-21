#pragma once

// The Direct3D device's mesh work buffers (007AEDE0..007AEE3C), shared by its mesh draws
// (device_mesh.cpp) and its flat-coloured draw (device_shadow.cpp): engine aligned arrays
// (lib::AlignedArray). The original's inlined SetSize calls the per-size allocators 004ECB70
// (TL vertices), 00406B30 (words), 00468F70 (16 bytes) and 004ECB50 (bytes), which
// AlignedArray::Allocate matches. See docs/notes/models.md ("Mesh draws").

#include "clip.h"

#include "lib/containers.h"
#include "lib/vecmath.h"

#include <cstdint>

namespace devicebuffers {

// A projected position: x, y, z, rhw (16 bytes).
struct ProjectedVertex : Vector4 {};
static_assert(sizeof(ProjectedVertex) == 16);

// The buffers (device_buffers.cpp): zero at the start, as the static initialisers 004E5C40-004E5DD0 leave them; their
// exit functions free the blocks.
extern lib::AlignedArray<TLVertex> g_vertices;
extern lib::AlignedArray<std::uint16_t> g_clipIndices;
extern lib::AlignedArray<ProjectedVertex> g_projected;
extern lib::AlignedArray<std::uint8_t> g_cornerOutcodes;
extern lib::AlignedArray<std::uint8_t> g_outcodes;

inline lib::AlignedArray<TLVertex>& Vertices() { return g_vertices; }
inline lib::AlignedArray<std::uint16_t>& ClipIndices() { return g_clipIndices; }
inline lib::AlignedArray<ProjectedVertex>& Projected() { return g_projected; }
// An outcode per corner (the clipper's input) and per projected vertex.
inline lib::AlignedArray<std::uint8_t>& CornerOutcodes() { return g_cornerOutcodes; }
inline lib::AlignedArray<std::uint8_t>& Outcodes() { return g_outcodes; }

} // namespace devicebuffers
