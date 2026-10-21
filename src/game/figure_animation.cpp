// 005ACCF0 (thiscall on the figure manager, RET 0xC): sets a figure's animation by name. Without
// an animation name the figure's animation is cleared (0050D950, whose EAX is returned). Otherwise
// the model is looked up (005AA5F0) and its animation part asked for the name (005A9EF0); each
// failure logs "Can't open model" / "Can't open animation ... for model" (0044C470) and returns
// 0; a found animation goes to the figure (0050DA10, whose EAX is returned). See
// docs/notes/models.md.
#include "ei/hook.h"
#include "lib/crt.h"

#include <cstdint>
#include "figure_manager.h"
#include "map_object_figures.h"
#include "originals_library.h"
#include "originals_engine.h"
#include "originals_units.h"

namespace {

// 005AA5F0: the manager's model of a name; 005A9EF0 (FigureModel::FindAnimation): the model's animation of a name.
// The figure: set an animation (0050DA10, CFigureAnimationState::LoadBoneTracks; the name is the
// bones' animation).
// 0044C470: the game's debug message (cdecl, printf-style).

} // namespace

int __fastcall SetFigureAnimation(void* manager, void*, void* figure, const char* modelName, const char* animation) {
    if (!animation) return static_cast<CFigureAnimationState*>(figure)->ClearBoneTracks();
    stlp::string key(modelName);
    void* model = static_cast<CFigureManager*>(manager)->FindModel(&key);
    if (!model) {
        game::DebugLog("Can't open model \"%s\"\n", modelName);
        return 0;
    }
    stlp::string name(animation);
    void* found = static_cast<FigureModel*>(model)->FindAnimation(&name);
    if (!found) {
        game::DebugLog("Can't open animation \"%s\" for model \"%s\"\n", animation, modelName);
        return 0;
    }
    return static_cast<CFigureAnimationState*>(figure)->LoadBoneTracks(static_cast<CFigureModelPart*>(found),
                                                                       reinterpret_cast<const FigureAnimation*>(animation));
}

EI_REWRITTEN(0x005ACCF0, SetFigureAnimation);
