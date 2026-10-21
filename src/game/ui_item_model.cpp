// An item's model for the item screens' model view (005EBFB0; ui_item_view.cpp caches it). See
// docs/notes/ui.md, section 9.
#include "engine_object.h"
#include "figure_manager.h"
#include "figure_mesh.h"
#include "ui.h"

#include "ei/hook.h"
#include "originals_ui.h"

namespace {
// The stand-in for a model the manager does not have: a 100 x 100 sprite (layout 0..100 x 100..0
// at depth 10, scale 70) over texels 0..128.
constexpr float kDepth = 10.0f;
constexpr float kScale = 70.0f;
constexpr float kSize = 100.0f;
constexpr int kTexels = 0x80;
} // namespace

// 005EBFB0 (__fastcall). The model manager's object `name`, else a new interface sprite standing in
// for it; one reference taken for the caller. A failed sprite is not checked (as in the original).
CEngineObject* __fastcall ItemModel(const char* name) {
    void* manager = UiModelManager();
    CEngineObject* model = static_cast<CFigureManager*>(manager)->NewFigure(name);
    if (!model) {
        model = static_cast<CFigureManager*>(manager)->NewFigure(nullptr);
        reinterpret_cast<CUiMesh*>(model)->SetRectangle(kDepth, kScale, 0.0f, kSize, kSize, 0.0f, 0, 0, kTexels, kTexels);
        static_cast<CFigureMesh*>(model)->FitVertexBounds();
    }
    ++model->refCount_;
    return model;
}
EI_REWRITTEN(0x005EBFB0, ItemModel);
