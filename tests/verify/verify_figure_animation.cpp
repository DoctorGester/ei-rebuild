// Checks for the figure animation setter (src/game/figure_animation.cpp). It loads and changes the
// figure, so only ours runs; logged.
#include "verify.h"

int __fastcall SetFigureAnimation(void* manager, void*, void* figure, const char* modelName, const char* animation);

namespace {
void* s_unused;
}

// 005ACCF0.
static int __fastcall SetFigureAnimationVerify(void* manager, void*, void* figure, const char* modelName,
                                               const char* animation) {
    int result = SetFigureAnimation(manager, nullptr, figure, modelName, animation);
    ei::verify::Check("SetFigureAnimation", true, true, "%s / %s -> %d", modelName ? modelName : "-",
                      animation ? animation : "-", result);
    return result;
}
EI_VERIFY(0x005ACCF0, SetFigureAnimationVerify, 0, s_unused);
