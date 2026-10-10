// Checks for the animation state's skeleton (map_object_animation_bones.cpp, A-U10).
// - Construct (0050C5F0) allocates nothing: the original builds into a raw copy of the object's
//   memory, ours into the object; bytes +0x00..+0x74 and the results compared.
// - FindBone (0050CC20): pure, original vs ours. FindRoot (0050D2E0): root and `changed` kept, the
//   original runs, its results are kept and the two fields put back, ours runs; compared.
// - The rest change lists, references and the figure manager's data: ours only, the state checked
//   afterwards (blank after Reset / CreateState / DestroyState, the new bone at the list's tail
//   with its parent linked, meshes attached where a bone has the name, morph on every bone, ...).
// Reached by: every figure with a skeleton (unit spawns, world load: the skeleton links 005AC880
// call LinkPiece and FindRoot, LoadPart attaches meshes, SetFigureScale rescales; the client draw
// updates the pose; units / unit_create / models / spells_cast scenarios).
#include "verify.h"

#include "../../src/game/figure_mesh.h"
#include "../../src/game/map_object_figures.h"

#include <cstdint>
#include <cstring>

namespace {

constexpr std::size_t kConstructed = 0x75;   // the constructor writes up to `changed`

bool IsBlank(const CFigureAnimationState* state) {
    return !state->owner && !state->root && state->bones.count == 0 && !state->bones.head && !state->bones.blocks &&
           !state->current && state->queue.count == 0 && !state->queue.blocks && state->time == 0.0f &&
           !state->onEnd && state->morphS == 0.0f && state->morphT == 0.0f && state->morphU == 0.0f &&
           state->changed == 1;
}

bool SameText(const char* a, const char* b) { return std::strcmp(a, b) == 0; }

} // namespace

// 0050C5F0: PUSH -1; PUSH 0x0071CE8E (7 bytes).
static void* s_construct;
static CFigureAnimationState* __fastcall ConstructVerify(CFigureAnimationState* state, void*) {
    alignas(8) unsigned char scratch[sizeof(CFigureAnimationState)];
    std::memcpy(scratch, static_cast<void*>(state), sizeof(scratch));
    auto* copy = reinterpret_cast<CFigureAnimationState*>(scratch);
    using Original = CFigureAnimationState*(__fastcall*)(CFigureAnimationState*);
    CFigureAnimationState* expected = reinterpret_cast<Original>(s_construct)(copy);
    CFigureAnimationState* result = state->Construct();
    bool ok = expected == copy && result == state && std::memcmp(scratch, static_cast<void*>(state), kConstructed) == 0;
    ei::verify::Check("CFigureAnimationState::Construct", true, ok, "state %p", static_cast<void*>(state));
    return result;
}
EI_VERIFY(0x0050C5F0, ConstructVerify, 7, s_construct);

static void* s_unusedDestruct;
static void __fastcall DestructBodyVerify(CFigureAnimationState* state, void*) {
    state->DestructBody();
    std::uintptr_t vtable;
    std::memcpy(&vtable, static_cast<void*>(state), sizeof(vtable));
    bool ok = vtable == CEngineObject::kVtableAddress && !state->owner_ && state->bones.count == 0 &&
              state->queue.count == 0;
    ei::verify::Check("CFigureAnimationState::DestructBody", true, ok, "vtable %08X", static_cast<unsigned>(vtable));
}
EI_VERIFY(0x0050C730, DestructBodyVerify, 0, s_unusedDestruct);

static void* s_unusedDelete;
static CFigureAnimationState* __fastcall DeleteStateVerify(CFigureAnimationState* state, void*, unsigned flags) {
    CFigureAnimationState* result = state->DeleteState(flags);
    ei::verify::Check("CFigureAnimationState::DeleteState", true, result == state, "flags %u", flags);
    return result;
}
EI_VERIFY(0x0050C710, DeleteStateVerify, 0, s_unusedDelete);

static void* s_unusedCreate;
static int __fastcall CreateStateVerify(CFigureAnimationState* state, void*) {
    int result = state->CreateState();
    ei::verify::Check("CFigureAnimationState::CreateState", true, result == 1 && IsBlank(state), "result %d", result);
    return result;
}
EI_VERIFY(0x0050C860, CreateStateVerify, 0, s_unusedCreate);

static void* s_unusedDestroy;
static void __fastcall DestroyStateVerify(CFigureAnimationState* state, void*) {
    state->DestroyState();
    ei::verify::Check("CFigureAnimationState::DestroyState", true, IsBlank(state), "state %p", static_cast<void*>(state));
}
EI_VERIFY(0x0050C8E0, DestroyStateVerify, 0, s_unusedDestroy);

static void* s_unusedReset;
static void __fastcall ResetVerify(CFigureAnimationState* state, void*) {
    state->Reset();
    ei::verify::Check("CFigureAnimationState::Reset", true, IsBlank(state), "state %p", static_cast<void*>(state));
}
EI_VERIFY(0x0050C9E0, ResetVerify, 0, s_unusedReset);

static void* s_unusedRelease;
static void __fastcall ReleaseBonesVerify(CFigureAnimationState* state, void*) {
    state->ReleaseBones();
    bool ok = state->bones.count == 0 && !state->bones.head && !state->bones.tail && !state->bones.blocks;
    ei::verify::Check("CFigureAnimationState::ReleaseBones", true, ok, "%d left", state->bones.count);
}
EI_VERIFY(0x0050C990, ReleaseBonesVerify, 0, s_unusedRelease);

// 0050CC20: PUSH EBX; PUSH ESI; MOV ESI,[ECX+0x28] (5 bytes).
static void* s_findBone;
static FigureBone* __fastcall FindBoneVerify(CFigureAnimationState* state, void*, const char* name) {
    using Original = FigureBone*(__fastcall*)(CFigureAnimationState*, void*, const char*);
    FigureBone* expected = reinterpret_cast<Original>(s_findBone)(state, nullptr, name);
    FigureBone* actual = state->FindBone(name);
    ei::verify::Check("CFigureAnimationState::FindBone", expected, actual, "%p vs %p (%s)", static_cast<void*>(expected),
                      static_cast<void*>(actual), name);
    return expected;
}
EI_VERIFY(0x0050CC20, FindBoneVerify, 5, s_findBone);

// A new bone at the tail, referenced, its parent (if named and present) holding it, `changed` set.
static bool Linked(const CFigureAnimationState* state, const FigureBone* bone, int countBefore) {
    if (state->bones.count != countBefore + 1 || state->bones.tail->data != bone || state->changed != 1) return false;
    if (!SameText(bone->name3, state->owner->name)) return false;
    if (bone->morphS != state->morphS || bone->morphT != state->morphT || bone->morphU != state->morphU) return false;
    if (!bone->parent) return true;
    return SameText(bone->parent->name, bone->name2) && bone->parent->children.Find(bone);
}

static void* s_unusedLinkPiece;
static int __fastcall LinkPieceVerify(CFigureAnimationState* state, void*, void* model, const char* child,
                                      const char* parentName) {
    int before = state->bones.count;
    int result = state->LinkPiece(model, child, parentName);
    const FigureBone* bone = state->bones.tail ? state->bones.tail->data : nullptr;
    bool ok = result == 1 && bone && SameText(bone->name, child) && SameText(bone->name2, parentName) &&
              Linked(state, bone, before);
    ei::verify::Check("CFigureAnimationState::LinkPiece", true, ok, "%s <- %s, %d bones", parentName, child,
                      state->bones.count);
    return result;
}
EI_VERIFY(0x0050CC60, LinkPieceVerify, 0, s_unusedLinkPiece);

static void* s_unusedLinkBone;
static int __fastcall LinkBoneVerify(CFigureAnimationState* state, void*, void* model, FigureBone* bone) {
    int before = state->bones.count;
    int references = bone->refCount_;
    int result = state->LinkBone(model, bone);
    bool ok = result == 1 && bone->refCount_ == references + 1 && Linked(state, bone, before);
    ei::verify::Check("CFigureAnimationState::LinkBone", true, ok, "%s, %d bones", static_cast<const char*>(bone->name),
                      state->bones.count);
    return result;
}
EI_VERIFY(0x0050CD70, LinkBoneVerify, 0, s_unusedLinkBone);

static void* s_unusedAttach;
static int __fastcall AttachMeshVerify(CFigureAnimationState* state, void*, CEngineObject* part) {
    const char* name = part->GetName();
    FigureBone* named = nullptr;
    for (auto* node = state->bones.head; node && !named; node = node->next)
        if (SameText(node->data->name, name)) named = node->data;
    int result = state->AttachMesh(part);
    bool ok = state->changed == 1;
    if (named) {
        const auto* mesh = named->mesh;
        ok = ok && result == 1 && reinterpret_cast<const CEngineObject*>(mesh) == part &&
             std::memcmp(&mesh->position_, &named->position, sizeof(Vector3)) == 0 &&
             std::memcmp(&mesh->rotation_, &named->rotation, sizeof(Quaternion)) == 0;
    } else {
        ok = ok && result == 0;
    }
    ei::verify::Check("CFigureAnimationState::AttachMesh", true, ok, "%s: %d", name, result);
    return result;
}
EI_VERIFY(0x0050D0E0, AttachMeshVerify, 0, s_unusedAttach);

// 0050D2E0: MOV EAX,[ECX+0x28]; PUSH ESI; TEST EAX,EAX (6 bytes).
static void* s_findRoot;
static int __fastcall FindRootVerify(CFigureAnimationState* state, void*) {
    FigureBone* rootBefore = state->root;
    std::uint8_t changedBefore = state->changed;
    int expected = reinterpret_cast<int(__fastcall*)(CFigureAnimationState*)>(s_findRoot)(state);
    FigureBone* expectedRoot = state->root;
    std::uint8_t expectedChanged = state->changed;
    state->root = rootBefore;
    state->changed = changedBefore;
    int actual = state->FindRoot();
    bool ok = expected == actual && expectedRoot == state->root && expectedChanged == state->changed;
    ei::verify::Check("CFigureAnimationState::FindRoot", true, ok, "%d / %d, root %p / %p", expected, actual,
                      static_cast<void*>(expectedRoot), static_cast<void*>(state->root));
    return expected;
}
EI_VERIFY(0x0050D2E0, FindRootVerify, 6, s_findRoot);

static void* s_unusedSetModel;
static int __fastcall SetModelVerify(CFigureAnimationState* state, void*, const char* model) {
    int result = state->SetModel(model);
    ei::verify::Check("CFigureAnimationState::SetModel", true, result == 1 && state->changed == 1, "%s: %d", model,
                      result);
    return result;
}
EI_VERIFY(0x0050D320, SetModelVerify, 0, s_unusedSetModel);

static void* s_unusedDetach;
static int __fastcall DetachMeshVerify(CFigureAnimationState* state, void*, CEngineObject* part) {
    bool carried = false;
    for (auto* node = state->bones.head; node; node = node->next)
        carried = carried || (node->data->mesh && reinterpret_cast<CEngineObject*>(node->data->mesh) == part);
    int result = state->DetachMesh(part);
    bool ok = result == (carried ? 1 : 0) && state->changed == 1;
    ei::verify::Check("CFigureAnimationState::DetachMesh", true, ok, "carried %d, result %d", carried, result);
    return result;
}
EI_VERIFY(0x0050D350, DetachMeshVerify, 0, s_unusedDetach);

static void* s_unusedDetachAll;
static int __fastcall DetachMeshesVerify(CFigureAnimationState* state, void*) {
    int result = state->DetachMeshes();
    bool ok = result == 1 && state->changed == 1;
    for (auto* node = state->bones.head; node; node = node->next) ok = ok && !node->data->mesh;
    ei::verify::Check("CFigureAnimationState::DetachMeshes", true, ok, "%d bones", state->bones.count);
    return result;
}
EI_VERIFY(0x0050D3A0, DetachMeshesVerify, 0, s_unusedDetachAll);

// 0050D3E0 / 0050D450 place the skeleton through 0050C230 (compared by its own wrapper): ours
// only, `changed` cleared.
static void* s_unusedPlace;
static void __fastcall PlaceIfChangedVerify(CFigureAnimationState* state, void*) {
    state->PlaceIfChanged();
    ei::verify::Check("CFigureAnimationState::PlaceIfChanged", 0, static_cast<int>(state->changed), "changed %d",
                      state->changed);
}
EI_VERIFY(0x0050D3E0, PlaceIfChangedVerify, 0, s_unusedPlace);

static void* s_unusedUpdate;
static void __fastcall UpdatePoseVerify(CFigureAnimationState* state, void*) {
    state->UpdatePose();
    ei::verify::Check("CFigureAnimationState::UpdatePose", 0, static_cast<int>(state->changed), "changed %d",
                      state->changed);
}
EI_VERIFY(0x0050D450, UpdatePoseVerify, 0, s_unusedUpdate);

static void* s_unusedStop;
static void __fastcall StopVerify(CFigureAnimationState* state, void*) {
    state->Stop();
    bool ok = state->queue.count == 0 && !state->current && state->changed == 1;
    ei::verify::Check("CFigureAnimationState::Stop", true, ok, "%d queued", state->queue.count);
}
EI_VERIFY(0x0050D5A0, StopVerify, 0, s_unusedStop);

static void* s_unusedRescale;
static void __fastcall RescaleVerify(CFigureAnimationState* state, void*, float s, float t, float u) {
    state->Rescale(s, t, u);
    bool ok = state->changed == 0 && state->morphS == s && state->morphT == t && state->morphU == u;
    for (auto* node = state->bones.head; node; node = node->next)
        ok = ok && node->data->morphS == s && node->data->morphT == t && node->data->morphU == u;
    ei::verify::Check("CFigureAnimationState::Rescale", true, ok, "(%g, %g, %g), %d bones", s, t, u, state->bones.count);
}
EI_VERIFY(0x0050D7C0, RescaleVerify, 0, s_unusedRescale);

static void* s_unusedNames;
static void __fastcall CollectBoneNamesVerify(CFigureAnimationState* state, void*, mfc::CStringArray* out) {
    int before = out->size;
    state->CollectBoneNames(out);
    bool ok = out->size == before + state->bones.count;
    int at = before;
    for (auto* node = state->bones.head; node && ok; node = node->next, ++at)
        ok = SameText((*out)[at], node->data->name);
    ei::verify::Check("CFigureAnimationState::CollectBoneNames", true, ok, "%d names", out->size);
}
EI_VERIFY(0x0050D920, CollectBoneNamesVerify, 0, s_unusedNames);

// 0050D950 / 0050DA10 (the figure manager's 005ACCF0): ours only; every bone's placing track
// plays nothing without key frames / plays `animation`; `changed` set.
static void* s_unusedClearTracks;
static int __fastcall ClearBoneTracksVerify(CFigureAnimationState* state, void*) {
    int result = state->ClearBoneTracks();
    bool ok = result == 1 && state->changed == 1;
    for (auto* node = state->bones.head; node; node = node->next) {
        const AnimationTrack* track = node->data->pose;
        ok = ok && !track->animation && track->frames.size == 0 && !track->vertices.data && track->time == 0.0f;
    }
    ei::verify::Check("CFigureAnimationState::ClearBoneTracks", true, ok, "%d bones", state->bones.count);
    return result;
}
EI_VERIFY(0x0050D950, ClearBoneTracksVerify, 0, s_unusedClearTracks);

static void* s_unusedLoadTracks;
static int __fastcall LoadBoneTracksVerify(CFigureAnimationState* state, void*, CFigureModelPart* part,
                                           const FigureAnimation* animation) {
    int result = state->LoadBoneTracks(part, animation);
    bool ok = result == 1 && state->changed == 1;
    for (auto* node = state->bones.head; node; node = node->next) ok = ok && node->data->pose->animation == animation;
    ei::verify::Check("CFigureAnimationState::LoadBoneTracks", true, ok, "%d bones", state->bones.count);
    return result;
}
EI_VERIFY(0x0050DA10, LoadBoneTracksVerify, 0, s_unusedLoadTracks);

// 0050DFF0: AlignedPtrArray::SetSize (the animation tracks' copy): ours only, the size as asked,
// the block 32-byte aligned (none at 0).
static void* s_unusedSetSize;
static void __fastcall TrackSetSizeVerify(AlignedPtrArray* array, void*, int count, int growBy) {
    array->SetSize(count, growBy);
    bool ok = array->size == count && array->capacity >= count &&
              (count == 0 ? !array->data : (reinterpret_cast<std::uintptr_t>(array->data) & 0x1F) == 0);
    ei::verify::Check("AlignedPtrArray::SetSize (0050DFF0)", true, ok, "size %d capacity %d", array->size,
                      array->capacity);
}
EI_VERIFY(0x0050DFF0, TrackSetSizeVerify, 0, s_unusedSetSize);
