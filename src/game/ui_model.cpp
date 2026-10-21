// A window's 3D model preview (CUiModel, ui.h): its object's animations, parts and drawing. The
// object is a CObject3D (map_object_figures.h). See docs/notes/ui.md, section 2.
#include "map_object_shadow.h"
#include "ui.h"

#include "base_manager.h"
#include "console.h"
#include "map_object_figures.h"
#include "unit_create.h"
#include "world.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "originals_ui.h"
#include "originals_units.h"
#include "game_data.h"
#include "game_singletons.h"

namespace {

// 005187B0 (thiscall on the object, RET 8): plays `animation` (with `restart` 1 from here).
void PlayAnimation(CObject3D* object, void*, FigureAnimation* animation, int restart) {
    object->PlayAnimation(animation, restart);
}
// 00512B20 (thiscall on the object manager 007AF5F8, RET 8): the object of `kind` named `name`.
FigureAnimation* FindNamed(void* manager, void*, const mfc::CString* name, std::uint32_t kind) {
    return static_cast<FigureAnimation*>(static_cast<CMapObjectManager*>(manager)->FindAnimation(name, kind));
}
constexpr std::uint32_t kRestPoseKind = 0x80590000;

// A part is hidden while bit 0x80 of its flags is set.
constexpr std::uint32_t kHidden = 0x80;
void ShowFigurePart(CFigureMesh* part, int show) {
    auto* object = reinterpret_cast<CEngineObject*>(part);
    if (static_cast<std::uint8_t>(show)) object->flags_ &= ~kHidden;
    else object->flags_ |= kHidden;
}

// The object's virtual slots 25 (the animation clock, seconds as float) and 23 (draw in `pass`).
void SetClock(CObject3D* object, float seconds) {
    using Fn = void(__thiscall*)(CObject3D*, float);
    ei::ObjectSlot<Fn>(object, 25)(object, seconds);
}
void DrawObject(CObject3D* object, UiDrawPass* pass) {
    using Fn = void(__thiscall*)(CObject3D*, UiDrawPass*);
    ei::ObjectSlot<Fn>(object, 23)(object, pass);
}

// The interface's animation clock: timeGetTime() * 1/67 (00740EFC), the milliseconds loaded as an
// unsigned qword.
float InterfaceClock() {
    constexpr float kRate = 0x1.e9131ap-7f;   // 0.0149253728 (00740EFC)
    return static_cast<float>(x87::FromUnsigned(gameapi::timeGetTime()) * static_cast<double>(kRate));
}

// The preview's map object: class 0x33 under a fixed id; flag 0x4000000 on it.
constexpr int kPreviewClass = 0x33;
constexpr int kPreviewId = static_cast<int>(0x88CA6C00u);
constexpr std::uint32_t kPreviewObject = 0x4000000;

// Bit 0x800 of the object's flags is set while a preview draws it.
constexpr std::uint32_t kPreviewDraw = 0x800;

} // namespace

// 005FC1D0. The object released (slot 4) and forgotten.
void CUiModel::ReleaseObject() {
    if (object_) reinterpret_cast<CEngineObject*>(object_)->Release();
    object_ = nullptr;
}

// 005FC3B0 (RET 4). Skill animation `animation` + 1 of the object (00518FC0) played.
void CUiModel::Play(int animation) {
    CObject3D* object = Figure();
    PlayAnimation(object, nullptr, object->FindSkillAnimation(animation + 1), 1);
}

// 005FC3D0. The rest pose: the object named like the figure (+0xA4) of kind 0x80590000 from the
// object manager, played.
void CUiModel::Stop() {
    CObject3D* object = Figure();
    mfc::CString name(object->name);
    PlayAnimation(object, nullptr, FindNamed(TheObjectManager(), nullptr, &name, kRestPoseKind), 1);
}
EI_REWRITTEN(0x005FC3D0, CUiModel::Stop);

// 005FC4C0 (RET 8). The part named `name` (005169A0) shown or hidden (`show`'s low byte).
void CUiModel::ShowPart(const char* name, int show) {
    if (CFigureMesh* part = Figure()->FindPart(name)) ShowFigurePart(part, show);
}

// 005FC4F0 (RET 4). Every part (+0xD0) shown or hidden; null entries skipped.
void CUiModel::ShowAllParts(int show) {
    for (int i = 0; i < Figure()->parts.size; ++i)
        if (auto* part = static_cast<CFigureMesh*>(Figure()->parts.data[i])) ShowFigurePart(part, show);
}

// 005FC2F0 (RET 4). Draws the object in `pass` in plain white (the pass's colour, slot 7, put back
// afterwards) at the interface clock, with flag 0x800 set during the draw. The original builds
// the white from three FISTPs of 255.0.
void CUiModel::Draw(UiDrawPass* pass) {
    std::uint32_t saved = pass->colour_;
    pass->SetColour(0xFFFFFFFF);
    CObject3D* object = Figure();
    object->flags_ |= kPreviewDraw;
    SetClock(object, InterfaceClock());
    DrawObject(object, pass);
    Figure()->flags_ &= ~kPreviewDraw;
    pass->SetColour(saved);
}
EI_REWRITTEN(0x005FC2F0, CUiModel::Draw);

// 005FC1F0 (RET 0xC). The figure's steps go through their addresses (rewritten, with wrappers).
void CUiModel::Load(const char* name, const char* from, const char* to) {
    auto* manager = TheClientWorld()->MapObjectManager();
    auto* object = static_cast<CObject3D*>(manager->CreateObject(kPreviewClass, kPreviewId));
    object_ = reinterpret_cast<CTransformObject*>(object);
    ++object->refCount_;
    object->flags_ |= kPreviewObject;
    object->position.x = 0.0f;
    object->position.y = 0.0f;
    object->position.z = 3.0f;
    CEngineObject* specific = object->attached;
    console::CStringArray parts;
    object->MakeFigure(name, &parts, 1);
    static_cast<CObject3DClientSpecific*>(specific)->RegisterPartMaterials();
    object->PlayAnimationSecond(0, nullptr);   // (the original passes the model name as the unused argument)
    object->RestartPose();
    static_cast<CObject3DClientSpecific*>(specific)->SetSkins(from, to);
}
EI_REWRITTEN(0x005FC1F0, CUiModel::Load);
