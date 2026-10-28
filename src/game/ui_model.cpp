// A window's 3D model preview (CUiModel, ui.h): its object's animations, parts and drawing. The
// object is a CObject3D (map_object_figures.h). See docs/notes/ui.md, section 2.
#include "map_object_shadow.h"
#include "model.h"
#include "ui.h"

#include "base_manager.h"
#include "map_object_figures.h"
#include "unit_create.h"
#include "world.h"

#include "ei/x87.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "game_data.h"
#include "game_singletons.h"

#include <string>
#include <vector>

namespace {

constexpr std::uint32_t kRestPoseKind = 0x80590000;

// A part is hidden while bit 0x80 of its flags is set.
constexpr std::uint32_t kHidden = 0x80;
void ShowFigurePart(CFigureMesh* part, int show) {
    CEngineObject* object = part;
    if (static_cast<std::uint8_t>(show)) object->flags_ &= ~kHidden;
    else object->flags_ |= kHidden;
}

// The interface's animation clock: timeGetTime() * 1/67, the milliseconds loaded as an
// unsigned qword.
float InterfaceClock() {
    constexpr float kRate = 0x1.e9131ap-7f;   // 0.0149253728
    return static_cast<float>(x87::FromUnsigned(gameapi::timeGetTime()) * static_cast<double>(kRate));
}

// The preview's map object: class 0x33 under a fixed id; flag 0x4000000 on it.
constexpr int kPreviewClass = 0x33;
constexpr int kPreviewId = static_cast<int>(0x88CA6C00u);
constexpr std::uint32_t kPreviewObject = 0x4000000;

// Bit 0x800 of the object's flags is set while a preview draws it.
constexpr std::uint32_t kPreviewDraw = 0x800;

} // namespace

// The object released and forgotten.
CUiModel::~CUiModel() {
    if (object_) object_->Release();
}

// Skill animation `animation` + 1 of the object played.
void CUiModel::Play(int animation) {
    CObject3D* object = Figure();
    object->PlayAnimation(object->FindSkillAnimation(animation + 1), 1);
}

// The rest pose: the object named like the figure of kind 0x80590000 from the object manager, played.
void CUiModel::Stop() {
    CObject3D* object = Figure();
    std::string name(object->name);
    object->PlayAnimation(TheObjectManager()->FindAnimation(&name, kRestPoseKind), 1);
}

// The part named `name` shown or hidden (`show`'s low byte).
void CUiModel::ShowPart(const char* name, int show) {
    if (CFigureMesh* part = Figure()->FindPart(name)) ShowFigurePart(part, show);
}

// Every part shown or hidden; null entries skipped.
void CUiModel::ShowAllParts(int show) {
    for (int i = 0; i < std::ssize(Figure()->parts); ++i)
        if (auto* part = Figure()->parts[i]) ShowFigurePart(part, show);
}

// Draws the object in `pass` in plain white (the pass's colour put back afterwards) at the interface clock,
// with flag 0x800 set during the draw.
void CUiModel::Draw(CSceneRenderer* pass) {
    std::uint32_t saved = pass->colour_;
    pass->SetColour(0xFFFFFFFF);
    CObject3D* object = Figure();
    object->flags_ |= kPreviewDraw;
    object->AdvanceTime(InterfaceClock());
    object->DrawCollectDirty(pass);
    Figure()->flags_ &= ~kPreviewDraw;
    pass->SetColour(saved);
}

void CUiModel::Load(const char* name, const char* from, const char* to) {
    auto* manager = TheClientWorld()->MapObjectManager();
    auto* object = manager->CreateObjectAs<CObject3D>(kPreviewClass, kPreviewId);
    object_ = object;
    ++object->refCount_;
    object->flags_ |= kPreviewObject;
    object->localPosition_.x = 0.0f;
    object->localPosition_.y = 0.0f;
    object->localPosition_.z = 3.0f;
    CObject3DClientSpecific* specific = object->ClientSpecific();
    std::vector<std::string> parts;
    object->MakeFigure(name, &parts, 1);
    specific->RegisterPartMaterials();
    object->PlayAnimationSecond(0, nullptr);   // (the original passes the model name as the unused argument)
    object->RestartPose();
    specific->SetSkins(from, to);
}
