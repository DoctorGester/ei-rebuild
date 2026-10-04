// Checks for the Bink movie's frame step (bink_video.cpp).
#include "verify.h"

unsigned __fastcall BinkVideoFrame(void* self, void*, char toPrimary);

// 005DBD80 decodes and blits: ours only. Checked: the result is 0 or 1 and the video's handle and
// surface are unchanged.
static void* s_binkFrame;
static unsigned __fastcall BinkVideoFrameVerify(void* self, void*, char toPrimary) {
    void* holder = *static_cast<void**>(self);
    void* bink = static_cast<void**>(self)[6];
    unsigned result = BinkVideoFrame(self, nullptr, toPrimary);
    bool ok = (result & 0xFF) <= 1 && *static_cast<void**>(self) == holder && static_cast<void**>(self)[6] == bink;
    ei::verify::Check("CBinkVideo::Frame", true, ok, "result %08X", result);
    return result;
}
EI_VERIFY(0x005DBD80, BinkVideoFrameVerify, 0, s_binkFrame);
