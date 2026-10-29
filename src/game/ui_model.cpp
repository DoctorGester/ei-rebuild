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
#include <cstdint>
#include <string>
#include "game_singletons.h"

#include <vector>

namespace {

constexpr std::uint32_t kRestPoseKind = 0x80590000;

void ShowFigurePart(CFigureMesh* part, int show) {
    CEngineObject* object = part;
    if (static_cast<std::uint8_t>(show)) object->flags_ &= ~FigureMeshFlag::Hidden;
    else object->flags_ |= FigureMeshFlag::Hidden;
}

// The interface's animation clock: timeGetTime() * 1/67, the milliseconds loaded as an
// unsigned qword.
float InterfaceClock() {
    constexpr float kRate = 0x1.e9131ap-7f;   // 0.0149253728
    return static_cast<float>(x87::FromUnsigned(gameapi::timeGetTime()) * static_cast<double>(kRate));
}

// The preview's map object: class 0x33 under a fixed id; MapObjectFlag::Preview on it.
constexpr int kPreviewId = static_cast<int>(0x88CA6C00u);

} // namespace

// The object released and forgotten.
CUiModel::~CUiModel() {
    if (object_) object_->Release();
}

// Animation `animation` of the object played.
void CUiModel::Play(SkillAnimation animation) {
    CObject3D* object = object_;
    object->PlayAnimation(object->FindSkillAnimation(animation));
}

// The rest pose: the object named like the figure of kind 0x80590000 from the object manager, played.
void CUiModel::Stop() {
    CObject3D* object = object_;
    std::string name(object->name);
    object->PlayAnimation(g_clientWorld.objectManager_->FindAnimation(&name, kRestPoseKind));
}

// The part named `name` shown or hidden (`show`'s low byte).
void CUiModel::ShowPart(const char* name, int show) {
    if (CFigureMesh* part = object_->FindPart(name)) ShowFigurePart(part, show);
}

// Every part shown or hidden; null entries skipped.
void CUiModel::ShowAllParts(int show) {
    for (int i = 0; i < std::ssize(object_->parts); ++i)
        if (auto* part = object_->parts[i]) ShowFigurePart(part, show);
}

// Draws the object in `pass` in plain white (the pass's colour put back afterwards) at the interface clock,
// with MapObjectFlag::NoEdgeFade set during the draw.
void CUiModel::Draw(CSceneRenderer* pass) {
    std::uint32_t saved = pass->colour_;
    pass->SetColour(0xFFFFFFFF);
    CObject3D* object = object_;
    object->flags_ |= MapObjectFlag::NoEdgeFade;
    object->AdvanceTime(InterfaceClock());
    object->DrawCollectDirty(pass);
    object_->flags_ &= ~MapObjectFlag::NoEdgeFade;
    pass->SetColour(saved);
}

void CUiModel::Load(const char* name, const char* from, const char* to) {
    auto* manager = g_clientWorld.MapObjectManager();
    auto* object = manager->CreateObjectAs<CObject3D>(MapObjectType::Creature, kPreviewId);
    object_ = object;
    ++object->refCount_;
    object->flags_ |= MapObjectFlag::Preview;
    object->localPosition_.x = 0.0f;
    object->localPosition_.y = 0.0f;
    object->localPosition_.z = 3.0f;
    CObject3DClientSpecific* specific = object->ClientSpecific();
    std::vector<std::string> parts;
    object->MakeFigure(name, &parts, 1);
    specific->RegisterPartMaterials();
    object->PlayAnimation(nullptr);
    object->RestartPose();
    specific->SetSkins(from, to);
}
