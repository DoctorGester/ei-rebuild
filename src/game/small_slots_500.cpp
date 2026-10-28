// Small leftovers: the class names and ids of the animation
// classes, the scene renderer's frame time and a vector copy.
#include "map_object_animation.h"
#include "game_strings.h"
#include "map_object_figures.h"
#include "model.h"

#include "ei/hook.h"

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

namespace {
constexpr const char* kTrackClassName = "CAnimationTrack";
constexpr const char* kBoneClassName = "CAnimationNode";
constexpr const char* kStateClassName = "CAnimationSystem2";
constexpr int kTrackClassId = 0x22;
constexpr int kBoneClassId = 0x21;
constexpr int kStateClassId = 0x21;   // the same id as the bones'

} // namespace

const char* AnimationTrack::GetClassName() { return kTrackClassName; }
int AnimationTrack::GetClassId() { return kTrackClassId; }

const char* FigureBone::GetName() { return name.c_str(); }
const char* FigureBone::GetClassName() { return kBoneClassName; }
int FigureBone::GetClassId() { return kBoneClassId; }

const char* CFigureAnimationState::GetClassName() { return kStateClassName; }
int CFigureAnimationState::GetClassId() { return kStateClassId; }

