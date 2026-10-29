// The Direct3D device's mesh work buffers (device_buffers.h).
#include "device_buffers.h"

#include <vector>

namespace devicebuffers {

std::vector<std::uint16_t> g_clipIndices;
std::vector<TLVertex> g_vertices;
std::vector<ProjectedVertex> g_projected;
std::vector<std::uint8_t> g_cornerOutcodes;
std::vector<std::uint8_t> g_outcodes;

} // namespace devicebuffers
