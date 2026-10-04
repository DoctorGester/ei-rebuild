// Checks for the software cursor's frame draw (ui_cursor_frame.cpp).
#include "verify.h"

#include <windows.h>

#include <cstring>

class CCursor;
void __fastcall CursorDrawFrame(CCursor* self);

// 005E54A0 blits: ours only. Checked: when it composed the cursor (an image at +0x0C and a
// non-empty rectangle +0x30), the drawn rectangle +0x20 is the cursor's rectangle afterwards.
static void* s_cursorFrame;
static void __fastcall CursorDrawFrameVerify(CCursor* self) {
    auto* p = reinterpret_cast<unsigned char*>(self);
    CursorDrawFrame(self);
    bool software = (*reinterpret_cast<unsigned char*>(0x007AF064) & 1) != 0;   // the renderer's +4 bit 0
    bool composed = software && *reinterpret_cast<int*>(p + 0x0C) != 0 && *reinterpret_cast<unsigned char*>(p + 0x91) != 0 &&
                    !IsRectEmpty(reinterpret_cast<RECT*>(p + 0x30));
    bool ok = !composed || std::memcmp(p + 0x20, p + 0x30, sizeof(RECT)) == 0;
    ei::verify::Check("CursorDrawFrame", true, ok, "drawn rectangle not the cursor's");
}
EI_VERIFY(0x005E54A0, CursorDrawFrameVerify, 0, s_cursorFrame);
