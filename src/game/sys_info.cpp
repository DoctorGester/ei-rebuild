// CSysInfo's slots and destructor (004DDD60-004DE8B0; A-S11). The system information object is a
// global (007AB8F8; an engine object, vtable 0073D644, class id 0xD) holding five texts about the
// machine. A static initialiser (table entry 00783320: 004DDD30) constructs it and registers its
// destructor with atexit; the constructor (004DDD6F) calls Create, which fills the texts
// (004DDF28, the registry and version queries). All of that runs before the hooks install, so the
// constructor, Create's body and its helpers (004DDF28-004DE789) are not rewritten; what is here
// can run later: the destructor (at exit) and the virtual slots. The original is compiled without
// optimisation (every function keeps a frame and spills ECX). See docs/notes/script.md (A-S11).
#include "engine_object.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "lib/mfc.h"

#include <cstddef>
#include <cstdint>

#pragma warning(disable : 4740) // naked forwarders

struct CSysInfo : CEngineObject {
    static constexpr int kClassId = 0xD;
    mfc::CString texts[5];   // +0x1C empty until Create
};
static_assert(offsetof(CSysInfo, texts) == 0x1C && sizeof(CSysInfo) == 0x30);

namespace {

// 004DDF28: the texts filled from the system (thiscall, plain RET).
__declspec(naked) void __fastcall SysInfoCollectOriginal(CSysInfo*) EI_ORIGINAL(0x004DDF28)

CSysInfo* SysInfo() { return reinterpret_cast<CSysInfo*>(0x007AB8F8); }
void SetSysInfoVtable(CSysInfo* self) { ei::SetVtable(self, 0x0073D644); }
const char* SysInfoClassNameText() { return reinterpret_cast<const char*>(0x0078DCD8); }   // "CSysInfo"

} // namespace

// 004DDF05 (slot 2, Create): the texts collected; returns 1. The constructor calls it directly.
int __fastcall SysInfoCreate(CSysInfo* self) {
    SysInfoCollectOriginal(self);
    return 1;
}
EI_REPLACE(0x004DDF05, SysInfoCreate);

// 004DDF1D (slot 3, Destroy): nothing.
void __fastcall SysInfoDestroy(CSysInfo*) {}
EI_REPLACE(0x004DDF1D, SysInfoDestroy);

// 004DE890 / 004DE8A0 (slots 11 / 12): the class name and id.
const char* __fastcall SysInfoClassName(CSysInfo*) { return SysInfoClassNameText(); }
EI_REPLACE(0x004DE890, SysInfoClassName);
int __fastcall SysInfoClassId(CSysInfo*) { return CSysInfo::kClassId; }
EI_REPLACE(0x004DE8A0, SysInfoClassId);

// 004DDE75: the destructor body: its vtable, the five texts from the last to the first, the engine
// object's base destructor.
void __fastcall SysInfoDestruct(CSysInfo* self) {
    SetSysInfoVtable(self);
    for (int i = 4; i >= 0; --i) self->texts[i].~CString();
    self->DestructBase();
}
EI_REPLACE(0x004DDE75, SysInfoDestruct);

// 004DE8B0 (slot 0, RET 4): the destructor, then operator delete when bit 0 of `flags` is set.
CSysInfo* __fastcall SysInfoDelete(CSysInfo* self, void*, unsigned flags) {
    SysInfoDestruct(self);
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}
EI_REPLACE(0x004DE8B0, SysInfoDelete);

// 004DDD60 (cdecl; registered with atexit by the static initialiser): the global's destructor.
void __cdecl SysInfoAtExit() { SysInfoDestruct(SysInfo()); }
EI_REPLACE(0x004DDD60, SysInfoAtExit);
