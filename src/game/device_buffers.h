#pragma once

// The Direct3D device's mesh work buffers (vectors), shared by its mesh draws and its flat-coloured draw.
// See docs/notes/models.md ("Mesh draws").

#include "clip.h"

#include "lib/containers.h"
#include "lib/vecmath.h"

#include <cstdint>
#include <vector>

namespace devicebuffers {

struct ProjectedVertex : Vector4 {};
static_assert(sizeof(ProjectedVertex) == 16);

// The buffers: zero at the start; their exit functions free the blocks.
extern std::vector<TLVertex> g_vertices;
extern std::vector<std::uint16_t> g_clipIndices;
extern std::vector<ProjectedVertex> g_projected;
extern std::vector<std::uint8_t> g_cornerOutcodes;
extern std::vector<std::uint8_t> g_outcodes;

inline std::vector<TLVertex>& Vertices() { return g_vertices; }
inline std::vector<std::uint16_t>& ClipIndices() { return g_clipIndices; }
inline std::vector<ProjectedVertex>& Projected() { return g_projected; }
// An outcode per corner (the clipper's input) and per projected vertex.
inline std::vector<std::uint8_t>& CornerOutcodes() { return g_cornerOutcodes; }
inline std::vector<std::uint8_t>& Outcodes() { return g_outcodes; }

} // namespace devicebuffers
