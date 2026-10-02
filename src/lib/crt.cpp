#include "crt.h"

#include "ei/hook.h"

namespace gamecrt {

__declspec(naked) void* __cdecl operator_new(std::size_t) EI_ORIGINAL(0x007005AF)
__declspec(naked) void __cdecl operator_delete(void*) EI_ORIGINAL(0x007005D8)

__declspec(naked) int __cdecl rand() EI_ORIGINAL(0x006E77F7)
__declspec(naked) int __cdecl tolower(int) EI_ORIGINAL(0x006EBA53)
__declspec(naked) int __cdecl toupper(int) EI_ORIGINAL(0x006E91C0)
__declspec(naked) int __cdecl strnicmp(const char*, const char*, std::size_t) EI_ORIGINAL(0x006FA1A0)
__declspec(naked) int __cdecl stricmp(const char*, const char*) EI_ORIGINAL(0x006F2230)

} // namespace gamecrt
