// A window's 3D model preview (UiModel, ui.h): its object's animations, parts and drawing. The
// object is a CObject3D (map_object_figures.h). See docs/notes/ui.md, section 2.
#include "map_object_shadow.h"
#include "model.h"
#include "ui.h"

#include "base_manager.h"
#include "map_object_figures.h"
#include "unit_create.h"
#include "world.h"

#include "lib/gameapi.h"
#include <cstdint>
#include <string>
#include "game_singletons.h"

#include <vector>

namespace {

constexpr std::uint32_t kRestPoseKind = 0x80590000;

void ShowFigurePart(FigureMesh* part, int show) {
    EngineObject* object = part;
    if (static_cast<std::uint8_t>(show)) object->flags &= ~FigureMeshFlag::Hidden;
    else object->flags |= FigureMeshFlag::Hidden;
}

// The interface's animation clock: timeGetTime() * 1/67, the milliseconds loaded as an
// unsigned qword.
float InterfaceClock() {
    constexpr float kRate = 0x1.e9131ap-7f;   // 0.0149253728
    return static_cast<float>(static_cast<double>(gameapi::timeGetTime()) * kRate);
}

// The preview's map object: class 0x33 under a fixed id; MapObjectFlag::Preview on it.
constexpr int kPreviewId = static_cast<int>(0x88CA6C00u);

} // namespace

// The object released and forgotten.
UiModel::~UiModel() {
    if (object) object->Release();
}

// Animation `animation` of the object played.
void UiModel::Play(SkillAnimation animation) {
    CObject3D* currentObject = object;
    currentObject->PlayAnimation(currentObject->FindSkillAnimation(animation));
}

// The rest pose: the object named like the figure of kind 0x80590000 from the object manager, played.
void UiModel::Stop() {
    CObject3D* currentObject = object;
    std::string name(currentObject->name);
    currentObject->PlayAnimation(g_clientWorld.objectManager->FindAnimation(&name, kRestPoseKind));
}

// The part named `name` shown or hidden (`show`'s low byte).
void UiModel::ShowPart(const char* name, int show) {
    if (FigureMesh* part = object->FindPart(name)) ShowFigurePart(part, show);
}

// Every part shown or hidden; null entries skipped.
void UiModel::ShowAllParts(int show) {
    for (int i = 0; i < std::ssize(object->parts); ++i)
        if (FigureMesh* part = object->parts[i]) ShowFigurePart(part, show);
}

// Draws the object in `pass` in plain white (the pass's colour put back afterwards) at the interface clock,
// with MapObjectFlag::NoEdgeFade set during the draw.
void UiModel::Draw(SceneRenderer* pass) {
    std::uint32_t saved = pass->colour;
    pass->SetColour(0xFFFFFFFF);
    CObject3D* currentObject = object;
    currentObject->flags |= MapObjectFlag::NoEdgeFade;
    currentObject->AdvanceTime(InterfaceClock());
    currentObject->DrawCollectDirty(pass);
    object->flags &= ~MapObjectFlag::NoEdgeFade;
    pass->SetColour(saved);
}

void UiModel::Load(const char* name, const char* from, const char* to) {
    auto* manager = g_clientWorld.MapObjectManager();
    auto* newObject = manager->CreateObjectAs<CObject3D>(MapObjectType::Creature, kPreviewId);
    object = newObject;
    newObject->AddRef();
    newObject->flags |= MapObjectFlag::Preview;
    newObject->localPosition.x = 0.0f;
    newObject->localPosition.y = 0.0f;
    newObject->localPosition.z = 3.0f;
    Object3DClientSpecific* specific = newObject->ClientSpecific();
    std::vector<std::string> parts;
    newObject->MakeFigure(name, &parts, 1);
    specific->RegisterPartMaterials();
    newObject->PlayAnimation(nullptr);
    newObject->RestartPose();
    specific->SetSkins(from, to);
}
