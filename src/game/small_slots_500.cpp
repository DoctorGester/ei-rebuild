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

// The animation track's slots 11 / 12.
const char* __fastcall AnimationTrackClassName(AnimationTrack*) { return kTrackClassName; }
EI_REWRITTEN(0x0050A6E0, AnimationTrackClassName);
int __fastcall AnimationTrackClassId(AnimationTrack*) { return kTrackClassId; }
EI_REWRITTEN(0x0050A6F0, AnimationTrackClassId);
const char* AnimationTrack::GetClassName() { return kTrackClassName; }
int AnimationTrack::GetClassId() { return kTrackClassId; }

// A figure bone's slots 7 / 11 / 12: its name (the
// piece's), class name and id. The first is 4 bytes (too short to hook).
const char* __fastcall FigureBoneName(FigureBone* self) { return self->name.c_str(); }
EI_REPLACE(0x0050B180, FigureBoneName);
const char* __fastcall FigureBoneClassName(FigureBone*) { return kBoneClassName; }
EI_REWRITTEN(0x0050B190, FigureBoneClassName);
int __fastcall FigureBoneClassId(FigureBone*) { return kBoneClassId; }
EI_REWRITTEN(0x0050B1A0, FigureBoneClassId);
const char* FigureBone::GetName() { return name.c_str(); }
const char* FigureBone::GetClassName() { return kBoneClassName; }
int FigureBone::GetClassId() { return kBoneClassId; }

// The figure animation state's slots 11 / 12.
const char* __fastcall AnimationStateClassName(CFigureAnimationState*) { return kStateClassName; }
EI_REWRITTEN(0x0050C6F0, AnimationStateClassName);
int __fastcall AnimationStateClassId(CFigureAnimationState*) { return kStateClassId; }
EI_REWRITTEN(0x0050C700, AnimationStateClassId);
const char* CFigureAnimationState::GetClassName() { return kStateClassName; }
int CFigureAnimationState::GetClassId() { return kStateClassId; }

// The scene renderer's slot 6: the last frame's time in milliseconds. 4 bytes (too short
// to hook).
std::uint32_t __fastcall RendererFrameTime(CSceneRenderer* self) { return self->frameTime_; }
EI_REPLACE(0x00500A90, RendererFrameTime);

// A vector copied (the selection marker's corners).
void Vector3::Assign(const Vector3* other) {
    x = other->x;
    y = other->y;
    z = other->z;
}
EI_REWRITTEN(0x00503F20, Vector3::Assign);
