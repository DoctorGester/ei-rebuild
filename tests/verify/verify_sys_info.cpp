// Checks for sys_info.cpp (A-S11). Main thread.
//  - Side by side (pure): the class name and id slots 004DE890 / 004DE8A0. Nothing in the game
//    asks the system information object for them: no scenario reaches them.
//  - Ours only: Create 004DDF05 (the constructor calls it from a static initialiser, before the
//    hooks: never reached), Destroy 004DDF1D (empty; no caller), the destructor 004DDE75 through
//    the exit handler 004DDD60 (every exit of the game), the deleting destructor 004DE8B0 (the
//    object is a global: no caller).
#include "verify.h"

#include "../../src/game/sys_info.h"

// sys_info.cpp (no header declares the shims).
int __fastcall SysInfoCreate(CSysInfo* self);
void __fastcall SysInfoDestroy(CSysInfo* self);
const char* __fastcall SysInfoClassName(CSysInfo* self);
int __fastcall SysInfoClassId(CSysInfo* self);
void __fastcall SysInfoDestruct(CSysInfo* self);
void __cdecl SysInfoAtExit();

namespace {
void* s_unused;

int __fastcall CreateVerify(CSysInfo* self) {
    int result = SysInfoCreate(self);
    ei::verify::Check("CSysInfo::Create (004DDF05)", 1, result, "");
    return result;
}

void __fastcall DestroyVerify(CSysInfo* self) {
    SysInfoDestroy(self);
    ei::verify::Check("CSysInfo::Destroy (004DDF1D)", true, true, "");
}

// 004DE890: PUSH EBP (1), MOV EBP,ESP (2), PUSH ECX (1), MOV [EBP-4],ECX (3).
const char*(__fastcall* s_className)(CSysInfo*);
const char* __fastcall ClassNameVerify(CSysInfo* self) {
    const char* expected = s_className(self);
    const char* result = SysInfoClassName(self);
    ei::verify::Check("CSysInfo class name (004DE890)", static_cast<const void*>(expected),
                      static_cast<const void*>(result), "%s", result);
    return result;
}

// 004DE8A0: the same prologue.
int(__fastcall* s_classId)(CSysInfo*);
int __fastcall ClassIdVerify(CSysInfo* self) {
    int expected = s_classId(self);
    int result = SysInfoClassId(self);
    ei::verify::Check("CSysInfo class id (004DE8A0)", expected, result, "");
    return result;
}

void __fastcall DestructVerify(CSysInfo* self) {
    SysInfoDestruct(self);
    ei::verify::Check("CSysInfo destructor (004DDE75)", true, true, "");
}

CSysInfo* __fastcall DeleteVerify(CSysInfo* self, void*, unsigned flags) {
    CSysInfo* result = self->DeleteScalar(flags);
    ei::verify::Check("CSysInfo deleting destructor (004DE8B0)", static_cast<void*>(self), static_cast<void*>(result),
                      "flags %u", flags);
    return result;
}

void __cdecl AtExitVerify() {
    SysInfoAtExit();
    ei::verify::Check("CSysInfo exit handler (004DDD60)", true, true, "");
}

} // namespace

EI_VERIFY(0x004DDF05, CreateVerify, 0, s_unused);
EI_VERIFY(0x004DDF1D, DestroyVerify, 0, s_unused);
EI_VERIFY(0x004DE890, ClassNameVerify, 7, s_className);
EI_VERIFY(0x004DE8A0, ClassIdVerify, 7, s_classId);
EI_VERIFY(0x004DDE75, DestructVerify, 0, s_unused);
EI_VERIFY(0x004DE8B0, DeleteVerify, 0, s_unused);
EI_VERIFY(0x004DDD60, AtExitVerify, 0, s_unused);
