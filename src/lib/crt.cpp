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
__declspec(naked) char* __cdecl strncpy(char*, const char*, std::size_t) EI_ORIGINAL(0x006E7E80)
__declspec(naked) int __cdecl isalpha(int) EI_ORIGINAL(0x006E92FB)
__declspec(naked) int __cdecl sprintf(char*, const char*, ...) EI_ORIGINAL(0x006E8715)
__declspec(naked) int __cdecl mbsicmp(const unsigned char*, const unsigned char*) EI_ORIGINAL(0x006E8767)
__declspec(naked) int __cdecl mbscmp(const unsigned char*, const unsigned char*) EI_ORIGINAL(0x006E848B)

__declspec(naked) int __cdecl open(const char*, int, ...) EI_ORIGINAL(0x006EB35C)
__declspec(naked) long __cdecl lseek(int, long, int) EI_ORIGINAL(0x006EB284)
__declspec(naked) int __cdecl read(int, void*, unsigned) EI_ORIGINAL(0x006EB046)
__declspec(naked) int __cdecl write(int, const void*, unsigned) EI_ORIGINAL(0x006EB642)
__declspec(naked) int __cdecl close(int) EI_ORIGINAL(0x006EAF66)

} // namespace gamecrt

namespace game {

__declspec(naked) int __cdecl FatalError(const char*, ...) EI_ORIGINAL(0x00480080)
__declspec(naked) void __cdecl DebugLog(const char*, ...) EI_ORIGINAL(0x0044C470)

} // namespace game
