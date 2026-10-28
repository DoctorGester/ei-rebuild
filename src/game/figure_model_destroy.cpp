// The figure model's destructor (FigureModel, model.h). Every cached animation is deleted, the map cleared,
// each archive closed (CloseResource) and deleted, then the map destroyed. See docs/notes/models.md.
#include "figure_manager.h"   // CFigureManager, FigureModel


#include "lib/crt.h"
#include "lib/mfc_slots.h"

#include <cstdint>


void DestroyFigureModel(FigureModel* model) {
    FigureModel::AnimationMap& table = model->animationCache;
    for (auto& [name, animation] : table)
        if (animation) delete animation;
    table.clear();

    static constexpr CMappedResource* FigureModel::*kArchives[3] = {&FigureModel::mesh, &FigureModel::bones,
                                                                    &FigureModel::animations};
    for (CMappedResource* FigureModel::*archive : kArchives) (model->*archive)->CloseResource();
    for (CMappedResource* FigureModel::*archive : kArchives)
        if (model->*archive) delete (model->*archive);

    std::destroy_at(&table);
}

// The figure manager's destructor: every cached model destroyed and freed, the base's clean-up, the model map
// cleared and its buckets freed (the map's inline destructor), then the base destructor.
CFigureManager::~CFigureManager() {
    for (auto& [name, model] : models_) {
        if (model) {
            DestroyFigureModel(model);
            gamecrt::operator_delete(model);
        }
    }
    CResourceManager::Destroy();   // the base's clean-up
}
