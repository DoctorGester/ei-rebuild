// Differential check for CMappedResource::FindEntry (resource archive name lookup).
#include "verify.h"

#include "../../src/game/resource.h"

// 00674850 starts with PUSH ECX; PUSH EBX; PUSH EBP; MOV EBP,ECX; PUSH ESI (6 bytes).
static int(__fastcall* s_original)(const CMappedResource*, void*, const char*);

static int __fastcall FindEntryVerify(const CMappedResource* self, void*, const char* name) {
    int expected = s_original(self, nullptr, name);
    int actual = self->FindEntry(name);
    ei::verify::Check("CMappedResource::FindEntry", expected, actual, "name=\"%s\" -> %d vs %d", name,
                      expected, actual);
    return expected;
}
EI_VERIFY(0x00674850, FindEntryVerify, 6, s_original);
