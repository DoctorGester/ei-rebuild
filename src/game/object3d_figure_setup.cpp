// Object3D::SetUpAnimatedFigure: the figure of a model with an animation state (units,
// levers, map objects with animations; called by the zone loader, the world's object setup, Lever::SetState
// and the unit controller): the state takes the model, one box and one name per animated part, and the
// object's bounds around the boxes. See map_object_figures.h and docs/notes/map_objects.md ("Figures").
#include "figure_manager.h"
#include "map_object_figures.h"

#include "ai_map.h"
#include "core_maths.h"
#include "core_transform.h"
#include "figure_mesh.h"
#include "link_figure.h"   // LinkHeaderMap: the figure's pieces
#include "world.h"

#include "lib/containers.h"
#include "lib/crt.h"
#include <cstdint>
#include <cstring>
#include <string>
#include "lib/text_case.h"
#include "lib/float_select.h"

#include <bit>
#include <cfloat>
#include <cmath>
#include <memory>
#include <new>
#include <vector>

namespace {
constexpr float kHeroHalfWidth = 0.5f;
constexpr const char* kCantLoadPart = "CObject3D::LoadFigure.\n Can't load figure \"%s\" for object \"%s\".\n";
constexpr const char* kNoFigureManager = "CObject3D::LoadFigures.\n No valid figure manager.\n";
constexpr const char* kCantCreatePart = "CObject3D::LoadFigures.\n Can't create object \"%s\" with bodypart \"%s\".\n";



} // namespace

void Object3D::SetUpAnimatedFigure(std::string_view model) {
    FigureManager* manager = FigureManagerOf(this);
    Vector3 high = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
    Vector3 low = {FLT_MAX, FLT_MAX, FLT_MAX};
    if (animation) {
        animation->Rescale(scale.x, scale.y, scale.z);
        animation->SetModel(model);
        animation->Play(nullptr);
        animation->UpdatePose();
        int count = lib::Count(animation->bones);
        partBoxes.resize(count);   // new boxes are all zero; the old ones are kept
        partNames.resize(animation->bones.size());

        FigureModel* figureModel;
        {
            std::string modelName(model);
            figureModel = manager->FindModel(&modelName);
        }
        int index = 0;
        for (auto node = animation->bones.begin(); node != animation->bones.end(); ++index) {
            FigureBone* bone = *node;
            ++node;
            FootprintPart* box = &partBoxes[index];
            manager->LoadFigureBounds(&box->bounds, figureModel, model, bone->GetName(),
                                            std::bit_cast<std::uint32_t>(scale.x),
                                            std::bit_cast<std::uint32_t>(scale.y),
                                            std::bit_cast<std::uint32_t>(scale.z));
            box = &partBoxes[index];
            box->pivot = {bone->position.x, bone->position.y, bone->position.z};
            box->bounds.flags |= BoundsFlag::Loaded;
            partNames[index] = bone->GetName();

            box = &partBoxes[index];
            high.x = MaxKeepB(high.x, static_cast<float>(box->pivot.x + box->bounds.boxMax.x + box->bounds.center.x));
            high.y = MaxKeepB(high.y, static_cast<float>(box->pivot.y + box->bounds.boxMax.y + box->bounds.center.y));
            high.z = MaxKeepB(high.z, static_cast<float>(box->pivot.z + box->bounds.boxMax.z + box->bounds.center.z));
            low.x = MinKeepA(static_cast<float>(box->pivot.x + box->bounds.boxMin.x + box->bounds.center.x), low.x);
            low.y = MinKeepA(static_cast<float>(box->pivot.y + box->bounds.boxMin.y + box->bounds.center.y), low.y);
            low.z = MinKeepA(static_cast<float>(box->pivot.z + box->bounds.boxMin.z + box->bounds.center.z), low.z);

            const std::string_view partName = bone->GetName();
            if (text::CompareIgnoringCase(partName.substr(0, 4), "BASE") == 0) box->bounds.flags |= BoundsFlag::Base;
            if (text::CompareIgnoringCase(partName.substr(0, 5), "CROWN") == 0) box->bounds.flags |= BoundsFlag::Crown;
            if (text::CompareIgnoringCase(partName.substr(0, 5), "EMPTY") == 0) box->bounds.flags |= BoundsFlag::NoFootprint | BoundsFlag::Crown;
        }
        animation->ReleaseBones();
    }
    if (MapType() == MapObjectType::Hero) {
        high.y = kHeroHalfWidth;
        high.x = kHeroHalfWidth;
        low.y = -kHeroHalfWidth;
        low.x = -kHeroHalfWidth;
    }
    bounds.center.x = static_cast<float>((low.x + high.x) * 0.5f);
    bounds.center.y = static_cast<float>((low.y + high.y) * 0.5f);
    bounds.center.z = static_cast<float>((low.z + high.z) * 0.5f);
    bounds.boxMin.x = low.x - bounds.center.x;
    bounds.boxMin.y = low.y - bounds.center.y;
    bounds.boxMin.z = low.z - bounds.center.z;
    bounds.boxMax.x = high.x - bounds.center.x;
    bounds.boxMax.y = high.y - bounds.center.y;
    bounds.boxMax.z = high.z - bounds.center.z;
    bounds.flags |= BoundsFlag::Box;
    bounds.radius = LengthXYZ(bounds.boxMax);
    bounds.flags |= BoundsFlag::Sphere;
}

// --- Bounds of the placed parts -----------------------------------------------------------------

void Object3D::UpdatePartBounds() {
    if (!(flags & MapObjectFlag::FiguresLoaded)) return;
    float highX = -FLT_MAX, highY = -FLT_MAX, highZ = -FLT_MAX;
    float lowX = FLT_MAX, lowY = FLT_MAX, lowZ = FLT_MAX;
    for (int i = 0; i < std::ssize(parts); ++i) {
        // A part is a figure mesh (figure_mesh.h) on a TransformObject.
        parts[i]->UpdateWorld(localRotation, localPosition);
        parts[i]->ExtendWorldBox(&highX, &highY, &highZ, &lowX, &lowY, &lowZ);
    }
    // The x sum stays on the FPU stack; the y and z sums are stored first.
    float centreX = static_cast<float>((highX + lowX) * 0.5f);
    float centreY = static_cast<float>(static_cast<float>(highY + lowY) * 0.5f);
    float centreZ = static_cast<float>(static_cast<float>(highZ + lowZ) * 0.5f);
    bounds.boxMax.x = highX - centreX;
    bounds.boxMax.y = highY - centreY;
    bounds.boxMax.z = highZ - centreZ;
    bounds.boxMin.x = lowX - centreX;
    bounds.boxMin.y = lowY - centreY;
    bounds.boxMin.z = lowZ - centreZ;
    bounds.flags |= BoundsFlag::Box;
    bounds.flags |= BoundsFlag::Sphere;
    bounds.radius = LengthZYX(bounds.boxMin);
    bounds.center.x = centreX - localPosition.x;
    bounds.center.y = centreY - localPosition.y;
    bounds.center.z = centreZ - localPosition.z;
}

// --- Parts by name prefix -----------------------------------------------------------------------

std::vector<FigureMesh*> Object3D::CollectPartsByPrefix(std::string_view prefix) {
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

// --- Loading single parts ---------------------------------------------------------------------

FigureMesh* Object3D::LoadPart(std::string_view partName) {
    FigureManager* manager = FigureManagerOf(this);
    FigureMesh* part = manager->NewPart(name, partName, flags & MapObjectFlag::Detached);
    if (!part) {
        game::DebugLog(kCantLoadPart, std::string(partName), name);
        return nullptr;
    }
    if (LinkMesh* current = figure) {
        // The figure gets the piece (it loads it unless it has it).
        bool loaded;
        {
            std::string key(partName);
            loaded = current->LoadPart(&key);
        }
        if (!loaded) return nullptr;   // the part is kept, unreleased (as in the game)
        const LinkPartHeader& piece = current->headers.find(std::string(partName))->second;
        part->materialValue = piece.materialValue;
        part->meshType = piece.meshType;
        // The box and the pivot.
        part->box = piece.box;
    }
    if (text::CompareIgnoringCase(partName.substr(0, 4), "BASE") == 0) part->box.bounds.flags |= BoundsFlag::Base;
    if (text::CompareIgnoringCase(partName.substr(0, 5), "CROWN") == 0) part->box.bounds.flags |= BoundsFlag::Crown;
    if (text::CompareIgnoringCase(partName.substr(0, 5), "EMPTY") == 0) {
        part->box.bounds.flags |= BoundsFlag::NoFootprint;
        part->box.bounds.flags |= BoundsFlag::Crown;
    }
    // Joins the parts unless it is there (appended).
    bool present = false;
    for (int i = 0; i < std::ssize(parts) && !present; ++i) present = parts[i] == part;
    if (!present) {
        parts.push_back(part);
        part->SetObject(this);
        if (animation) animation->AttachMesh(part);
        if (attached) attached->OnPartAdded(part);
    }
    part->box.pivot.x = part->localPosition.x;
    part->box.pivot.y = part->localPosition.y;
    part->box.bounds.flags |= BoundsFlag::Loaded;
    part->box.pivot.z = part->localPosition.z;
    return part;
}

int Object3D::LoadParts(std::string_view model, const std::vector<std::string>* names) {
    FigureManager* manager = FigureManagerOf(this);
    {
        std::string key(model);
        manager->FindModel(&key);   // loads and caches the model
    }
    name = model;
    if (!manager) return game::FatalError(kNoFigureManager);
    for (const std::string& part : *names) {
        if (!LoadPart(part)) game::DebugLog(kCantCreatePart, name, part);
    }
    return 1;
}

// --- Building the figure ----------------------------------------------------------------------

int Object3D::MakeFigure(std::string_view model, std::vector<std::string>* names, std::uint32_t arg) {
    name = model;
    if (animation) {
        animation->Rescale(scale.x, scale.y, scale.z);
        animation->SetModel(model);
    }
    if (names->empty() && animation) {
        *names = animation->BoneNames();
        std::string key(model);
        LinkMesh* made = WorldContext()->FigureLoader()->LoadLink(&key, scale.x, scale.y, scale.z);
        figure.Assign(made);
    } else {
        // The piece names as the loader's LoadFigure reads them.
        std::vector<std::string> pieces = *names;
        {
            std::string key(model);
            LinkMesh* made = WorldContext()->FigureLoader()->LoadFigure(&key, &pieces, scale.x, scale.y, scale.z);
            figure.Assign(made);
        }
    }
    LoadParts(model, names);
    ClassId classId = GetClassId();
    if ((classId == ClassId::Building || GetClassId() == ClassId::Unit) && animation) {
        animation->Play(nullptr);
        animation->UpdatePose();
    }
    ComputeBounds();
    if (arg == 0 && !figure) flags &= ~MapObjectFlag::FiguresLoaded;
    else flags |= MapObjectFlag::FiguresLoaded;
    return 1;
}
