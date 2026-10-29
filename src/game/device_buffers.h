#pragma once

// The Direct3D device's mesh work buffers (vectors), shared by its mesh draws and its flat-coloured draw.
// See docs/notes/models.md ("Mesh draws").

#include "lib/vecmath.h"

#include <cstdint>
#include <vector>

namespace devicebuffers {

// The projected positions (CTransformPipeline::Project / ProjectClipped).
extern std::vector<Vector4> g_projected;
// An outcode per projected vertex.
extern std::vector<std::uint8_t> g_outcodes;

} // namespace devicebuffers
