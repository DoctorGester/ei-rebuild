// Checks for the skeleton link loader (src/game/figure_links.cpp). It reads resources and links a
// figure, so only ours runs; logged.
#include "verify.h"

#include "../../src/game/figure_manager.h"
#include "../../src/game/map_object_figures.h"


namespace {
void* s_unused;
}

// 005AC880.
static int __fastcall LoadSkeletonLinksVerify(CFigureManager* manager, void*, CFigureAnimationState* state,
                                              const char* name) {
    int result = manager->LoadSkeletonLinks(state, name);
    ei::verify::Check("LoadSkeletonLinks", true, true, "%s -> %d", name, result);
    return result;
}
EI_VERIFY(0x005AC880, LoadSkeletonLinksVerify, 0, s_unused);


// 005ACB80: reads resources and changes the bone, so ours only; the result is 0 or 1.
static int __fastcall LoadFigureBonesVerify(CFigureManager* manager, void*, FigureModel* model, FigureBone* bone) {
    int result = manager->LoadFigureBones(model, bone);
    ei::verify::Check("LoadFigureBones", true, result == 0 || result == 1, "model %p -> %d", model, result);
    return result;
}
EI_VERIFY(0x005ACB80, LoadFigureBonesVerify, 0, s_unused);
