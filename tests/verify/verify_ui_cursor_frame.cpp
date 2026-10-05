// Checks for the software cursor's frame draw and its draws to the primary (ui_cursor_frame.cpp).
#include "verify.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

class CCursor;
void __fastcall CursorDrawFrame(CCursor* self);
long __fastcall HolderBlt(std::uint8_t* self, void*, RECT* to, std::uint8_t* source, RECT* from, unsigned long flags, void* fx);
void __fastcall CursorThread(CCursor* self);
void __fastcall CursorSaveBackground(CCursor* self, void*, char fromPrimary);

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

// 005E5640, a Blt between two holders: the original (8 stolen bytes: PUSH ESI; MOV ESI,ECX;
// PUSH 007AEE78), then ours (the same copy again); results compared. With the software cursor
// (the frame draw and the cursor thread).
using HolderBltFn = long(__fastcall*)(std::uint8_t*, void*, RECT*, std::uint8_t*, RECT*, unsigned long, void*);
static HolderBltFn s_holderBlt;
static long __fastcall HolderBltVerify(std::uint8_t* self, void*, RECT* to, std::uint8_t* source, RECT* from, unsigned long flags,
                                       void* fx) {
    long expected = s_holderBlt(self, nullptr, to, source, from, flags, fx);
    long result = HolderBlt(self, nullptr, to, source, from, flags, fx);
    ei::verify::Check("HolderBlt", expected, result, "result %08lX, original %08lX", static_cast<unsigned long>(result),
                      static_cast<unsigned long>(expected));
    return result;
}
EI_VERIFY(0x005E5640, HolderBltVerify, 8, s_holderBlt);

// 005E5690 is the cursor thread's loop: ours only. Checked when it returns: it was asked to stop
// (+0x9C) or the cursor is off (+0x91 0). Started in every scenario (it draws only with the
// software cursor bit).
static void* s_cursorThread;
static void __fastcall CursorThreadVerify(CCursor* self) {
    CursorThread(self);
    auto* p = reinterpret_cast<unsigned char*>(self);
    bool ok = p[0x91] == 0 || *reinterpret_cast<int*>(p + 0x9C) != 0;
    ei::verify::Check("CursorThread", true, ok, "returned without a stop request");
}
EI_VERIFY(0x005E5690, CursorThreadVerify, 0, s_cursorThread);

// 005E6630 copies the screen under the cursor into its background surface: ours only. Checked: the
// drawn rectangle (+0x20) and the bounds (+0x50) are unchanged. Every scenario that loads a zone
// (the loading movie calls it).
static void* s_saveBackground;
static void __fastcall CursorSaveBackgroundVerify(CCursor* self, void*, char fromPrimary) {
    auto* p = reinterpret_cast<unsigned char*>(self);
    RECT drawn, bounds;
    std::memcpy(&drawn, p + 0x20, sizeof(RECT));
    std::memcpy(&bounds, p + 0x50, sizeof(RECT));
    CursorSaveBackground(self, nullptr, fromPrimary);
    bool ok = std::memcmp(&drawn, p + 0x20, sizeof(RECT)) == 0 && std::memcmp(&bounds, p + 0x50, sizeof(RECT)) == 0;
    ei::verify::Check("CursorSaveBackground", true, ok, "rectangles changed");
}
EI_VERIFY(0x005E6630, CursorSaveBackgroundVerify, 0, s_saveBackground);
