#include "crt.h"

#include "ei/hook.h"
#include "game/originals_library.h"

namespace gamecrt {

__declspec(naked) void* __cdecl operator_new(std::size_t) EI_ORIGINAL(0x007005AF)
__declspec(naked) void __cdecl operator_delete(void*) EI_ORIGINAL(0x007005D8)
__declspec(naked) void* __cdecl malloc(std::size_t) EI_ORIGINAL(0x006EABD1)
__declspec(naked) void __cdecl free(void*) EI_ORIGINAL(0x006EA8AD)

__declspec(naked) int __cdecl rand() EI_ORIGINAL(0x006E77F7)
__declspec(naked) void __cdecl srand(unsigned) EI_ORIGINAL(0x006E77EA)
__declspec(naked) int __cdecl tolower(int) EI_ORIGINAL(0x006EBA53)
__declspec(naked) int __cdecl toupper(int) EI_ORIGINAL(0x006E91C0)
__declspec(naked) int __cdecl strnicmp(const char*, const char*, std::size_t) EI_ORIGINAL(0x006FA1A0)
__declspec(naked) int __cdecl stricmp(const char*, const char*) EI_ORIGINAL(0x006F2230)
__declspec(naked) char* __cdecl strncpy(char*, const char*, std::size_t) EI_ORIGINAL(0x006E7E80)
__declspec(naked) int __cdecl isalpha(int) EI_ORIGINAL(0x006E92FB)
__declspec(naked) int __cdecl isdigit(int) EI_ORIGINAL(0x006E9379)
__declspec(naked) int __cdecl isprint(int) EI_ORIGINAL(0x006E944C)
__declspec(naked) int __cdecl sprintf(char*, const char*, ...) EI_ORIGINAL(0x006E8715)
__declspec(naked) int __cdecl sscanf(const char*, const char*, ...) EI_ORIGINAL(0x006EAEEC)
__declspec(naked) char* __cdecl itoa(int, char*, int) EI_ORIGINAL(0x006E9B36)
__declspec(naked) int __cdecl mbsicmp(const unsigned char*, const unsigned char*) EI_ORIGINAL(0x006E8767)
__declspec(naked) int __cdecl access(const char*, int) EI_ORIGINAL(0x006EAF20)
int __cdecl mbscmp(const unsigned char* a, const unsigned char* b) {
    return GameMbscmp(reinterpret_cast<const char*>(a), reinterpret_cast<const char*>(b));
}

namespace {

struct CaseFold {
    bool built = false;
    unsigned localeHandle = 0;
    unsigned codePage = 0;
    int fold[256];
};

unsigned CtypeHandle() { return *reinterpret_cast<const unsigned*>(0x007CABF0); }   // __lc_handle[LC_CTYPE]
unsigned CodePage() { return *reinterpret_cast<const unsigned*>(0x007CAC00); }      // __lc_codepage

CaseFold caseFold;

} // namespace

const int* CaseFoldTable() {
    CaseFold& table = caseFold;
    const unsigned handle = CtypeHandle();
    const unsigned codePage = CodePage();
    if (!table.built || table.localeHandle != handle || table.codePage != codePage) {
        for (int c = 0; c < 256; ++c) table.fold[c] = tolower(c);
        table.localeHandle = handle;
        table.codePage = codePage;
        table.built = true;
    }
    return table.fold;
}

__declspec(naked) double __cdecl acos(double) EI_ORIGINAL(0x006E7334)
__declspec(naked) double __cdecl asin(double) EI_ORIGINAL(0x006E83D4)
__declspec(naked) double __cdecl fmod(double, double) EI_ORIGINAL(0x006E95A0)
__declspec(naked) unsigned __cdecl controlfp(unsigned, unsigned) EI_ORIGINAL(0x006EBBB4)

__declspec(naked) int __cdecl open(const char*, int, ...) EI_ORIGINAL(0x006EB35C)
__declspec(naked) long __cdecl lseek(int, long, int) EI_ORIGINAL(0x006EB284)
__declspec(naked) int __cdecl read(int, void*, unsigned) EI_ORIGINAL(0x006EB046)
__declspec(naked) int __cdecl write(int, const void*, unsigned) EI_ORIGINAL(0x006EB642)
__declspec(naked) int __cdecl close(int) EI_ORIGINAL(0x006EAF66)
__declspec(naked) long __cdecl filelength(int) EI_ORIGINAL(0x006E904C)

__declspec(naked) CrtFile* __cdecl fopen(const char*, const char*) EI_ORIGINAL(0x006E9107)
__declspec(naked) int __cdecl fclose(CrtFile*) EI_ORIGINAL(0x006E8EB8)
__declspec(naked) int __cdecl fseek(CrtFile*, long, int) EI_ORIGINAL(0x006E95D2)
__declspec(naked) long __cdecl ftell(CrtFile*) EI_ORIGINAL(0x006E97C4)
__declspec(naked) std::size_t __cdecl fread(void*, std::size_t, std::size_t, CrtFile*) EI_ORIGINAL(0x006E8F35)
__declspec(naked) std::size_t __cdecl fwrite(const void*, std::size_t, std::size_t, CrtFile*) EI_ORIGINAL(0x006E968B)

__declspec(naked) void __cdecl splitpath(const char*, char*, char*, char*, char*) EI_ORIGINAL(0x006E8BEE)
__declspec(naked) double __cdecl ceil(double) EI_ORIGINAL(0x006E8B1F)
__declspec(naked) NewHandler __cdecl set_new_handler(NewHandler) EI_ORIGINAL(0x006E8D35)

} // namespace gamecrt

namespace game {

__declspec(naked) int __cdecl FatalError(const char*, ...) EI_ORIGINAL(0x00480080)
__declspec(naked) unsigned __cdecl DebugLog(const char*, ...) EI_ORIGINAL(0x0044C470)

} // namespace game
