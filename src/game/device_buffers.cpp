// The Direct3D device's mesh work buffers (device_buffers.h) and the exit functions that free them. Each static
// initialiser (004E5C40, 004E5C90, 004E5CE0, 004E5D30, 004E5DD0) zeroes one array, which the definition's `{}` does
// here, and registers its exit function with the game's atexit in list order; the functions are ported on the names
// and registered at their addresses (docs/notes/standalone.md, "The static initialisers' objects"). The sixth array
// of the original's list (007AEDF0, 004E5D80) is never used: config/unread_data.csv.
#include "device_buffers.h"

#include "ei/hook.h"

namespace devicebuffers {

constinit lib::AlignedArray<std::uint16_t> g_clipIndices = /*007AEDE0*/ {};
constinit lib::AlignedArray<TLVertex> g_vertices = /*007AEE00*/ {};
constinit lib::AlignedArray<ProjectedVertex> g_projected = /*007AEE10*/ {};
constinit lib::AlignedArray<std::uint8_t> g_cornerOutcodes = /*007AEE20*/ {};
constinit lib::AlignedArray<std::uint8_t> g_outcodes = /*007AEE30*/ {};

// The exit functions: each frees its array's block (the fields stay).
void __cdecl FreeOutcodes() { g_outcodes.FreeData(); }               // 004E5C70
void __cdecl FreeCornerOutcodes() { g_cornerOutcodes.FreeData(); }   // 004E5CC0
void __cdecl FreeProjected() { g_projected.FreeData(); }             // 004E5D10
void __cdecl FreeVertices() { g_vertices.FreeData(); }               // 004E5D60
void __cdecl FreeClipIndices() { g_clipIndices.FreeData(); }         // 004E5E00

} // namespace devicebuffers

EI_REPLACE(0x004E5C70, devicebuffers::FreeOutcodes);
EI_REPLACE(0x004E5CC0, devicebuffers::FreeCornerOutcodes);
EI_REPLACE(0x004E5D10, devicebuffers::FreeProjected);
EI_REPLACE(0x004E5D60, devicebuffers::FreeVertices);
EI_REPLACE(0x004E5E00, devicebuffers::FreeClipIndices);
