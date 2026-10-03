// Checks for the briefing variables (src/game/world_briefing.cpp). The speaker and the variable
// only read the briefing texts (each lookup opens its section again), so the original runs first
// into a scratch string and the results are compared; MarkBriefing sets a variable, so only ours
// runs and the variable is read back.
#include "verify.h"

#include "../../src/game/world.h"

#include <cstring>

namespace {
void* s_unused;

using StringFn = mfc::CString*(__stdcall*)(mfc::CString*, const char*);
StringFn s_originalSpeaker;
StringFn s_originalVariable;

__declspec(naked) double __fastcall VariableGetAt(void*, void*, const char*) {
    __asm mov eax, 0x00460AF0
    __asm jmp eax
}

mfc::CString* Compare(const char* what, StringFn original, mfc::CString* (__stdcall* ours)(mfc::CString*, const char*),
                      mfc::CString* out, const char* briefing) {
    alignas(mfc::CString) std::uint8_t storage[sizeof(mfc::CString)];
    mfc::CString* expected = original(reinterpret_cast<mfc::CString*>(storage), briefing);
    mfc::CString* result = ours(out, briefing);
    bool ok = result == out && std::strcmp(*result, *expected) == 0;
    ei::verify::Check(what, true, ok, "%s: %s (original %s)", briefing ? briefing : "", static_cast<const char*>(*result),
                      static_cast<const char*>(*expected));
    expected->~CString();
    return result;
}
} // namespace

// 0057FC90.
static mfc::CString* __stdcall BriefingSpeakerVerify(mfc::CString* out, const char* briefing) {
    return Compare("BriefingSpeakerInto", s_originalSpeaker, BriefingSpeakerInto, out, briefing);
}
EI_VERIFY(0x0057FC90, BriefingSpeakerVerify, 7, s_originalSpeaker);

// 0057FDE0.
static mfc::CString* __stdcall BriefingVariableVerify(mfc::CString* out, const char* briefing) {
    return Compare("BriefingVariableInto", s_originalVariable, BriefingVariableInto, out, briefing);
}
EI_VERIFY(0x0057FDE0, BriefingVariableVerify, 7, s_originalVariable);

// 0057FF40: afterwards the briefing's variable is 1 for the local player.
static void __fastcall MarkBriefingVerify(GameFlow* self, void*, const char* briefing) {
    self->MarkBriefing(briefing);
    alignas(mfc::CString) std::uint8_t storage[sizeof(mfc::CString)];
    mfc::CString* variable = BriefingVariableInto(reinterpret_cast<mfc::CString*>(storage), briefing);
    auto* player = *reinterpret_cast<WorldPlayer**>(WorldClient()->LocalView() + 0x1C);
    double value = VariableGetAt(player->variables, nullptr, *variable);
    ei::verify::Check("GameFlow::MarkBriefing", true, value == 1.0, "%s = %g", static_cast<const char*>(*variable), value);
    variable->~CString();
}
EI_VERIFY(0x0057FF40, MarkBriefingVerify, 0, s_unused);
