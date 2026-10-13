// Check for the game's message box (app.cpp, 004560D0).
//
// Ours only: the box is modal and, in full screen, flips DirectDraw to the GDI surface, so it
// cannot run twice. What it must leave: MessageBoxA's answer (not 0) in EAX and the cursor that
// was set before the box. Only the fatal error reports call it (FatalError 00480080,
// FatalErrorCode 004800E0 and 00480160); no scenario reaches it.
#include "verify.h"

#include "../../src/game/app.h"
#include "../../src/lib/gameapi.h"

namespace {

void* s_unused;
int __fastcall ShowMessageBoxVerify(const char* text, int flags) {
    const HCURSOR before = gameapi::GetCursor();
    const int answer = app::ShowMessageBox(text, flags);
    const HCURSOR after = gameapi::GetCursor();
    ei::verify::Check("app::ShowMessageBox", true, answer != 0 && before == after, "answer %d, cursor %p -> %p", answer,
                      static_cast<void*>(before), static_cast<void*>(after));
    return answer;
}
EI_VERIFY(0x004560D0, ShowMessageBoxVerify, 0, s_unused);

} // namespace
