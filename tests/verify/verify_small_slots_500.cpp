// Checks for small_slots_500.cpp, scene_skin_lifetime.cpp, renderer_base_create.cpp and the view
// functions of renderer_init.cpp (A-U12).
// Side by side (pure):
// - The class names / ids (0050A6E0, 0050A6F0, 0050B190, 0050B1A0, 0050C6F0, 0050C700): animation
//   tracks, bones and states saved or logged (none known to be reached in a normal run).
// - VectorCopy (00503F20), MatrixTransposeInPlace (00501980), CTransformPipeline::Top (005019D0):
//   no scenario (their callers are rewritten or never called).
// Ours only (the state afterwards is checked):
// - SceneSkin::Construct / Destruct / Clear / SetUnknown00 (00508700, 005087B0, 005088E0,
//   00508930): every fake unit made and dropped (the main menu's and the interface's figures).
//   Create (00508870): a fake unit's scene load (00446AF0).
// - CSceneRenderer::CreateBase (00500540): once, at every start of the game.
// - CSceneRenderer::SetView (005015A0): none (nothing calls slot 25).
// FigureBoneName (0050B180) and RendererFrameTime (00500A90) are 4 bytes: too short to hook.
#include "verify.h"

#include "../../src/game/link_figure.h"
#include "../../src/game/map_object_animation.h"
#include "../../src/game/map_object_figures.h"
#include "../../src/game/model.h"
#include "../../src/game/options.h"
#include "../../src/game/scene_loader.h"

#include <cstring>

const char* __fastcall AnimationTrackClassName(AnimationTrack*);
int __fastcall AnimationTrackClassId(AnimationTrack*);
const char* __fastcall FigureBoneClassName(FigureBone*);
int __fastcall FigureBoneClassId(FigureBone*);
const char* __fastcall AnimationStateClassName(CFigureAnimationState*);
int __fastcall AnimationStateClassId(CFigureAnimationState*);
void __fastcall VectorCopy(Vector3* self, void*, const Vector3* other);
void __fastcall MatrixTransposeInPlace(float* m);

using scene::SceneSkin;

namespace {
void* s_unused;
} // namespace

// --- Class names and ids: each starts with MOV EAX,imm32 (5 bytes) ----------------------------------

static const char*(__fastcall* s_AnimationTrackClassName)(AnimationTrack*);
static const char* __fastcall AnimationTrackClassNameVerify(AnimationTrack* self) {
    const char* expected = s_AnimationTrackClassName(self);
    const char* actual = AnimationTrackClassName(self);
    ei::verify::Check("AnimationTrackClassName", true, expected == actual && std::strcmp(actual, "CAnimationTrack") == 0, "%s", expected);
    return expected;
}
EI_VERIFY(0x0050A6E0, AnimationTrackClassNameVerify, 5, s_AnimationTrackClassName);

static int(__fastcall* s_AnimationTrackClassId)(AnimationTrack*);
static int __fastcall AnimationTrackClassIdVerify(AnimationTrack* self) {
    int expected = s_AnimationTrackClassId(self);
    ei::verify::Check("AnimationTrackClassId", expected, AnimationTrackClassId(self), "");
    return expected;
}
EI_VERIFY(0x0050A6F0, AnimationTrackClassIdVerify, 5, s_AnimationTrackClassId);

static const char*(__fastcall* s_FigureBoneClassName)(FigureBone*);
static const char* __fastcall FigureBoneClassNameVerify(FigureBone* self) {
    const char* expected = s_FigureBoneClassName(self);
    const char* actual = FigureBoneClassName(self);
    ei::verify::Check("FigureBoneClassName", true, expected == actual && std::strcmp(actual, "CAnimationNode") == 0, "%s", expected);
    return expected;
}
EI_VERIFY(0x0050B190, FigureBoneClassNameVerify, 5, s_FigureBoneClassName);

static int(__fastcall* s_FigureBoneClassId)(FigureBone*);
static int __fastcall FigureBoneClassIdVerify(FigureBone* self) {
    int expected = s_FigureBoneClassId(self);
    ei::verify::Check("FigureBoneClassId", expected, FigureBoneClassId(self), "");
    return expected;
}
EI_VERIFY(0x0050B1A0, FigureBoneClassIdVerify, 5, s_FigureBoneClassId);

static const char*(__fastcall* s_AnimationStateClassName)(CFigureAnimationState*);
static const char* __fastcall AnimationStateClassNameVerify(CFigureAnimationState* self) {
    const char* expected = s_AnimationStateClassName(self);
    const char* actual = AnimationStateClassName(self);
    ei::verify::Check("AnimationStateClassName", true, expected == actual && std::strcmp(actual, "CAnimationSystem2") == 0, "%s", expected);
    return expected;
}
EI_VERIFY(0x0050C6F0, AnimationStateClassNameVerify, 5, s_AnimationStateClassName);

static int(__fastcall* s_AnimationStateClassId)(CFigureAnimationState*);
static int __fastcall AnimationStateClassIdVerify(CFigureAnimationState* self) {
    int expected = s_AnimationStateClassId(self);
    ei::verify::Check("AnimationStateClassId", expected, AnimationStateClassId(self), "");
    return expected;
}
EI_VERIFY(0x0050C700, AnimationStateClassIdVerify, 5, s_AnimationStateClassId);

// --- Small pure functions -----------------------------------------------------------------------------

// MOV EAX,[ESP+4] (4 bytes), MOV EDX,[EAX] (2).
static void(__fastcall* s_VectorCopy)(Vector3*, void*, const Vector3*);
static void __fastcall VectorCopyVerify(Vector3* self, void*, const Vector3* other) {
    Vector3 expected;
    s_VectorCopy(&expected, nullptr, other);
    VectorCopy(self, nullptr, other);
    ei::verify::Check("VectorCopy", true, std::memcmp(&expected, self, sizeof(Vector3)) == 0, "");
}
EI_VERIFY(0x00503F20, VectorCopyVerify, 6, s_VectorCopy);

// FLD [ECX+4] (3 bytes), MOV EAX,[ECX+10h] (3).
static void(__fastcall* s_MatrixTransposeInPlace)(float*);
static void __fastcall MatrixTransposeInPlaceVerify(float* m) {
    float expected[16];
    std::memcpy(expected, m, sizeof(expected));
    s_MatrixTransposeInPlace(expected);
    MatrixTransposeInPlace(m);
    ei::verify::Check("MatrixTransposeInPlace", true, std::memcmp(expected, m, sizeof(expected)) == 0, "");
}
EI_VERIFY(0x00501980, MatrixTransposeInPlaceVerify, 6, s_MatrixTransposeInPlace);

// MOV EAX,[ESP+4] (4 bytes), MOV EDX,[ECX+EAX*8+18h] (4).
static const Matrix4*(__fastcall* s_PipelineTop)(const CTransformPipeline*, void*, int);
static const Matrix4* __fastcall PipelineTopVerify(const CTransformPipeline* self, void*, int index) {
    const Matrix4* expected = s_PipelineTop(self, nullptr, index);
    ei::verify::Check("CTransformPipeline::Top", static_cast<const void*>(expected),
                      static_cast<const void*>(self->Top(index)), "%d", index);
    return expected;
}
EI_VERIFY(0x005019D0, PipelineTopVerify, 8, s_PipelineTop);

// --- The fake unit's skin -------------------------------------------------------------------------------

static SceneSkin* __fastcall SkinConstructVerify(SceneSkin* self) {
    SceneSkin* result = self->Construct();
    bool ok = result == self && !self->root && !self->bones.data && self->unknown_00 == 0 && self->clock[0] == 0 &&
              self->clock[1] == 0xBFF00000u && self->names.size == 0 && !self->unknown_3C.data &&
              self->unknown_38 == 0 && static_cast<const char*>(self->name)[0] == 0;
    ei::verify::Check("SceneSkin::Construct", true, ok, "");
    return result;
}
EI_VERIFY(0x00508700, SkinConstructVerify, 0, s_unused);

static void __fastcall SkinDestructVerify(SceneSkin* self) {
    self->Destruct();
    ei::verify::Check("SceneSkin::Destruct", true, !self->root && self->unknown_00 == 0, "");
}
EI_VERIFY(0x005087B0, SkinDestructVerify, 0, s_unused);

static int __fastcall SkinCreateVerify(SceneSkin* self) {
    int result = self->Create();
    bool ok = result == 1 && !self->root && self->clock[0] == 0 && self->clock[1] == 0 && self->names.size == 0 &&
              self->unknown_4C == 0 && std::strcmp(static_cast<const char*>(self->name), "none") == 0;
    ei::verify::Check("SceneSkin::Create", true, ok, "");
    return result;
}
EI_VERIFY(0x00508870, SkinCreateVerify, 0, s_unused);

static void __fastcall SkinClearVerify(SceneSkin* self) {
    const bool hadRoot = self->root != nullptr;
    self->Clear();
    bool ok = !self->root && self->unknown_00 == 0 && self->names.size == 0 && (!hadRoot || self->bones.size == 0);
    ei::verify::Check("SceneSkin::Clear", true, ok, "");
}
EI_VERIFY(0x005088E0, SkinClearVerify, 0, s_unused);

static void __fastcall SkinSetUnknown00Verify(SceneSkin* self, void*, std::uint32_t value) {
    self->SetUnknown00(value);
    ei::verify::Check("SceneSkin::SetUnknown00", value, self->unknown_00, "");
}
EI_VERIFY(0x00508930, SkinSetUnknown00Verify, 0, s_unused);

// --- The renderer ---------------------------------------------------------------------------------------

static void* __fastcall CreateBaseVerify(CSceneRenderer* self, void*, void* window, options::GameOptions* settings) {
    void* result = self->CreateBase(window, settings);
    bool ok = result == self->graphicsProcessor_ && self->pipeline_ && self->pipeline_->refs_ >= 1 &&
              self->graphicsLibrary_;
    ei::verify::Check("CSceneRenderer::CreateBase", true, ok, "");
    return result;
}
EI_VERIFY(0x00500540, CreateBaseVerify, 0, s_unused);

static void __fastcall SetViewVerify(CSceneRenderer* self, void*, const tagRECT* screen, float fieldOfView) {
    self->SetView(screen, fieldOfView);
    const MatrixStack* stacks = self->pipeline_->stacks_;
    bool ok = stacks[6].count == 1 && stacks[7].count == 1 && stacks[8].count == 1 && stacks[0].count == 3 &&
              stacks[1].count == 3 && stacks[2].count == 2 && stacks[5].count == 2 && stacks[14].count == 0;
    ei::verify::Check("CSceneRenderer::SetView", true, ok, "");
}
EI_VERIFY(0x005015A0, SetViewVerify, 0, s_unused);
