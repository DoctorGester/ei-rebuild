#pragma once

// The Direct3D device's mesh work buffers, shared by its mesh draws and its flat-coloured draw
//: engine aligned arrays (lib::AlignedArray). The game's inlined SetSize calls per-size
// allocators (TL vertices, words, 16 bytes, bytes), which AlignedArray::Allocate matches. See
// docs/notes/models.md ("Mesh draws").

#include "clip.h"

#include "lib/containers.h"
#include "lib/vecmath.h"

#include <cstdint>

namespace devicebuffers {

struct ProjectedVertex : Vector4 {};
static_assert(sizeof(ProjectedVertex) == 16);

// The buffers: zero at the start; their exit functions free the blocks.
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
