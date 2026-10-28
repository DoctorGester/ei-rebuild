// An item's model for the item screens' model view (ui_item_view.cpp caches it). See
// docs/notes/ui.md, section 9.
#include "engine_object.h"
#include "figure_manager.h"
#include "figure_mesh.h"
#include "ui.h"

#include "game_singletons.h"

namespace {
// The stand-in for a model the manager does not have: a 100 x 100 sprite (layout 0..100 x 100..0
// at depth 10, scale 70) over texels 0..128.
constexpr float kDepth = 10.0f;
constexpr float kScale = 70.0f;
constexpr float kSize = 100.0f;
constexpr int kTexels = 0x80;
} // namespace

// The model manager's object `name`, else a new interface sprite standing in for it; one reference
// taken for the caller. A failed sprite is not checked (neither is it in the original).
CUiMesh* ItemModel(std::string_view name) {
    CFigureManager* manager = TheFigureManager();
    // The interface's model manager makes class 0x43 objects: interface sprites.
    CUiMesh* model = AsUiMesh(manager->NewFigure(name));
    if (!model) {
        model = AsUiMesh(manager->NewBlankFigure());
        model->SetRectangle(kDepth, kScale, 0.0f, kSize, kSize, 0.0f, 0, 0, kTexels, kTexels);
        model->FitVertexBounds();
    }
    ++model->refCount_;
    return model;
}
