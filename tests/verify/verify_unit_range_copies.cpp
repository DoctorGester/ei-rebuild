// A-U11: wrappers for byte-identical copies in the units range whose first registrations compare
// through their own trampolines (so they cannot be shared): ours only.
// - 0052D030 (ScriptRefDestroy's copy): the slot cleared; returns the slot.
// - 00545B80 (CountedPointerRelease's copy, app_registry.cpp): the pointer left as it was.
#include "verify.h"

#include "../../src/game/engine_object.h"
#include "../../src/game/script.h"

namespace app {
void __fastcall CountedPointerRelease(void** self);
}

namespace {
void* s_unused;
}

static void* __fastcall RefDestroyCopyVerify(CEngineObject** slot, void*, int flags) {
    if (flags & 1) return ScriptRefDestroy(slot, nullptr, flags);   // freed: nothing to look at
    void* result = ScriptRefDestroy(slot, nullptr, flags);
    ei::verify::Check("ScriptRefDestroy@0052D030", true, result == slot && *slot == nullptr, "");
    return result;
}
EI_VERIFY(0x0052D030, RefDestroyCopyVerify, 0, s_unused);

static void __fastcall CountedPointerReleaseVerify(void** self) {
    void* before = *self;
    app::CountedPointerRelease(self);
    ei::verify::Check("CountedPointerRelease@00545B80", before, *self, "%p", before);
}
EI_VERIFY(0x00545B80, CountedPointerReleaseVerify, 0, s_unused);
