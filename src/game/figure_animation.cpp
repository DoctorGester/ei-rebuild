// Sets a figure's animation by name (figure manager). Without an animation name the figure's animation is
// cleared (and the clear's result returned). Otherwise the model is looked up and its animation part asked
// for the name; each failure logs "Can't open model" / "Can't open animation ... for model" and returns 0; a
// found animation goes to the figure (its result returned). See docs/notes/models.md.
#include "lib/crt.h"

#include <cstdint>
#include "figure_manager.h"
#include "map_object_figures.h"

namespace {

} // namespace

int CFigureManager::SetFigureAnimation(FigureAnimationState* figure, const char* modelName,
                                       const FigureAnimation* animation) {
    if (!animation) return figure->ClearBoneTracks();
    std::string key(modelName);
    FigureModel* model = FindModel(&key);
    if (!model) {
        game::DebugLog("Can't open model \"%s\"\n", modelName);
        return 0;
    }
    std::string name(animation->name);
    MappedResource* found = model->FindAnimation(&name);
    if (!found) {
        game::DebugLog("Can't open animation \"%s\" for model \"%s\"\n", animation->name, modelName);
        return 0;
    }
    return figure->LoadBoneTracks(found, animation);
}
