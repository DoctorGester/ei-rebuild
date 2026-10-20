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
// The figure: set an animation (RET 8: animation, name).
__declspec(naked) int __fastcall SetAnimation(void*, void*, void*, const char*) EI_ORIGINAL(0x0050DA10)
// 0044C470: the game's debug message (cdecl, printf-style).

} // namespace

int __fastcall SetFigureAnimation(void* manager, void*, void* figure, const char* modelName, const char* animation) {
    if (!animation) return static_cast<CFigureAnimationState*>(figure)->ClearBoneTracks();
    stlp::string key(modelName);
    void* model = static_cast<CFigureManager*>(manager)->FindModel(&key);
    if (!model) {
        game::DebugLog(reinterpret_cast<const char*>(0x00790DC0), modelName);
        return 0;
    }
    stlp::string name(animation);
    void* found = static_cast<FigureModel*>(model)->FindAnimation(&name);
    if (!found) {
        game::DebugLog(reinterpret_cast<const char*>(0x00790D94), animation, modelName);
        return 0;
    }
    return SetAnimation(figure, nullptr, found, animation);
}

EI_REPLACE(0x005ACCF0, SetFigureAnimation);
