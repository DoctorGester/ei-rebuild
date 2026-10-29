// Small leftovers: the class names and ids of the animation
// classes, the scene renderer's frame time and a vector copy.
#include "map_object_animation.h"
#include "map_object_figures.h"
#include "model.h"

#include "ei/hook.h"

namespace {
constexpr const char* kTrackClassName = "CAnimationTrack";
constexpr const char* kBoneClassName = "CAnimationNode";
constexpr const char* kStateClassName = "CAnimationSystem2";

} // namespace

const char* AnimationTrack::ClassName() { return kTrackClassName; }
ClassId AnimationTrack::GetClassId() { return ClassId::AnimationTrack; }

const char* FigureBone::GetName() { return name.c_str(); }
const char* FigureBone::ClassName() { return kBoneClassName; }
ClassId FigureBone::GetClassId() { return ClassId::FigureBone; }

const char* CFigureAnimationState::ClassName() { return kStateClassName; }
ClassId CFigureAnimationState::GetClassId() { return ClassId::FigureAnimationState; }

