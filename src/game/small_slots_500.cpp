// Small leftovers of 0x00500000-0x0050E000 (A-U12): the class names and ids of the animation
// classes, the scene renderer's frame time and a vector copy.
#include "map_object_animation.h"
#include "map_object_figures.h"
#include "model.h"

#include "ei/hook.h"

namespace {
constexpr std::uintptr_t kTrackClassName = std::uintptr_t(0x0078EF1C);   // "CAnimationTrack"
constexpr std::uintptr_t kBoneClassName = std::uintptr_t(0x0078EF2C);    // "CAnimationNode"
constexpr std::uintptr_t kStateClassName = std::uintptr_t(0x0078EF3C);   // "CAnimationSystem2"
constexpr int kTrackClassId = 0x22;
constexpr int kBoneClassId = 0x21;
constexpr int kStateClassId = 0x21;   // the same id as the bones'

const char* Text(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }
} // namespace

// 0050A6E0 / 0050A6F0 (the animation track's slots 11 / 12, vtable 0073D9D8).
const char* __fastcall AnimationTrackClassName(AnimationTrack*) { return Text(kTrackClassName); }
EI_REPLACE(0x0050A6E0, AnimationTrackClassName);
int __fastcall AnimationTrackClassId(AnimationTrack*) { return kTrackClassId; }
EI_REPLACE(0x0050A6F0, AnimationTrackClassId);

// 0050B180 / 0050B190 / 0050B1A0 (a figure bone's slots 7 / 11 / 12, vtable 0073DA0C): its name (the
// piece's), class name and id. The first is 4 bytes (too short to hook).
const char* __fastcall FigureBoneName(FigureBone* self) { return self->name; }
EI_REPLACE(0x0050B180, FigureBoneName);
const char* __fastcall FigureBoneClassName(FigureBone*) { return Text(kBoneClassName); }
EI_REPLACE(0x0050B190, FigureBoneClassName);
int __fastcall FigureBoneClassId(FigureBone*) { return kBoneClassId; }
EI_REPLACE(0x0050B1A0, FigureBoneClassId);

// 0050C6F0 / 0050C700 (the figure animation state's slots 11 / 12, vtable 0073DA4C).
const char* __fastcall AnimationStateClassName(CFigureAnimationState*) { return Text(kStateClassName); }
EI_REPLACE(0x0050C6F0, AnimationStateClassName);
int __fastcall AnimationStateClassId(CFigureAnimationState*) { return kStateClassId; }
EI_REPLACE(0x0050C700, AnimationStateClassId);

// 00500A90 (the scene renderer's slot 6): the last frame's time in milliseconds. 4 bytes (too short
// to hook).
std::uint32_t __fastcall RendererFrameTime(CSceneRenderer* self) { return self->frameTime_; }
EI_REPLACE(0x00500A90, RendererFrameTime);

// 00503F20 (RET 4). A vector copied (the selection marker's corners).
void Vector3::Assign(const Vector3* other) {
    x = other->x;
    y = other->y;
    z = other->z;
}
EI_REPLACE_METHOD(0x00503F20, Vector3::Assign);
