// Small leftovers: the class names and ids of the animation
// classes, the scene renderer's frame time and a vector copy.
#include "map_object_animation.h"
#include "map_object_figures.h"
#include "model.h"

#include "ei/hook.h"

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

namespace {
constexpr const char* kTrackClassName = "CAnimationTrack";
constexpr const char* kBoneClassName = "CAnimationNode";
constexpr const char* kStateClassName = "CAnimationSystem2";

} // namespace

const char* AnimationTrack::GetClassName() { return kTrackClassName; }
ClassId AnimationTrack::GetClassId() { return kClassAnimationTrack; }

const char* FigureBone::GetName() { return name.c_str(); }
const char* FigureBone::GetClassName() { return kBoneClassName; }
ClassId FigureBone::GetClassId() { return kClassFigureBone; }

const char* CFigureAnimationState::GetClassName() { return kStateClassName; }
ClassId CFigureAnimationState::GetClassId() { return kClassFigureAnimationState; }

