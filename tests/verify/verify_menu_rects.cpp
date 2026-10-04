// Checks for LoadMenuRects (src/game/menu_rects.cpp). It opens a resource file, so only ours runs;
// on success no rectangle is left all zero.
#include "verify.h"

#include <cstdint>

int __fastcall LoadMenuRects(void* owner, void*, std::uint8_t* rects);   // menu_rects.cpp

namespace {
void* s_unused;
}

// 005ACE80.
static int __fastcall LoadMenuRectsVerify(void* owner, void*, std::uint8_t* rects) {
    int result = LoadMenuRects(owner, nullptr, rects);
    bool ok = true;
    if (result == 1)
        for (int offset = 0; offset <= 0xA0; offset += 0x10) {
            auto* r = reinterpret_cast<const int*>(rects + offset);
            if (r[0] == 0 && r[1] == 0 && r[2] == 0 && r[3] == 0) ok = false;
        }
    ei::verify::Check("LoadMenuRects", true, ok, "-> %d", result);
    return result;
}
EI_VERIFY(0x005ACE80, LoadMenuRectsVerify, 0, s_unused);
