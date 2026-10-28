// The Direct3D device's mesh work buffers (device_buffers.h) and the exit functions that free them. Each array
// starts zeroed, which the definition's `{}` does, and its exit function is registered with the game's atexit in
// list order (docs/notes/standalone.md, "The static initialisers' objects"). The sixth array of the game's list is
// never used: config/unread_data.csv.
#include "device_buffers.h"

#include <vector>

namespace devicebuffers {

constinit std::vector<std::uint16_t> g_clipIndices = /*007AEDE0*/ {};
constinit std::vector<TLVertex> g_vertices = /*007AEE00*/ {};
constinit std::vector<ProjectedVertex> g_projected = /*007AEE10*/ {};
constinit std::vector<std::uint8_t> g_cornerOutcodes = /*007AEE20*/ {};
constinit std::vector<std::uint8_t> g_outcodes = /*007AEE30*/ {};

// The exit functions: each frees its array's block (the fields stay).
void FreeOutcodes() { lib::FreeStorage(g_outcodes); }
void FreeCornerOutcodes() { lib::FreeStorage(g_cornerOutcodes); }
void FreeProjected() { lib::FreeStorage(g_projected); }
void FreeVertices() { lib::FreeStorage(g_vertices); }
void FreeClipIndices() { lib::FreeStorage(g_clipIndices); }

} // namespace devicebuffers
