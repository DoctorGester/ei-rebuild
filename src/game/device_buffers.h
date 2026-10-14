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

inline lib::AlignedArray<TLVertex>& Vertices() { return *reinterpret_cast<lib::AlignedArray<TLVertex>*>(0x007AEE00); }
inline lib::AlignedArray<std::uint16_t>& ClipIndices() { return *reinterpret_cast<lib::AlignedArray<std::uint16_t>*>(0x007AEDE0); }
inline lib::AlignedArray<ProjectedVertex>& Projected() { return *reinterpret_cast<lib::AlignedArray<ProjectedVertex>*>(0x007AEE10); }
// An outcode per corner (the clipper's input) and per projected vertex.
inline lib::AlignedArray<std::uint8_t>& CornerOutcodes() { return *reinterpret_cast<lib::AlignedArray<std::uint8_t>*>(0x007AEE20); }
inline lib::AlignedArray<std::uint8_t>& Outcodes() { return *reinterpret_cast<lib::AlignedArray<std::uint8_t>*>(0x007AEE30); }

} // namespace devicebuffers
