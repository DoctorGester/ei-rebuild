// Checks for the animation track's lifetime (animation_track_lifetime.cpp, A-U10).
// - Construct (0050A5D0) allocates nothing (its arrays start empty): the original builds into a raw
//   copy of the track's memory, ours into the track; all 0x78 bytes and the results compared.
// - CreateTrack / DestroyTrack (0050A810 / 0050A8C0), DestructBody (0050A720), DeleteTrack (0050A700):
//   ours only; afterwards the reset state (no animation, bone, key frames or vertices, the default
//   position and rotation, time 0) / the engine object's vtable.
// Reached by every bone's animator (units: made on the first animation, released with the figure).
#include "verify.h"

#include "../../src/game/map_object_animation.h"

#include <cstdint>
#include <cstring>

namespace {

bool IsReset(const AnimationTrack* track) {
    const auto& position = *reinterpret_cast<const Vector3*>(std::uintptr_t(0x007AF110));
    const auto& rotation = *reinterpret_cast<const Quaternion*>(std::uintptr_t(0x007AF100));
    return !track->animation && !track->bone && track->positionCount == 0 && !track->positions &&
           track->rotationCount == 0 && !track->rotations && track->vertexCount == 0 && !track->frames.data &&
           track->frames.size == 0 && !track->vertices.data && track->vertices.size == 0 && track->time == 0.0f &&
           std::memcmp(&track->position, &position, sizeof(Vector3)) == 0 &&
           std::memcmp(&track->rotation, &rotation, sizeof(Quaternion)) == 0;
}

void* s_unusedCreate;
void* s_unusedDestroy;
void* s_unusedDestruct;
void* s_unusedDelete;

} // namespace

// 0050A5D0: MOV EAX,FS:[0] (6 bytes).
static void* s_construct;
static AnimationTrack* __fastcall ConstructVerify(AnimationTrack* track, void*) {
    alignas(8) unsigned char scratch[sizeof(AnimationTrack)];
    std::memcpy(scratch, static_cast<void*>(track), sizeof(scratch));
    auto* copy = reinterpret_cast<AnimationTrack*>(scratch);
    AnimationTrack* expected = reinterpret_cast<AnimationTrack*(__fastcall*)(AnimationTrack*)>(s_construct)(copy);
    AnimationTrack* result = track->Construct();
    int differs = -1;
    const auto* ours = reinterpret_cast<const unsigned char*>(track);
    for (std::size_t i = 0; i < sizeof(AnimationTrack) && differs < 0; ++i)
        if (scratch[i] != ours[i]) differs = static_cast<int>(i);
    ei::verify::Check("AnimationTrack::Construct", true, expected == copy && result == track && differs < 0,
                      "first difference at +0x%X", differs);
    return result;
}
EI_VERIFY(0x0050A5D0, ConstructVerify, 6, s_construct);

static int __fastcall CreateTrackVerify(AnimationTrack* track, void*) {
    int result = track->CreateTrack();
    ei::verify::Check("AnimationTrack::CreateTrack", true, result == 1 && IsReset(track), "result %d", result);
    return result;
}
EI_VERIFY(0x0050A810, CreateTrackVerify, 0, s_unusedCreate);

static void __fastcall DestroyTrackVerify(AnimationTrack* track, void*) {
    track->DestroyTrack();
    ei::verify::Check("AnimationTrack::DestroyTrack", true, IsReset(track), "track %p", static_cast<void*>(track));
}
EI_VERIFY(0x0050A8C0, DestroyTrackVerify, 0, s_unusedDestroy);

static void __fastcall DestructBodyVerify(AnimationTrack* track, void*) {
    track->DestructBody();
    std::uintptr_t vtable;
    std::memcpy(&vtable, static_cast<void*>(track), sizeof(vtable));
    ei::verify::Check("AnimationTrack::DestructBody", true, vtable == CEngineObject::kVtableAddress && !track->owner_,
                      "vtable %08X", static_cast<unsigned>(vtable));
}
EI_VERIFY(0x0050A720, DestructBodyVerify, 0, s_unusedDestruct);

static AnimationTrack* __fastcall DeleteTrackVerify(AnimationTrack* track, void*, unsigned flags) {
    AnimationTrack* result = track->DeleteTrack(flags);
    ei::verify::Check("AnimationTrack::DeleteTrack", true, result == track, "flags %u", flags);
    return result;
}
EI_VERIFY(0x0050A700, DeleteTrackVerify, 0, s_unusedDelete);
