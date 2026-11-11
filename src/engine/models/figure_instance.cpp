// A figure in the scene (figure_instance.h): its parts by name, the animation state it plays, the points of its parts,
// and a figure alone: built from a model, dressed and drawn by the scene renderer without a game object. See
// docs/notes/map_objects.md ("Figures").
#include "engine/models/figure_instance.h"

#include "engine/core/engine_globals.h"   // g_sceneRenderer
#include "engine/core/engine_object.h"
#include "engine/core/manager_context.h"
#include "engine/models/figure_manager.h"
#include "engine/models/link_figure.h"   // LinkMesh, FigureMeshDrawer
#include "engine/models/link_loader.h"
#include "engine/models/material.h"
#include "engine/models/texture.h"
#include "engine/scene/device.h"
#include "engine/scene/light.h"
#include "engine/scene/pipeline.h"
#include "engine/scene/scene_renderer.h"
#include "lib/str.h"
#include "lib/text_case.h"
#include "render/render.h"

#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr const char* kClassName = "FigureInstance";

// The first of `parts` that `matches`, or null.
template <typename Parts, typename Match>
FigureMesh* FirstPart(const Parts& parts, Match matches) {
    for (int i = 0; i < std::ssize(parts); ++i) {
        FigureMesh* part = parts[i];
        if (matches(part)) return part;
    }
    return nullptr;
}

bool PartTooFar(const FigureMesh* part) { return LengthSquared(part->localPosition) > kPartsNearSquared; }

} // namespace

void ObjectWorldMatrix(const Quaternion* orientation, const Vector3& position, Matrix4* out) {
    orientation->ToMatrix(out->m);
    out->m[11] = position.z;
    out->m[3] = position.x;
    out->m[7] = position.y;
    std::memset(&out->m[12], 0, 3 * sizeof(float));
    out->m[15] = 1.0f;
}

void MarkPartBox(ClipBounds* bounds, std::string_view partName) {
    if (text::CompareIgnoringCase(partName.substr(0, 4), "BASE") == 0) bounds->flags |= BoundsFlag::Base;
    if (text::CompareIgnoringCase(partName.substr(0, 5), "CROWN") == 0) bounds->flags |= BoundsFlag::Crown;
    if (text::CompareIgnoringCase(partName.substr(0, 5), "EMPTY") == 0) {
        bounds->flags |= BoundsFlag::NoFootprint;
        bounds->flags |= BoundsFlag::Crown;
    }
}

render::MeshLighting FigureLighting(float coefficient) {
    render::MeshLighting lighting{};
    if (const Light* sun = g_sceneRenderer.sun) {
        lighting.sun = true;
        lighting.sunDirection = sun->direction;
        for (int k = 0; k < 3; ++k) lighting.sunColour[k] = sun->color[k];
    }
    const float* ambient = AmbientColour();
    for (int k = 0; k < 3; ++k) lighting.ambientColour[k] = ambient[k];
    lighting.coefficient = coefficient;
    return lighting;
}

// --- Lifetime ---------------------------------------------------------------------------------------

FigureInstance::FigureInstance() {
    localRotation.SetIdentity();
    worldRotation.SetIdentity();
    worldPosition = {};
}

FigureInstance::~FigureInstance() = default;

void FigureInstance::Create() {
    localPosition = {};
    localRotation.SetIdentity();
    flags = 0;
    name.clear();
    scale = {};
    defaultScale = {};
    drawnStep = {};
}

void FigureInstance::Destroy() {
    looks.clear();
    if (animation) animation->DetachMeshes();
    SetAnimationState(nullptr);
    parts.clear();
    figure.Reset();
    animations = nullptr;
    flags = 0;
}

const char* FigureInstance::ClassName() { return kClassName; }
ClassId FigureInstance::GetClassId() { return ClassId::FigureInstance; }

float FigureInstance::AnimationFactor(float) { return 1.0f; }

// --- Parts ------------------------------------------------------------------------------------------

FigureMesh* FigureInstance::FindPartByPrefix(std::string_view prefix) {
    // The original asks for the name twice; the parts return the same string both times.
    auto startsWith = [prefix](FigureMesh* part) { return std::string_view(part->GetName()).starts_with(prefix); };
    return FirstPart(parts, startsWith);
}

FigureMesh* FigureInstance::FindPart(std::string_view partName) {
    auto named = [partName](FigureMesh* part) { return str::Compare(part->GetName(), partName) == 0; };
    return FirstPart(parts, named);
}

FigureMesh* FigureInstance::FindPartOfType(int partType) {
    for (int i = 0; i < std::ssize(parts); ++i)
        if (parts[i]->type == partType) return parts[i];
    return nullptr;
}

std::vector<FigureMesh*> FigureInstance::CollectPartsByPrefix(std::string_view prefix) {
    std::vector<FigureMesh*> found;
    // The game asks each part for its name twice; parts return the same string both times.
    auto startsWith = [prefix](FigureMesh* part) { return std::string_view(part->GetName()).starts_with(prefix); };
    for (int i = 0; i < std::ssize(parts); ++i) {
        FigureMesh* part = parts[i];
        if (!startsWith(part)) continue;
        found.push_back(part);
    }
    return found;
}

bool FigureInstance::AddPart(FigureMesh* part) {
    bool present = false;
    for (int i = 0; i < std::ssize(parts) && !present; ++i) present = parts[i] == part;
    if (present) return false;
    parts.push_back(part);
    part->SetObject(this);
    if (animation) animation->AttachMesh(part);
    return true;
}

bool FigureInstance::TakePiece(FigureMesh* part, std::string_view partName) {
    LinkMesh* current = figure;
    if (!current->loader->LoadPartInto(current, std::string(partName))) return false;
    const LinkPartHeader& piece = current->headers.find(partName)->second;
    part->materialValue = piece.materialValue;
    part->meshType = piece.meshType;
    // The box and the pivot.
    part->box = piece.box;
    return true;
}

Vector3 FigureInstance::PartPoint(const FigureMesh* part, const Vector3& figureScale) const {
    const float scaleX = figureScale.x, scaleY = figureScale.y, scaleZ = figureScale.z;
    const Vector3 v = localRotation.Rotate(part->localPosition);
    return {v.x * scaleX + localPosition.x, v.y * scaleY + localPosition.y, v.z * scaleZ + localPosition.z};
}

void FigureInstance::SetPartFlags(std::uint32_t mask) {
    for (FigureMesh* part : parts) part->flags |= mask;
}

void FigureInstance::ClearPartFlags(std::uint32_t mask) {
    for (FigureMesh* part : parts) part->flags &= ~mask;
}

void FigureInstance::SetPartsFlatColour(std::uint32_t value) {
    for (FigureMesh* part : parts) part->flatColour = value;
}

void FigureInstance::SetPartsFlatSpecular(std::uint32_t value) {
    for (FigureMesh* part : parts) part->flatSpecular = value;
}

// --- The animation state ----------------------------------------------------------------------------

namespace {
constexpr float kFirstFrame = 0.0f;
} // namespace

void FigureInstance::SetAnimationState(FigureAnimationState* state) {
    if (animation) animation->owner = nullptr;
    animation.Assign(state);
    if (state) state->owner = this;
}

void FigureInstance::PlayAnimation(FigureAnimation* figureAnimation) {
    if (animation) animation->Play(figureAnimation);
}

int FigureInstance::AttachPartMesh(FigureMesh* part) {
    if (!animation) return 0;
    return animation->AttachMesh(part);
}

void FigureInstance::RestartPose() {
    if (!animation) return;
    animation->SetTime(kFirstFrame);
    animation->PlaceIfChanged();
}

// --- A figure alone ---------------------------------------------------------------------------------

bool FigureInstance::Build(std::string_view model, const Vector3& size, const std::vector<std::string>& partList) {
    FigureManager* manager = FigureManagerOf(this);
    // (Without the model the skeleton's load would end the game.)
    if (!manager->FindModel(model)) return false;
    if (!animation)
        SetAnimationState(manager->CreateObjectAs<FigureAnimationState>(FigureObjectType::AnimationState, kNoObjectId));
    name = model;
    scale = size;
    animation->Rescale(scale.x, scale.y, scale.z);
    animation->SetModel(model);
    LinkFigureLoader* loader = GetManagerContext()->FigureLoader();
    const std::vector<std::string> pieces = partList.empty() ? animation->BoneNames() : partList;
    if (partList.empty()) figure.Assign(loader->LoadLink(model, {scale.x, scale.y, scale.z}));
    else figure.Assign(loader->LoadFigure(model, pieces, {scale.x, scale.y, scale.z}));
    if (!figure) return false;
    for (const std::string& pieceName : pieces) {
        FigureMesh* part = manager->NewPart(name, pieceName, 0);
        if (!TakePiece(part, pieceName)) {
            manager->ReleaseObject(part);
            continue;
        }
        AddPart(part);
        part->box.pivot = part->localPosition;
        part->box.bounds.flags |= BoundsFlag::Loaded;
    }
    animation->Play(nullptr);
    animation->UpdatePose();
    return true;
}

void FigureInstance::Dress(std::string_view skin) {
    TextureManager* textures = GetManagerContext()->GetTextureManager();
    MaterialManager* materials = GetManagerContext()->GetMaterialManager();
    looks.clear();
    looks.resize(parts.size());
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const FigureMesh* part = parts[i];
        if (textures) looks[i].texture.Assign(textures->FindOrLoad(static_cast<int>(part->meshType), skin));
        if (materials) looks[i].material.Assign(materials->NewMaterial(static_cast<int>(part->materialValue)));
    }
}

void FigureInstance::Draw(SceneRenderer* context, float lightingCoefficient) {
    if (animation) animation->PlaceForDraw();
    render::MeshLighting lighting = FigureLighting(lightingCoefficient);
    ObjectWorldMatrix(&localRotation, localPosition, &worldMatrix);
    const Matrix4& view = *context->pipeline->Top(TransformPipeline::kWorldStack);
    TextureDevice* device = context->device;
    device->SetMaterial(nullptr);
    for (std::size_t i = 0; i < parts.size(); ++i) {
        FigureMesh* part = parts[i];
        part->flags &= ~FigureMeshFlag::Drawn;
        if (PartTooFar(part) || (part->flags & FigureMeshFlag::Hidden) || i >= looks.size() || !looks[i].material)
            continue;
        context->SetTexture(0, looks[i].texture);
        Matrix4 local, model;
        ObjectWorldMatrix(&part->localRotation, part->localPosition, &local);
        model.Multiply(worldMatrix.m, local.m);
        if (part->drawer->DrawLit(device, view, model, lighting, looks[i].material, 0xFF))
            part->flags |= FigureMeshFlag::Drawn;
    }
}
