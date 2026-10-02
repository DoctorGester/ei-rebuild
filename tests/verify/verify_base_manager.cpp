// Differential check for CBaseManager::FindObject (pure lookup).
#include "verify.h"

#include "../../src/game/base_manager.h"

// 00403F40 starts with PUSH EBX; MOV EBX,[ECX+0x24]; PUSH ESI (5 bytes).
static CEngineObject*(__fastcall* s_findObject)(const CBaseManager*, void*, unsigned);

static CEngineObject* __fastcall FindObjectVerify(const CBaseManager* self, void*, unsigned id) {
    CEngineObject* expected = s_findObject(self, nullptr, id);
    ei::verify::Check("CBaseManager::FindObject", expected, self->FindObject(id), "id %u", id);
    return expected;
}
EI_VERIFY(0x00403F40, FindObjectVerify, 5, s_findObject);
