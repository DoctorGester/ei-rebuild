// Checks for the figure model's destructor (src/game/figure_model_destroy.cpp). Ours only (it
// frees): afterwards the map holds no elements.
#include "verify.h"

#include "../../src/game/figure_manager.h"

void __fastcall DestroyFigureModel(FigureModel* model);

namespace {
void* s_unused;
}

// 005A9C90.
static void __fastcall DestroyFigureModelVerify(FigureModel* model) {
    DestroyFigureModel(model);
    ei::verify::Check("DestroyFigureModel", 0, static_cast<int>(model->animationCache.Size()), "");
}
EI_VERIFY(0x005A9C90, DestroyFigureModelVerify, 0, s_unused);

void __fastcall DestroyFigureManager(char* manager);

// 005AA1C0: frees, so ours only; afterwards the model map holds no elements.
static void __fastcall DestroyFigureManagerVerify(char* manager) {
    DestroyFigureManager(manager);
    ei::verify::Check("DestroyFigureManager", 0, static_cast<int>(reinterpret_cast<CFigureManager*>(manager)->models_.Size()), "");
}
EI_VERIFY(0x005AA1C0, DestroyFigureManagerVerify, 0, s_unused);
