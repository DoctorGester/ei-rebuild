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
#include "lib/mfc.h"
#include "lib/stlp.h"

#include "ei/hook.h"
#include "library_trees.h"

#include <bit>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <new>

namespace {
constexpr int kTypeHero = 0x32;                  // type_ of the player's units: a fixed x / y box
constexpr float kHeroHalfWidth = 0.5f;
constexpr float kHalf = 0.5f;
constexpr std::uint32_t kBoundsSphere = 1;       // bounds.flags: radius valid
constexpr std::uint32_t kBoundsBox = 2;          // bounds.flags: box valid
constexpr std::uint32_t kFlagFigureLoadedBits = 0x80;   // flags_: the figure is loaded

// FootprintPart::flags set here.
constexpr std::uint32_t kBoxLoaded = 4;
constexpr std::uint32_t kBoxBase = 0x400;        // a part named "BASE..."
constexpr std::uint32_t kBoxCrown = 0x90;        // "CROWN..."
constexpr std::uint32_t kBoxEmpty = 0x890;       // "EMPTY..."

// The part box array's grow step when none is set: an eighth of the size, 4..1024 (MFC CArray).
int DefaultGrowBy(int size) {
    int grow = size / 8;
    if (grow < 4) return 4;
    if (grow > 0x400) return 0x400;
    return grow;
}

// Zeroing (inlined here): new part boxes are all zero.
void ClearBoxes(FootprintPart* boxes, int count) {
    std::memset(boxes, 0, static_cast<std::size_t>(count) * sizeof(FootprintPart));
}

// The game's branch-free selects: the stored candidate replaces the bound when the comparison
// sets C0 (it lies beyond the bound, or one of them is NaN).
float Higher(float bound, float v) { return bound >= v ? bound : v; }
float Lower(float bound, float v) { return v >= bound ? bound : v; }
} // namespace

// MFC CArray<FootprintPart>::SetSize(count, -1), inlined.
static void SetBoxCount(CObject3D* object, int count) {
    if (count == 0) {
        if (object->partBoxes) {
            gamecrt::operator_delete(object->partBoxes);
            object->partBoxes = nullptr;
        }
        object->partBoxCapacity = 0;
        object->partBoxCount = 0;
    } else if (!object->partBoxes) {
        object->partBoxes = gamecrt::operator_new_array<FootprintPart>(count);
        ClearBoxes(object->partBoxes, count);
        object->partBoxCapacity = count;
        object->partBoxCount = count;
    } else if (count <= object->partBoxCapacity) {
        if (count > object->partBoxCount)
            ClearBoxes(object->partBoxes + object->partBoxCount, count - object->partBoxCount);
        object->partBoxCount = count;
    } else {
        int grow = object->partBoxGrowBy ? object->partBoxGrowBy : DefaultGrowBy(object->partBoxCount);
        int capacity = object->partBoxCapacity + grow;
        if (count >= capacity) capacity = count;
        auto* boxes = gamecrt::operator_new_array<FootprintPart>(capacity);
        std::memcpy(boxes, object->partBoxes, object->partBoxCount * sizeof(FootprintPart));
        ClearBoxes(boxes + object->partBoxCount, count - object->partBoxCount);
        gamecrt::operator_delete(object->partBoxes);
        object->partBoxCount = count;
        object->partBoxes = boxes;
        object->partBoxCapacity = capacity;
    }
}

void CObject3D::SetUpAnimatedFigure(const char* model) {
    CFigureManager* manager = FigureManagerOf(this);
    Vector3 high = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
    Vector3 low = {FLT_MAX, FLT_MAX, FLT_MAX};
    if (animation) {
        animation->Rescale(scale.x, scale.y, scale.z);
        animation->SetModel(model);
        animation->Play(nullptr);
        animation->UpdatePose();
        int count = animation->bones.count;
        SetBoxCount(this, count);
        partNames.resize(static_cast<std::size_t>(animation->bones.count));

        FigureModel* figureModel;
        {
            stlp::string modelName(model, model + std::strlen(model));
            figureModel = manager->FindModel(&modelName);
        }
        int index = 0;
        for (auto* node = animation->bones.head; node; ++index) {
            FigureBone* bone = node->data;
            node = node->next;
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
            high.x = Higher(high.x, static_cast<float>(box->pivot.x + box->bounds.boxMax.x + box->bounds.center.x));
            high.y = Higher(high.y, static_cast<float>(box->pivot.y + box->bounds.boxMax.y + box->bounds.center.y));
            high.z = Higher(high.z, static_cast<float>(box->pivot.z + box->bounds.boxMax.z + box->bounds.center.z));
            low.x = Lower(low.x, static_cast<float>(box->pivot.x + box->bounds.boxMin.x + box->bounds.center.x));
            low.y = Lower(low.y, static_cast<float>(box->pivot.y + box->bounds.boxMin.y + box->bounds.center.y));
            low.z = Lower(low.z, static_cast<float>(box->pivot.z + box->bounds.boxMin.z + box->bounds.center.z));

            const char* partName = bone->GetName();
            const int* fold = gamecrt::CaseFoldTable();
            if (gamecrt::SameIgnoringCase(fold, partName, "BASE", 4)) box->bounds.flags |= kBoxBase;
            if (gamecrt::SameIgnoringCase(fold, partName, "CROWN", 5)) box->bounds.flags |= kBoxCrown;
            if (gamecrt::SameIgnoringCase(fold, partName, "EMPTY", 5)) box->bounds.flags |= kBoxEmpty;
        }
        animation->ReleaseBones();
    }
    if (type_ == kTypeHero) {
        high.y = kHeroHalfWidth;
        high.x = kHeroHalfWidth;
        low.y = -kHeroHalfWidth;
        low.x = -kHeroHalfWidth;
    }
    bounds.center.x = static_cast<float>((low.x + high.x) * kHalf);
    bounds.center.y = static_cast<float>((low.y + high.y) * kHalf);
    bounds.center.z = static_cast<float>((low.z + high.z) * kHalf);
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

EI_REWRITTEN(0x005179E0, CObject3D::SetUpAnimatedFigure);

// --- Bounds of the placed parts -----------------------------------------------------------------

void CObject3D::UpdatePartBounds() {
    if (!(flags_ & kFlagFigureLoadedBits)) return;
    float highX = -FLT_MAX, highY = -FLT_MAX, highZ = -FLT_MAX;
    float lowX = FLT_MAX, lowY = FLT_MAX, lowZ = FLT_MAX;
    for (int i = 0; i < parts.size; ++i) {
        // A part is a figure mesh (figure_mesh.h) on a CTransformObject.
        parts.data[i]->UpdateWorld(localRotation_, localPosition_);
        parts.data[i]->ExtendWorldBox(&highX, &highY, &highZ, &lowX, &lowY, &lowZ);
    }
    // The x sum stays on the FPU stack; the y and z sums are stored first.
    float centreX = static_cast<float>((highX + lowX) * kHalf);
    float centreY = static_cast<float>(static_cast<float>(highY + lowY) * kHalf);
    float centreZ = static_cast<float>(static_cast<float>(highZ + lowZ) * kHalf);
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
EI_REWRITTEN(0x00518100, CObject3D::UpdatePartBounds);

// --- Parts by name prefix -----------------------------------------------------------------------

void CObject3D::CollectPartsByPrefix(const char* prefix, FigurePartArray* out) {
    // The game asks each part for its name twice; parts return the same string both times.
    auto startsWith = [prefix](CFigureMesh* part) { return std::strstr(part->GetName(), prefix) == part->GetName(); };
    for (int i = 0; i < parts.size; ++i) {
        auto* part = parts.data[i];
        if (!startsWith(part)) continue;
        int index = out->size;
        out->SetSize(index + 1, -1);
        out->data[index] = part;
    }
    for (int i = 0; i < pendingParts.size; ++i) {
        auto* part = pendingParts.data[i];
        if (startsWith(part)) out->SetAtGrow(out->size, part);
    }
}
EI_REWRITTEN(0x00516B50, CObject3D::CollectPartsByPrefix);

// --- Loading single parts ---------------------------------------------------------------------

namespace {
constexpr std::uint32_t kFlagDetached = 0x1000;   // flags_: passed to the figure manager
constexpr std::uint32_t kBoxEmptyMark = 0x800;
constexpr const char* kCantLoadPart = "CObject3D::LoadFigure.\n Can't load figure \"%s\" for object \"%s\".\n";
constexpr const char* kNoFigureManager = "CObject3D::LoadFigures.\n No valid figure manager.\n";
constexpr const char* kCantCreatePart = "CObject3D::LoadFigures.\n Can't create object \"%s\" with bodypart \"%s\".\n";

} // namespace

// Add (SetAtGrow at the size; RET 4).
EI_REWRITTEN(0x00519A10, AlignedPtrArray::Add);

CFigureMesh* CObject3D::LoadPart(const char* partName, FigureModel* model, std::uint32_t arg) {
    CFigureManager* manager = FigureManagerOf(this);
    CFigureMesh* part = manager->NewPart(name.c_str(), partName, model, std::bit_cast<std::uint32_t>(scale.x),
        std::bit_cast<std::uint32_t>(scale.y), std::bit_cast<std::uint32_t>(scale.z), arg, flags_ & kFlagDetached);
    if (!part) {
        game::DebugLog(kCantLoadPart, partName, name.c_str());
        return nullptr;
    }
    if (CLinkMesh* current = figure) {
        // The figure gets the piece (its slot 4 loads it unless it has it).
        bool loaded;
        {
            stlp::string key(partName, partName + std::strlen(partName));
            loaded = current->LoadPart(&key);
        }
        if (!loaded) return nullptr;   // the part is kept, unreleased (as in the game)
        const LinkHeaderMap::Node* node;
        {
            stlp::string key(partName, partName + std::strlen(partName));
            node = lib_trees::FindLinkHeader(&current->headers_, &key);
        }
        const LinkPartHeader& piece = node->second;
        part->materialValue_ = piece.materialValue;
        part->meshType_ = piece.meshType;
        // The box and the pivot.
        part->box_ = piece.box;
    }
    const int* fold = gamecrt::CaseFoldTable();
    if (gamecrt::SameIgnoringCase(fold, partName, "BASE", 4)) part->box_.bounds.flags |= kBoxBase;
    if (gamecrt::SameIgnoringCase(fold, partName, "CROWN", 5)) part->box_.bounds.flags |= kBoxCrown;
    if (gamecrt::SameIgnoringCase(fold, partName, "EMPTY", 5)) {
        part->box_.bounds.flags |= kBoxEmptyMark;
        part->box_.bounds.flags |= kBoxCrown;
    }
    // AddPart, inlined (appending).
    bool present = false;
    for (int i = 0; i < parts.size && !present; ++i) present = parts.data[i] == part;
    if (!present) {
        parts.Add(part);
        ++part->refCount_;
        part->SetObject(this);
        if (animation) animation->AttachMesh(part);
        if (attached) Specific()->OnPartAdded(part);
    }
    part->box_.pivot.x = part->localPosition_.x;
    part->box_.pivot.y = part->localPosition_.y;
    part->box_.bounds.flags |= kBoxLoaded;
    part->box_.pivot.z = part->localPosition_.z;
    return part;
}
EI_REWRITTEN(0x00517240, CObject3D::LoadPart);

int CObject3D::LoadParts(const char* model, const std::vector<std::string>* names, std::uint32_t arg) {
    CFigureManager* manager = FigureManagerOf(this);
    FigureModel* cached;
    {
        stlp::string key(model, model + std::strlen(model));
        cached = manager->FindModel(&key);
    }
    name = model;
    if (!manager) return game::FatalError(kNoFigureManager);
    for (const std::string& part : *names) {
        if (!LoadPart(part.c_str(), cached, arg)) game::DebugLog(kCantCreatePart, model, part.c_str());
    }
    return 1;
}
EI_REWRITTEN(0x00517110, CObject3D::LoadParts);

// --- Building the figure ----------------------------------------------------------------------

namespace {
constexpr int kClassUnit50 = 0x50;
constexpr int kClassUnit54 = 0x54;
constexpr std::uint32_t kFlagFigureLoaded = 0x80;   // flags_

// The piece names as the loader's slot 3 (LoadFigure) reads them, into an empty vector (the game
// sizes it with helper calls and assigns each name).
void FillPieceNames(stlp::vector<stlp::string>& pieces, const std::vector<std::string>* names) {
    int count = static_cast<int>(names->size());
    if (count > 0) {
        auto* start = stlp::allocate_as<stlp::string>(static_cast<std::size_t>(count));
        pieces.Start() = start;
        for (int i = 0; i < count; ++i) {
            const char* text = (*names)[i].c_str();
            new (&start[i]) stlp::string(text, text + std::strlen(text));
        }
        pieces.Finish() = start + count;
        pieces.EndOfStorage() = pieces.Finish();
    }
}

void FreePieceNames(stlp::vector<stlp::string>& pieces) {
    for (stlp::string& piece : pieces) piece.~string();
    pieces.FreeStorage();
}

CLinkFigureLoader* FigureLoaderOf(const CObject3D* object) {
    return object->WorldContext()->FigureLoader();
}
} // namespace

int CObject3D::MakeFigure(const char* model, std::vector<std::string>* names, std::uint32_t arg) {
    name = model;
    if (animation) {
        animation->Rescale(scale.x, scale.y, scale.z);
        animation->SetModel(model);
    }
    if (names->empty() && animation) {
        animation->CollectBoneNames(names);
        stlp::string key(model, model + std::strlen(model));
        CLinkMesh* made = FigureLoaderOf(this)->LoadLink(&key, scale.x, scale.y, scale.z, false);
        figure.Assign(made);
    } else {
        stlp::vector<stlp::string> pieces;
        FillPieceNames(pieces, names);
        {
            stlp::string key(model, model + std::strlen(model));
            CLinkMesh* made = FigureLoaderOf(this)->LoadFigure(&key, &pieces, scale.x, scale.y, scale.z, false);
            CLinkMesh* old = figure;
            figure = made;
            if (made) ++made->refCount_;
            // The old figure loses a reference and is deleted at the last one.
            if (old && --old->refCount_ == 0) delete old;
        }
        FreePieceNames(pieces);
    }
    LoadParts(model, names, arg);
    int classId = GetClassId();
    if ((classId == kClassUnit54 || GetClassId() == kClassUnit50) && animation) {
        animation->Play(nullptr);
        animation->UpdatePose();
    }
    ComputeBounds();
    if (arg == 0 && !figure) flags_ &= ~kFlagFigureLoaded;
    else flags_ |= kFlagFigureLoaded;
    return 1;
}
EI_REWRITTEN(0x00516DA0, CObject3D::MakeFigure);
