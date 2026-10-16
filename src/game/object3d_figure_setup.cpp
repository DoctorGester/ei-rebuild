// CObject3D::SetUpAnimatedFigure (005179E0): the figure of a model with an animation state (units,
// levers, map objects with animations; called by the zone loader, the world's object setup,
// Lever::SetState and the unit controller): the state takes the model, one box and one name per
// animated part, and the object's bounds around the boxes. See map_object_figures.h and
// docs/notes/map_objects.md ("Figures").
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
#include "originals_engine.h"
#include "originals_library.h"
#include "originals_units.h"

#include <bit>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <new>

namespace {
constexpr int kTypeHero = 0x32;                  // type_ of the player's units: a fixed x / y box
constexpr float kHeroHalfWidth = 0.5f;
constexpr float kHalf = 0.5f;                    // 0073B86C
constexpr std::uint32_t kBoundsSphere = 1;       // boundsFlags: radius valid
constexpr std::uint32_t kBoundsBox = 2;          // boundsFlags: box valid
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

// 00519FC0 (inlined here: it only zeroes): new part boxes are all zero.
void ClearBoxes(FootprintPart* boxes, int count) {
    std::memset(boxes, 0, static_cast<std::size_t>(count) * sizeof(FootprintPart));
}

// The original's branch-free selects: the stored candidate replaces the bound when the comparison
// sets C0 (it lies beyond the bound, or one of them is NaN).
float Higher(float bound, float v) { return !(bound >= v) ? v : bound; }
float Lower(float bound, float v) { return !(v >= bound) ? v : bound; }
} // namespace

// MFC CArray<FootprintPart>::SetSize(count, -1), inlined by the original.
static void SetBoxCount(CObject3D* object, int count) {
    if (count == 0) {
        if (object->partBoxes) {
            gamecrt::operator_delete(object->partBoxes);
            object->partBoxes = nullptr;
        }
        object->partBoxCapacity = 0;
        object->partBoxCount = 0;
    } else if (!object->partBoxes) {
        object->partBoxes = static_cast<FootprintPart*>(gamecrt::operator_new(count * sizeof(FootprintPart)));
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
        auto* boxes = static_cast<FootprintPart*>(gamecrt::operator_new(capacity * sizeof(FootprintPart)));
        std::memcpy(boxes, object->partBoxes, object->partBoxCount * sizeof(FootprintPart));
        ClearBoxes(boxes + object->partBoxCount, count - object->partBoxCount);
        gamecrt::operator_delete(object->partBoxes);
        object->partBoxCount = count;
        object->partBoxes = boxes;
        object->partBoxCapacity = capacity;
    }
}

void CObject3D::SetUpAnimatedFigure(const char* model) {
    CEngineObject* manager = static_cast<CWorld*>(ManagerContext())->FigureManager();
    Vector3 high = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
    Vector3 low = {FLT_MAX, FLT_MAX, FLT_MAX};
    if (animation) {
        static_cast<CFigureAnimationState*>(animation)->Rescale(scale.x, scale.y, scale.z);
        animation->SetModel(model);
        animation->Play(nullptr);
        animation->UpdatePose();
        int count = animation->bones.count;
        SetBoxCount(this, count);
        StringArraySetSize(&partNameArrayVtable, nullptr, animation->bones.count, -1);

        void* figureModel;
        {
            stlp::string modelName(model, model + std::strlen(model));
            figureModel = static_cast<CFigureManager*>(manager)->FindModel(&modelName);
        }
        int index = 0;
        for (auto* node = animation->bones.head; node; ++index) {
            FigureBone* bone = node->data;
            node = node->next;
            FootprintPart* box = &partBoxes[index];
            static_cast<CFigureManager*>(manager)->LoadFigureBounds(reinterpret_cast<ClipBounds*>(box), static_cast<FigureModel*>(figureModel), model, bone->GetName(),
                                            std::bit_cast<std::uint32_t>(scale.x),
                                            std::bit_cast<std::uint32_t>(scale.y),
                                            std::bit_cast<std::uint32_t>(scale.z));
            box = &partBoxes[index];
            box->pivot = {bone->position.x, bone->position.y, bone->position.z};
            box->flags |= kBoxLoaded;
            reinterpret_cast<mfc::CString*>(partNames)[index] = bone->GetName();

            box = &partBoxes[index];
            high.x = Higher(high.x, static_cast<float>(box->pivot.x + box->boxMax.x + box->offset.x));
            high.y = Higher(high.y, static_cast<float>(box->pivot.y + box->boxMax.y + box->offset.y));
            high.z = Higher(high.z, static_cast<float>(box->pivot.z + box->boxMax.z + box->offset.z));
            low.x = Lower(low.x, static_cast<float>(box->pivot.x + box->boxMin.x + box->offset.x));
            low.y = Lower(low.y, static_cast<float>(box->pivot.y + box->boxMin.y + box->offset.y));
            low.z = Lower(low.z, static_cast<float>(box->pivot.z + box->boxMin.z + box->offset.z));

            const char* partName = bone->GetName();
            const int* fold = gamecrt::CaseFoldTable();
            if (gamecrt::SameIgnoringCase(fold, partName, "BASE", 4)) box->flags |= kBoxBase;
            if (gamecrt::SameIgnoringCase(fold, partName, "CROWN", 5)) box->flags |= kBoxCrown;
            if (gamecrt::SameIgnoringCase(fold, partName, "EMPTY", 5)) box->flags |= kBoxEmpty;
        }
        animation->ReleaseBones();
    }
    if (type_ == kTypeHero) {
        high.y = kHeroHalfWidth;
        high.x = kHeroHalfWidth;
        low.y = -kHeroHalfWidth;
        low.x = -kHeroHalfWidth;
    }
    center.x = static_cast<float>((low.x + high.x) * kHalf);
    center.y = static_cast<float>((low.y + high.y) * kHalf);
    center.z = static_cast<float>((low.z + high.z) * kHalf);
    below.x = low.x - center.x;
    below.y = low.y - center.y;
    below.z = low.z - center.z;
    above.x = high.x - center.x;
    above.y = high.y - center.y;
    above.z = high.z - center.z;
    boundsFlags |= kBoundsBox;
    radius = LengthXYZ(above);
    boundsFlags |= kBoundsSphere;
}

EI_REPLACE_METHOD(0x005179E0, CObject3D::SetUpAnimatedFigure);

// --- Bounds of the placed parts -----------------------------------------------------------------

void CObject3D::UpdatePartBounds() {
    if (!(flags_ & kFlagFigureLoadedBits)) return;
    float highX = -FLT_MAX, highY = -FLT_MAX, highZ = -FLT_MAX;
    float lowX = FLT_MAX, lowY = FLT_MAX, lowZ = FLT_MAX;
    for (int i = 0; i < parts.size; ++i) {
        // A part is a figure mesh (figure_mesh.h) on a CTransformObject.
        static_cast<CTransformObject*>(parts.data[i])
            ->UpdateWorld(orientation, position);
        static_cast<const CFigureMesh*>(parts.data[i])->ExtendWorldBox(&highX, &highY, &highZ, &lowX, &lowY, &lowZ);
    }
    // The x sum stays on the FPU stack; the y and z sums are stored first.
    float centreX = static_cast<float>((highX + lowX) * kHalf);
    float centreY = static_cast<float>(static_cast<float>(highY + lowY) * kHalf);
    float centreZ = static_cast<float>(static_cast<float>(highZ + lowZ) * kHalf);
    above.x = highX - centreX;
    above.y = highY - centreY;
    above.z = highZ - centreZ;
    below.x = lowX - centreX;
    below.y = lowY - centreY;
    below.z = lowZ - centreZ;
    boundsFlags |= kBoundsBox;
    boundsFlags |= kBoundsSphere;
    radius = LengthZYX(below);
    center.x = centreX - position.x;
    center.y = centreY - position.y;
    center.z = centreZ - position.z;
}
EI_REPLACE_METHOD(0x00518100, CObject3D::UpdatePartBounds);

// --- Parts by name prefix -----------------------------------------------------------------------

void CObject3D::CollectPartsByPrefix(const char* prefix, AlignedPtrArray* out) {
    // The original asks each part for its name twice; parts return the same string both times.
    auto startsWith = [prefix](CFigureMesh* part) { return std::strstr(part->GetName(), prefix) == part->GetName(); };
    for (int i = 0; i < parts.size; ++i) {
        auto* part = static_cast<CFigureMesh*>(parts.data[i]);
        if (!startsWith(part)) continue;
        int index = out->size;
        out->SetSize(index + 1, -1);   // 00519D70
        out->data[index] = part;
    }
    for (int i = 0; i < pendingParts.size; ++i) {
        auto* part = static_cast<CFigureMesh*>(pendingParts.data[i]);
        if (startsWith(part)) out->SetAtGrow(out->size, part);   // inlined by the original
    }
}
EI_REPLACE_METHOD(0x00516B50, CObject3D::CollectPartsByPrefix);

// --- Loading single parts ---------------------------------------------------------------------

namespace {
constexpr std::uint32_t kFlagDetached = 0x1000;   // flags_: passed to the figure manager
constexpr std::uint32_t kBoxEmptyMark = 0x800;    // 005174E0(1) on the box
constexpr const char* kCantLoadPart = "CObject3D::LoadFigure.\n Can't load figure \"%s\" for object \"%s\".\n";
constexpr const char* kNoFigureManager = "CObject3D::LoadFigures.\n No valid figure manager.\n";
constexpr const char* kCantCreatePart = "CObject3D::LoadFigures.\n Can't create object \"%s\" with bodypart \"%s\".\n";

// An MFC CStringArray as 00517110 reads it.
using PartNameArray = mfc::CStringArray;

// 00519A10 (AlignedPtrArray::Add, below), through its address so its wrapper runs.
__declspec(naked) int __fastcall AddAt(AlignedPtrArray*, void*, void*) EI_ORIGINAL(0x00519A10)

CEngineObject* FigureManagerOf(const CObject3D* object) {
    return static_cast<CWorld*>(object->ManagerContext())->FigureManager();
}
} // namespace

// 00519A10: Add (SetAtGrow at the size; RET 4).
EI_REPLACE_METHOD(0x00519A10, AlignedPtrArray::Add);

CFigureMesh* CObject3D::LoadPart(const char* partName, void* model, std::uint32_t arg) {
    CEngineObject* manager = FigureManagerOf(this);
    auto* part = static_cast<CFigureMesh*>(static_cast<CFigureManager*>(manager)->NewPart(name, partName, static_cast<FigureModel*>(model), std::bit_cast<std::uint32_t>(scale.x),
        std::bit_cast<std::uint32_t>(scale.y), std::bit_cast<std::uint32_t>(scale.z), arg, flags_ & kFlagDetached));
    if (!part) {
        game::DebugLog(kCantLoadPart, partName, name);
        return nullptr;
    }
    if (CLinkMesh* current = figure) {
        // The figure gets the piece (its slot 4 loads it unless it has it).
        bool loaded;
        {
            stlp::string key(partName, partName + std::strlen(partName));
            loaded = current->LoadPart(&key);
        }
        if (!loaded) return nullptr;   // the part is kept, unreleased (as the original)
        void* node;
        {
            stlp::string key(partName, partName + std::strlen(partName));
            StringMapFindOriginal(&current->headers_, nullptr, &node, &key);
        }
        const LinkPartHeader& piece = static_cast<const LinkHeaderMap::Node*>(node)->second;
        part->materialValue_ = piece.materialValue;
        part->meshType_ = piece.meshType;
        // The box and the pivot (bounds_, boxPivot_) as one FootprintPart.
        std::memcpy(&part->bounds_, &piece.bounds, sizeof(FootprintPart));
    }
    const int* fold = gamecrt::CaseFoldTable();
    if (gamecrt::SameIgnoringCase(fold, partName, "BASE", 4)) part->bounds_.flags |= kBoxBase;
    if (gamecrt::SameIgnoringCase(fold, partName, "CROWN", 5)) part->bounds_.flags |= kBoxCrown;
    if (gamecrt::SameIgnoringCase(fold, partName, "EMPTY", 5)) {
        part->bounds_.flags |= kBoxEmptyMark;
        part->bounds_.flags |= kBoxCrown;
    }
    // AddPart, inlined by the original (appending with 00519A10).
    bool present = false;
    for (int i = 0; i < parts.size && !present; ++i) present = parts.data[i] == part;
    if (!present) {
        AddAt(&parts, nullptr, part);
        ++part->refCount_;
        part->SetObject(this);
        if (animation) static_cast<CFigureAnimationState*>(animation)->AttachMesh(part);
        if (attached) Specific()->OnPartAdded(part);
    }
    part->boxPivot_.x = part->localPosition_.x;
    part->boxPivot_.y = part->localPosition_.y;
    part->bounds_.flags |= kBoxLoaded;
    part->boxPivot_.z = part->localPosition_.z;
    return part;
}
EI_REPLACE_METHOD(0x00517240, CObject3D::LoadPart);

int CObject3D::LoadParts(const char* model, const void* partList, std::uint32_t arg) {
    CEngineObject* manager = FigureManagerOf(this);
    void* cached;
    {
        stlp::string key(model, model + std::strlen(model));
        cached = static_cast<CFigureManager*>(manager)->FindModel(&key);
    }
    *reinterpret_cast<mfc::CString*>(&name) = model;
    if (!manager) return game::FatalError(kNoFigureManager);
    const auto* names = static_cast<const PartNameArray*>(partList);
    for (int i = 0; i < names->size; ++i) {
        if (!LoadPart(static_cast<const char*>((*names)[i]), cached, arg)) game::DebugLog(kCantCreatePart, model, static_cast<const char*>((*names)[i]));
    }
    return 1;
}
EI_REPLACE_METHOD(0x00517110, CObject3D::LoadParts);

// --- Building the figure ----------------------------------------------------------------------

namespace {
constexpr int kClassUnit50 = 0x50;
constexpr int kClassUnit54 = 0x54;
constexpr std::uint32_t kFlagFigureLoaded = 0x80;   // flags_

// The piece names as the loader's slot 3 (LoadFigure) reads them, into an empty vector (the original
// sizes it with 005195B0 / 004DDBA0 and assigns each name).
void FillPieceNames(stlp::vector<stlp::string>& pieces, const PartNameArray* names) {
    int count = names->size;
    if (count > 0) {
        auto* start = static_cast<stlp::string*>(stlp::allocate(static_cast<std::size_t>(count) * sizeof(stlp::string)));
        pieces.Start() = start;
        for (int i = 0; i < count; ++i) {
            const char* text = static_cast<const char*>((*names)[i]);
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
    return static_cast<CWorld*>(object->ManagerContext())->FigureLoader();
}
} // namespace

int CObject3D::MakeFigure(const char* model, void* partList, std::uint32_t arg) {
    *reinterpret_cast<mfc::CString*>(&name) = model;
    if (animation) {
        static_cast<CFigureAnimationState*>(animation)->Rescale(scale.x, scale.y, scale.z);
        animation->SetModel(model);
    }
    auto* names = static_cast<const PartNameArray*>(partList);
    if (names->size == 0 && animation) {
        animation->CollectBoneNames(static_cast<mfc::CStringArray*>(partList));
        stlp::string key(model, model + std::strlen(model));
        CLinkMesh* made = FigureLoaderOf(this)->LoadLink(&key, scale.x, scale.y, scale.z, false);
        FigureRef().Assign(made);
    } else {
        stlp::vector<stlp::string> pieces;
        FillPieceNames(pieces, names);
        {
            stlp::string key(model, model + std::strlen(model));
            CLinkMesh* made = FigureLoaderOf(this)->LoadFigure(&key, &pieces, scale.x, scale.y, scale.z, false);
            CLinkMesh* old = figure;
            figure = made;
            if (made) ++made->refCount_;
            // 00457720: the old figure loses a reference and is deleted at the last one.
            if (old && --old->refCount_ == 0) old->DeleteThis(1);
        }
        FreePieceNames(pieces);
    }
    LoadParts(model, partList, arg);
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
EI_REPLACE_METHOD(0x00516DA0, CObject3D::MakeFigure);
