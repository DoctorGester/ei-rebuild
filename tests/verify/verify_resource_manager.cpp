// Differential check for CResourceManager::HasFile (no side effects).
#include "verify.h"

#include "../../src/game/resource_manager.h"

// 006754E0 starts with PUSH EBX; MOV EBX,ECX; PUSH ESI; PUSH EDI (5 bytes).
static BOOL(__fastcall* s_hasFile)(CResourceManager*, void*, const char*);

static BOOL __fastcall HasFileVerify(CResourceManager* self, void*, const char* name) {
    BOOL expected = s_hasFile(self, nullptr, name);
    BOOL actual = self->CResourceManager::HasFile(name);
    ei::verify::Check("CResourceManager::HasFile", expected, actual, "\"%s\"", name);
    return expected;
}
EI_VERIFY(0x006754E0, HasFileVerify, 5, s_hasFile);
