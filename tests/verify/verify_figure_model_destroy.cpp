// Checks for the figure model's destructor (src/game/figure_model_destroy.cpp). Ours only (it
// frees): afterwards the map holds no elements.
#include "verify.h"

void __fastcall DestroyFigureModel(char* model);

namespace {
void* s_unused;
}

// 005A9C90.
static void __fastcall DestroyFigureModelVerify(char* model) {
    DestroyFigureModel(model);
    int count = *reinterpret_cast<int*>(model + 0x1C);
    ei::verify::Check("DestroyFigureModel", 0, count, "");
}
EI_VERIFY(0x005A9C90, DestroyFigureModelVerify, 0, s_unused);
