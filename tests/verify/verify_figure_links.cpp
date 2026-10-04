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
