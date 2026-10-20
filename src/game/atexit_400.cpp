// The exit-time destructors of 00400000-00440000's globals (the compiler's dynamic atexit
// functions, registered by the static initialisers listed at 00783000; none is a Ghidra function).
// They run at exit, after the hooks are installed. Each calls the global's destructor (rewritten or
// not) through its original address. See docs/notes/core.md.
#include "ei/hook.h"
#include "game_data.h"
#include "game_lists.h"
#include "hash_tables_400.h"
#include "originals_library.h"
#include "lib/mfc_coll.h"

#include <cstdint>

namespace atexit400 {

namespace {
__declspec(naked) void __fastcall CameraDestruct(void* /*camera*/) EI_ORIGINAL(0x0040CE60)

// The once-only flags of the two buffers (bit 0: 0079B900, bit 1: 0079B8E0).
std::uint8_t& BufferFlags() { return *reinterpret_cast<std::uint8_t*>(0x0079B8FC); }
} // namespace

// 0040C8A0: the camera at 0079B2A8.
void __cdecl DestroyGlobalCamera() { CameraDestruct(&ei::GameData<char>(0x0079B2A8)); }
// 00408D50: the chunk archive's class registry at 0079B270.
void __cdecl DestroyClassRegistry() { hash400::ClassRegistryDestruct(&ei::GameData<ChunkArchive::ClassRegistry>(0x0079B270)); }
// 00412250 / 004122A0: the counted pointers to the game server (0079B5DC) and client (0079B5D8).
void __cdecl ReleaseGlobalServer() { ::RefPtrRelease(&ei::GameData<RefPtr>(0x0079B5DC)); }
void __cdecl ReleaseGlobalClient() { ::RefPtrRelease(&ei::GameData<RefPtr>(0x0079B5D8)); }
// 0042D900: the CWordArray at 0079B668.
void __cdecl DestroyGlobalWordArray() { ei::GameData<mfc::impl::CWordArray>(0x0079B668).Destruct(); }
// 00432CC0 / 00432D00: the growing buffers at 0079B900 and 0079B8E0, each destroyed once.
void __cdecl DestroyGlobalBuffer1() {
    std::uint8_t flags = BufferFlags();
    if (flags & 1) return;
    BufferFlags() = static_cast<std::uint8_t>(flags | 1);
    GrowingBufferDestruct(&ei::GameData<char>(0x0079B900));
}
void __cdecl DestroyGlobalBuffer2() {
    std::uint8_t flags = BufferFlags();
    if (flags & 2) return;
    BufferFlags() = static_cast<std::uint8_t>(flags | 2);
    GrowingBufferDestruct(&ei::GameData<char>(0x0079B8E0));
}
// 00434980: the vector at 0079B8D0.
void __cdecl DestroyGlobalVector() { ei::GameData<stlp::vector<stlp::Opaque<8>>>(0x0079B8D0).FreeStorage(); }   // 004387D0

} // namespace atexit400

EI_REPLACE(0x0040C8A0, atexit400::DestroyGlobalCamera);
EI_REPLACE(0x00408D50, atexit400::DestroyClassRegistry);
EI_REPLACE(0x00412250, atexit400::ReleaseGlobalServer);
EI_REPLACE(0x004122A0, atexit400::ReleaseGlobalClient);
EI_REPLACE(0x0042D900, atexit400::DestroyGlobalWordArray);
EI_REPLACE(0x00432CC0, atexit400::DestroyGlobalBuffer1);
EI_REPLACE(0x00432D00, atexit400::DestroyGlobalBuffer2);
EI_REPLACE(0x00434980, atexit400::DestroyGlobalVector);
