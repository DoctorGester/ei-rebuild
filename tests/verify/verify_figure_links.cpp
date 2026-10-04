// Checks for the skeleton link loader (src/game/figure_links.cpp). It reads resources and links a
// figure, so only ours runs; logged.
#include "verify.h"

int __fastcall LoadSkeletonLinks(void* manager, void*, void* figure, const char* name);

namespace {
void* s_unused;
}

// 005AC880.
static int __fastcall LoadSkeletonLinksVerify(void* manager, void*, void* figure, const char* name) {
    int result = LoadSkeletonLinks(manager, nullptr, figure, name);
    ei::verify::Check("LoadSkeletonLinks", true, true, "%s -> %d", name, result);
    return result;
}
EI_VERIFY(0x005AC880, LoadSkeletonLinksVerify, 0, s_unused);

int __fastcall LoadFigureBones(void* manager, void*, void** model, unsigned char* figure);

// 005ACB80: reads resources and changes the figure, so ours only; the result is 0 or 1.
static int __fastcall LoadFigureBonesVerify(void* manager, void*, void** model, unsigned char* figure) {
    int result = LoadFigureBones(manager, nullptr, model, figure);
    ei::verify::Check("LoadFigureBones", true, result == 0 || result == 1, "model %p -> %d", model, result);
    return result;
}
EI_VERIFY(0x005ACB80, LoadFigureBonesVerify, 0, s_unused);
