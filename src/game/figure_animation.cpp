// 005ACCF0 (thiscall on the figure manager, RET 0xC): sets a figure's animation by name. Without
// an animation name the figure's animation is cleared (0050D950, whose EAX is returned). Otherwise
// the model is looked up and its animation part asked for the name; each
// failure logs "Can't open model" / "Can't open animation ... for model" and returns
// 0; a found animation goes to the figure (0050DA10, whose EAX is returned). See
// docs/notes/models.md.
#include "ei/hook.h"
#include "lib/crt.h"

#include <cstdint>
#include "figure_manager.h"
#include "map_object_figures.h"

namespace {

// The manager's model of a name; 005A9EF0 (FigureModel::FindAnimation): the model's animation of a name.
// The figure: set an animation (0050DA10, CFigureAnimationState::LoadBoneTracks; the name is the
// bones' animation).
// The game's debug message (cdecl, printf-style).

} // namespace

int CFigureManager::SetFigureAnimation(CFigureAnimationState* figure, const char* modelName,
                                       const FigureAnimation* animation) {
    if (!animation) return figure->ClearBoneTracks();
    stlp::string key(modelName);
    FigureModel* model = FindModel(&key);
    if (!model) {
        game::DebugLog("Can't open model \"%s\"\n", modelName);
        return 0;
    }
    stlp::string name(animation->name);
    void* found = model->FindAnimation(&name);
    if (!found) {
        game::DebugLog("Can't open animation \"%s\" for model \"%s\"\n", animation->name, modelName);
        return 0;
    }
    return figure->LoadBoneTracks(static_cast<CFigureModelPart*>(found), animation);
}

EI_REWRITTEN(0x005ACCF0, CFigureManager::SetFigureAnimation);
