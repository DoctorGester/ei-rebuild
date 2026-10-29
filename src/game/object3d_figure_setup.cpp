// CObject3D::SetUpAnimatedFigure: the figure of a model with an animation state (units,
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
constexpr std::uint32_t kBoundsSphere = 1;       // bounds.flags: radius valid
constexpr std::uint32_t kBoundsBox = 2;          // bounds.flags: box valid

// FootprintPart::flags set here.
constexpr std::uint32_t kBoxLoaded = 4;
constexpr std::uint32_t kBoxBase = 0x400;        // a part named "BASE..."
constexpr std::uint32_t kBoxCrown = 0x90;        // "CROWN..."
constexpr std::uint32_t kBoxEmpty = 0x890;       // "EMPTY..."

constexpr std::uint32_t kBoxEmptyMark = 0x800;
constexpr const char* kCantLoadPart = "CObject3D::LoadFigure.\n Can't load figure \"%s\" for object \"%s\".\n";
constexpr const char* kNoFigureManager = "CObject3D::LoadFigures.\n No valid figure manager.\n";
constexpr const char* kCantCreatePart = "CObject3D::LoadFigures.\n Can't create object \"%s\" with bodypart \"%s\".\n";


CLinkFigureLoader* FigureLoaderOf(const CObject3D* object) {
    return object->WorldContext()->FigureLoader();
}

} // namespace

void CObject3D::SetUpAnimatedFigure(std::string_view model) {
    CFigureManager* manager = FigureManagerOf(this);
    Vector3 high = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
    Vector3 low = {FLT_MAX, FLT_MAX, FLT_MAX};
    if (animation) {
        animation->Rescale(scale.x, scale.y, scale.z);
        animation->SetModel(model);
        animation->Play(nullptr);
        animation->UpdatePose();
        int count = std::ssize(animation->bones);
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
            box->bounds.flags |= kBoxLoaded;
            partNames[index] = bone->GetName();

            box = &partBoxes[index];
            high.x = MaxKeepB(high.x, static_cast<float>(box->pivot.x + box->bounds.boxMax.x + box->bounds.center.x));
            high.y = MaxKeepB(high.y, static_cast<float>(box->pivot.y + box->bounds.boxMax.y + box->bounds.center.y));
            high.z = MaxKeepB(high.z, static_cast<float>(box->pivot.z + box->bounds.boxMax.z + box->bounds.center.z));
            low.x = MinKeepA(static_cast<float>(box->pivot.x + box->bounds.boxMin.x + box->bounds.center.x), low.x);
            low.y = MinKeepA(static_cast<float>(box->pivot.y + box->bounds.boxMin.y + box->bounds.center.y), low.y);
            low.z = MinKeepA(static_cast<float>(box->pivot.z + box->bounds.boxMin.z + box->bounds.center.z), low.z);

            const std::string_view partName = bone->GetName();
            if (text::CompareIgnoringCase(partName.substr(0, 4), "BASE") == 0) box->bounds.flags |= kBoxBase;
            if (text::CompareIgnoringCase(partName.substr(0, 5), "CROWN") == 0) box->bounds.flags |= kBoxCrown;
            if (text::CompareIgnoringCase(partName.substr(0, 5), "EMPTY") == 0) box->bounds.flags |= kBoxEmpty;
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
    bounds.flags |= kBoundsBox;
    bounds.radius = LengthXYZ(bounds.boxMax);
    bounds.flags |= kBoundsSphere;
}

// --- Bounds of the placed parts -----------------------------------------------------------------

void CObject3D::UpdatePartBounds() {
    if (!(flags_ & MapObjectFlag::FiguresLoaded)) return;
    float highX = -FLT_MAX, highY = -FLT_MAX, highZ = -FLT_MAX;
    float lowX = FLT_MAX, lowY = FLT_MAX, lowZ = FLT_MAX;
    for (int i = 0; i < std::ssize(parts); ++i) {
        // A part is a figure mesh (figure_mesh.h) on a CTransformObject.
        parts[i]->UpdateWorld(localRotation_, localPosition_);
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
    bounds.flags |= kBoundsBox;
    bounds.flags |= kBoundsSphere;
    bounds.radius = LengthZYX(bounds.boxMin);
    bounds.center.x = centreX - localPosition_.x;
    bounds.center.y = centreY - localPosition_.y;
    bounds.center.z = centreZ - localPosition_.z;
}

// --- Parts by name prefix -----------------------------------------------------------------------

void CObject3D::CollectPartsByPrefix(std::string_view prefix, std::vector<CFigureMesh*>* out) {
    // The game asks each part for its name twice; parts return the same string both times.
    auto startsWith = [prefix](CFigureMesh* part) { return std::string_view(part->GetName()).starts_with(prefix); };
    for (int i = 0; i < std::ssize(parts); ++i) {
        auto* part = parts[i];
        if (!startsWith(part)) continue;
        int index = std::ssize(*out);
        out->resize(index + 1);
        (*out)[index] = part;
    }
    for (int i = 0; i < std::ssize(pendingParts); ++i) {
        auto* part = pendingParts[i];
        if (startsWith(part)) lib::SetAtGrow(*out, std::ssize(*out), part);
    }
}

// --- Loading single parts ---------------------------------------------------------------------

CFigureMesh* CObject3D::LoadPart(std::string_view partName) {
    CFigureManager* manager = FigureManagerOf(this);
    CFigureMesh* part = manager->NewPart(name, partName, flags_ & MapObjectFlag::Detached);
    if (!part) {
        game::DebugLog(kCantLoadPart, std::string(partName), name);
        return nullptr;
    }
    if (CLinkMesh* current = figure) {
        // The figure gets the piece (it loads it unless it has it).
        bool loaded;
        {
            std::string key(partName);
            loaded = current->LoadPart(&key);
        }
        if (!loaded) return nullptr;   // the part is kept, unreleased (as in the game)
        const LinkPartHeader& piece = current->headers_.find(std::string(partName))->second;
        part->materialValue_ = piece.materialValue;
        part->meshType_ = piece.meshType;
        // The box and the pivot.
        part->box_ = piece.box;
    }
    if (text::CompareIgnoringCase(partName.substr(0, 4), "BASE") == 0) part->box_.bounds.flags |= kBoxBase;
    if (text::CompareIgnoringCase(partName.substr(0, 5), "CROWN") == 0) part->box_.bounds.flags |= kBoxCrown;
    if (text::CompareIgnoringCase(partName.substr(0, 5), "EMPTY") == 0) {
        part->box_.bounds.flags |= kBoxEmptyMark;
        part->box_.bounds.flags |= kBoxCrown;
    }
    // Joins the parts unless it is there (appended).
    bool present = false;
    for (int i = 0; i < std::ssize(parts) && !present; ++i) present = parts[i] == part;
    if (!present) {
        parts.push_back(part);
        part->AddRef();
        part->SetObject(this);
        if (animation) animation->AttachMesh(part);
        if (attached) attached->OnPartAdded(part);
    }
    part->box_.pivot.x = part->localPosition_.x;
    part->box_.pivot.y = part->localPosition_.y;
    part->box_.bounds.flags |= kBoxLoaded;
    part->box_.pivot.z = part->localPosition_.z;
    return part;
}

int CObject3D::LoadParts(std::string_view model, const std::vector<std::string>* names) {
    CFigureManager* manager = FigureManagerOf(this);
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

int CObject3D::MakeFigure(std::string_view model, std::vector<std::string>* names, std::uint32_t arg) {
    name = model;
    if (animation) {
        animation->Rescale(scale.x, scale.y, scale.z);
        animation->SetModel(model);
    }
    if (names->empty() && animation) {
        animation->CollectBoneNames(names);
        std::string key(model);
        CLinkMesh* made = FigureLoaderOf(this)->LoadLink(&key, scale.x, scale.y, scale.z, false);
        figure.Assign(made);
    } else {
        // The piece names as the loader's LoadFigure reads them.
        std::vector<std::string> pieces = *names;
        {
            std::string key(model);
            CLinkMesh* made = FigureLoaderOf(this)->LoadFigure(&key, &pieces, scale.x, scale.y, scale.z, false);
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
    if (arg == 0 && !figure) flags_ &= ~MapObjectFlag::FiguresLoaded;
    else flags_ |= MapObjectFlag::FiguresLoaded;
    return 1;
}
