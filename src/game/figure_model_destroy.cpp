// The figure model's destructor (FigureModel, model.h). Every cached animation is deleted, the map cleared,
// each archive closed (CloseResource) and deleted, then the map destroyed. See docs/notes/models.md.
#include "figure_manager.h"   // CFigureManager, FigureModel


#include <cstdint>


FigureModel::~FigureModel() {
    std::unordered_map<std::string, MappedResource*>& table = animationCache;
    for (auto& [name, animation] : table)
        if (animation) delete animation;
    table.clear();

    static constexpr MappedResource* FigureModel::*kArchives[3] = {&FigureModel::mesh, &FigureModel::bones,
                                                                    &FigureModel::animations};
    for (MappedResource* FigureModel::*archive : kArchives) (this->*archive)->CloseResource();
    for (MappedResource* FigureModel::*archive : kArchives) delete (this->*archive);
}

// The figure manager's destructor: every cached model destroyed and freed, the base's clean-up, the model map
// cleared and its buckets freed (the map's inline destructor), then the base destructor.
CFigureManager::~CFigureManager() {
    for (auto& [name, model] : models) {
        delete model;
    }
    ResourceManager::Destroy();   // the base's clean-up
}
