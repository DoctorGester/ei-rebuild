// The Direct3D device's mesh work buffers (device_buffers.h) and the exit functions that free them. Each array
// starts zeroed, which the definition's `{}` does, and its exit function is registered with the game's atexit in
// list order (docs/notes/standalone.md, "The static initialisers' objects"). The sixth array of the game's list is
// never used: config/unread_data.csv.
#include "device_buffers.h"

namespace devicebuffers {

constinit lib::AlignedArray<std::uint16_t> g_clipIndices = /*007AEDE0*/ {};
constinit lib::AlignedArray<TLVertex> g_vertices = /*007AEE00*/ {};
constinit lib::AlignedArray<ProjectedVertex> g_projected = /*007AEE10*/ {};
constinit lib::AlignedArray<std::uint8_t> g_cornerOutcodes = /*007AEE20*/ {};
constinit lib::AlignedArray<std::uint8_t> g_outcodes = /*007AEE30*/ {};

// The exit functions: each frees its array's block (the fields stay).
void __cdecl FreeOutcodes() { g_outcodes.FreeData(); }
void __cdecl FreeCornerOutcodes() { g_cornerOutcodes.FreeData(); }
void __cdecl FreeProjected() { g_projected.FreeData(); }
void __cdecl FreeVertices() { g_vertices.FreeData(); }
void __cdecl FreeClipIndices() { g_clipIndices.FreeData(); }

} // namespace devicebuffers
